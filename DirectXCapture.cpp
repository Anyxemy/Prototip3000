#define NOMINMAX // Защита от макросов Windows min/max
#include "DirectXCapture.h"
#include <iostream>
#include <d3d11.h>
#include <Windows.Graphics.DirectX.Direct3D11.interop.h>
#include <windows.graphics.capture.interop.h>
#include <winrt/Windows.Foundation.h>

// Подключаем модули OpenCV CUDA
#include <opencv2/cudaimgproc.hpp>
#include <opencv2/cudaarithm.hpp> // Лечит ошибку с threshold

#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "d3d11.lib")

// Если вы идете по Плану Б, линкуем математику CUDA вручную
#pragma comment(lib, "opencv_cudaarithm4.lib") 

// Вспомогательная функция создания устройства
winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice CreateEngineDevice() {
    winrt::com_ptr<ID3D11Device> d3d11Device;
    winrt::com_ptr<ID3D11DeviceContext> d3d11Context;

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr, 0, D3D11_SDK_VERSION,
        d3d11Device.put(), nullptr, d3d11Context.put()
    );

    if (FAILED(hr)) return nullptr;

    winrt::com_ptr<IDXGIDevice> dxgiDevice;
    d3d11Device->QueryInterface(__uuidof(IDXGIDevice), dxgiDevice.put_void());

    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice device{ nullptr };
    hr = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), reinterpret_cast<::IInspectable**>(winrt::put_abi(device)));

    return device;
}

DirectXCapture::DirectXCapture() {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
}

DirectXCapture::~DirectXCapture() {
    Stop();
}

bool DirectXCapture::Initialize(const wchar_t* windowTitle) {
    m_hwnd = FindWindowW(NULL, windowTitle);
    if (!m_hwnd) {
        std::wcout << L"[-] Ошибка: Окно '" << windowTitle << L"' не найдено!\n";
        return false;
    }

    auto factory = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    HRESULT hr = factory->CreateForWindow(m_hwnd, winrt::guid_of<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>(), winrt::put_abi(m_captureItem));

    return (SUCCEEDED(hr) && m_captureItem);
}

bool DirectXCapture::Start(const AppProfile& profile) {
    if (m_isCapturing) return true;
    if (!m_captureItem) return false;

    m_currentProfile = &profile; // Запоминаем профиль

    auto size = m_captureItem.Size();
    auto device = CreateEngineDevice();
    if (!device) return false;

    m_framePool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
        device, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size
    );

    // Четкая структура лямбды без каши
    m_framePool.FrameArrived([this](winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender, winrt::Windows::Foundation::IInspectable const&) {
        auto frame = sender.TryGetNextFrame();
        if (frame) {
            this->ProcessFrameToGPU(frame);
        }
        });

    m_captureSession = m_framePool.CreateCaptureSession(m_captureItem);
    m_captureSession.StartCapture();
    m_isCapturing = true;
    return true;
}

void DirectXCapture::ProcessFrameToGPU(winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame const& frame) {
    if (!m_currentProfile) return;

    auto d3dSurface = frame.Surface();
    winrt::com_ptr<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> interopAccess;
    d3dSurface.as(interopAccess);

    winrt::com_ptr<ID3D11Texture2D> d3dTexture;
    interopAccess->GetInterface(IID_PPV_ARGS(d3dTexture.put()));
    if (!d3dTexture) return;

    D3D11_TEXTURE2D_DESC desc;
    d3dTexture->GetDesc(&desc);

    winrt::com_ptr<ID3D11Device> d3dDevice;
    d3dTexture->GetDevice(d3dDevice.put());
    winrt::com_ptr<ID3D11DeviceContext> context;
    d3dDevice->GetImmediateContext(context.put());

    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    winrt::com_ptr<ID3D11Texture2D> stagingTexture;
    if (FAILED(d3dDevice->CreateTexture2D(&stagingDesc, nullptr, stagingTexture.put()))) return;

    context->CopyResource(stagingTexture.get(), d3dTexture.get());

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(context->Map(stagingTexture.get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        try {
            // Обертка в базовые матрицы
            cv::Mat cpuFrame(desc.Height, desc.Width, CV_8UC4, mapped.pData, mapped.RowPitch);
            cv::cuda::GpuMat gpuFrame;
            gpuFrame.upload(cpuFrame);

            cv::cuda::GpuMat gpuBGR;
            cv::cuda::cvtColor(gpuFrame, gpuBGR, cv::COLOR_BGRA2BGR);

            m_outputFrames["Full_Screen_Snapshot"] = cpuFrame.clone();
            // Четкий перебор зон из профиля данных
            for (const auto& [zoneName, zoneData] : m_currentProfile->GetAllZones()) {
                // Проверка границ, чтобы не выйти за текстуру игры
                if (zoneData.rect.x + zoneData.rect.width <= desc.Width &&
                    zoneData.rect.y + zoneData.rect.height <= desc.Height) {

                    cv::cuda::GpuMat gpuZoneMat(gpuBGR, zoneData.rect);

                    if (zoneData.type == ZoneType::ResourceDigits) {
                        cv::cuda::GpuMat gpuGray, gpuThreshold;
                        cv::cuda::cvtColor(gpuZoneMat, gpuGray, cv::COLOR_BGR2GRAY);
                        cv::cuda::threshold(gpuGray, gpuThreshold, 128, 255, cv::THRESH_BINARY);

                        cv::Mat cpuVisual;
                        gpuThreshold.download(cpuVisual);

                        // Безопасно сохраняем кадр в буфер класса
                        std::lock_guard<std::mutex> lock(m_frameMutex);
                        m_outputFrames[zoneName] = cpuVisual;
                    }
                }
            } // Конец цикла for

            context->Unmap(stagingTexture.get(), 0);
            if (cv::waitKey(1) == 27) Stop(); // Нажатие ESC останавливает сессию

        }
        catch (...) {
            context->Unmap(stagingTexture.get(), 0);
        }
    }
}

void DirectXCapture::Stop() {
    if (!m_isCapturing) return;
    m_isCapturing = false;
    if (m_captureSession) { m_captureSession.Close(); m_captureSession = nullptr; }
    if (m_framePool) { m_framePool.Close(); m_framePool = nullptr; }
    m_captureItem = nullptr;
    cv::destroyAllWindows();
}

bool DirectXCapture::IsCapturing() const { return m_isCapturing; }

std::map<std::string, cv::Mat> DirectXCapture::GetLatestZones() {
    std::lock_guard<std::mutex> lock(m_frameMutex);
    return m_outputFrames;
}
