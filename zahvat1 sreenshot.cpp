#include <iostream>
#include <windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <Windows.Graphics.DirectX.Direct3D11.interop.h>
#include <windows.graphics.capture.interop.h>
#include <winrt/Windows.Foundation.h>

// Подключаем системные библиотеки для линкера
#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "Windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")

// Создание DirectX устройства
winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice CreateDirect3DDevice2() {
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

class DirectXCapture {
private:
    HWND m_hwnd = nullptr;
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_captureItem{ nullptr };
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool m_framePool{ nullptr };
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession m_captureSession{ nullptr };
    bool m_isCapturing = false;

    // Метод сохранения PNG
    void ProcessAndSaveFrame(winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame const& frame) {
        // ИСПРАВЛЕНИЕ 1: Используем правильный метод .Surface()
        auto d3dSurface = frame.Surface();

        winrt::com_ptr<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> interopAccess;
        d3dSurface.as(interopAccess);

        winrt::com_ptr<ID3D11Texture2D> texture;
        interopAccess->GetInterface(IID_PPV_ARGS(texture.put()));

        if (!texture) return;

        D3D11_TEXTURE2D_DESC desc;
        texture->GetDesc(&desc);

        winrt::com_ptr<IWICImagingFactory> wicFactory;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wicFactory.put()));

        winrt::com_ptr<IWICBitmapEncoder> encoder;
        wicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put());

        IStream* stream = nullptr;
        SHCreateStreamOnFileW(L"capture.png", STGM_CREATE | STGM_WRITE, &stream);
        encoder->Initialize(stream, WICBitmapEncoderNoCache);

        winrt::com_ptr<IWICBitmapFrameEncode> frameEncode;
        encoder->CreateNewFrame(frameEncode.put(), nullptr);
        frameEncode->Initialize(nullptr);
        frameEncode->SetSize(desc.Width, desc.Height);

        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        frameEncode->SetPixelFormat(&format);

        winrt::com_ptr<ID3D11Device> d3dDevice;
        texture->GetDevice(d3dDevice.put());
        winrt::com_ptr<ID3D11DeviceContext> context;
        d3dDevice->GetImmediateContext(context.put());

        D3D11_TEXTURE2D_DESC stagingDesc = desc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;

        winrt::com_ptr<ID3D11Texture2D> stagingTexture;
        d3dDevice->CreateTexture2D(&stagingDesc, nullptr, stagingTexture.put());

        // ИСПРАВЛЕНИЕ 2: Используем .get() для извлечения сырых указателей интерфейса
        context->CopyResource(stagingTexture.get(), texture.get());

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(context->Map(stagingTexture.get(), 0, D3D11_MAP_READ, 0, &mapped))) {
            frameEncode->WritePixels(desc.Height, mapped.RowPitch, mapped.RowPitch * desc.Height, (BYTE*)mapped.pData);
            context->Unmap(stagingTexture.get(), 0);
        }

        frameEncode->Commit();
        encoder->Commit();
        stream->Release();

        std::wcout << L"[***] УСПЕХ! Кадр из видеопамяти успешно сохранен в файл 'capture.png'\n";

        Stop();
        exit(0);
    }

public:
    DirectXCapture() {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    }

    ~DirectXCapture() {
        Stop();
    }

    bool Initialize(const wchar_t* windowTitle) {
        m_hwnd = FindWindowW(NULL, windowTitle);
        if (!m_hwnd) {
            std::wcout << L"[-] Ошибка: Окно '" << windowTitle << L"' не найдено в системе!\n";
            return false;
        }

        auto factory = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        HRESULT hr = factory->CreateForWindow(m_hwnd, winrt::guid_of<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>(), winrt::put_abi(m_captureItem));

        if (FAILED(hr) || !m_captureItem) return false;

        auto size = m_captureItem.Size();
        std::wcout << L"[+] Подключились к окну. Разрешение: " << size.Width << L"x" << size.Height << L"px\n";
        return true;
    }

    bool Start() {
        if (m_isCapturing) return true;
        if (!m_captureItem) return false;

        auto size = m_captureItem.Size();
        auto device = CreateDirect3DDevice2();
        if (!device) return false;

        m_framePool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
            device,
            winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            2,
            size
        );

        m_framePool.FrameArrived([this](winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender, winrt::Windows::Foundation::IInspectable const&) {
            auto frame = sender.TryGetNextFrame();
            if (frame) {
                this->ProcessAndSaveFrame(frame);
            }
            });

        m_captureSession = m_framePool.CreateCaptureSession(m_captureItem);
        m_captureSession.StartCapture();
        m_isCapturing = true;

        std::cout << "[+] Ожидаем первый прилетевший кадр...\n";
        return true;
    }

    void Stop() {
        if (!m_isCapturing) return;
        m_isCapturing = false;

        if (m_captureSession) { m_captureSession.Close(); m_captureSession = nullptr; }
        if (m_framePool) { m_framePool.Close(); m_framePool = nullptr; }
        m_captureItem = nullptr;
    }
};

int main2() {
    std::setlocale(LC_ALL, "Russian");
    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);

    DirectXCapture capturer;

    // Укажите точное имя целевой программы (например, открытого Блокнота)
    const wchar_t* targetWindow = L"dotAGE";

    if (capturer.Initialize(targetWindow)) {
        if (capturer.Start()) {
            std::cin.get();
            capturer.Stop();
        }
    }
    return 0;
}
