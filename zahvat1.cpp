#include <iostream>
#include <windows.h>
#include <d3d11.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <Windows.Graphics.DirectX.Direct3D11.interop.h>
#include <windows.graphics.capture.interop.h>
#include <winrt/Windows.Foundation.h>

// Подключаем OpenCV с поддержкой CUDA
#include <opencv2/opencv.hpp>
#include <opencv2/core/cuda.hpp>
#include <opencv2/cudaimgproc.hpp>
#include <tlhelp32.h>

// Подключаем системные библиотеки для линкера
#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "d3d11.lib")

class GameController {
public:
    // Функция для симуляции клика левой кнопкой мыши по точным координатам экрана
    static void LeftClick(int x, int y) {
        // 1. Перемещаем курсор в целевую точку
        // Используем абсолютные координаты Windows (0-65535) для точности на любых мониторах
        double screenWidth = GetSystemMetrics(SM_CXSCREEN);
        double screenHeight = GetSystemMetrics(SM_CYSCREEN);

        INPUT inputMove = { 0 };
        inputMove.type = INPUT_MOUSE;
        inputMove.mi.dx = static_cast<LONG>((x * 65535.0f) / screenWidth);
        inputMove.mi.dy = static_cast<LONG>((y * 65535.0f) / screenHeight);
        inputMove.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;

        SendInput(1, &inputMove, sizeof(INPUT));
        Sleep(50); // Маленькая пауза для стабильности интерфейса игры

        // 2. Симулируем нажатие левой кнопки мыши
        INPUT inputDown = { 0 };
        inputDown.type = INPUT_MOUSE;
        inputDown.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        SendInput(1, &inputDown, sizeof(INPUT));
        Sleep(50);

        // 3. Симулируем отпускание левой кнопки мыши
        INPUT inputUp = { 0 };
        inputUp.type = INPUT_MOUSE;
        inputUp.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(1, &inputUp, sizeof(INPUT));

        std::cout << "[*] ИИ произвел физический клик по координатам: " << x << "x" << y << "\n";
    }

    // Функция для симуляции нажатия клавиатурной клавиши (например, ESC или Пробел)
    static void PressKey(WORD vKey) {
        INPUT input[2] = { 0 };

        // Нажатие
        input[0].type = INPUT_KEYBOARD;
        input[0].ki.wVk = vKey;

        // Отпускание
        input[1].type = INPUT_KEYBOARD;
        input[1].ki.wVk = vKey;
        input[1].ki.dwFlags = MOUSEEVENTF_LEFTUP;

        SendInput(2, input, sizeof(INPUT));
    }
};

void DetectGameGraphicsAPI(const wchar_t* windowTitle) {
    HWND hwnd = FindWindowW(NULL, windowTitle);
    if (!hwnd) return;

    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);

    // Делаем снимок всех DLL-модулей, загруженных в игру
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
    if (hSnapshot == INVALID_HANDLE_VALUE) return;

    MODULEENTRY32W modEntry;
    modEntry.dwSize = sizeof(MODULEENTRY32W);

    bool isD3D11 = false, isD3D12 = false, isVulkan = false, isOpenGL = false;

    if (Module32FirstW(hSnapshot, &modEntry)) {
        do {
            if (_wcsicmp(modEntry.szModule, L"d3d11.dll") == 0) isD3D11 = true;
            if (_wcsicmp(modEntry.szModule, L"d3d12.dll") == 0) isD3D12 = true;
            if (_wcsicmp(modEntry.szModule, L"vulkan-1.dll") == 0) isVulkan = true;
            if (_wcsicmp(modEntry.szModule, L"opengl32.dll") == 0) isOpenGL = true;
        } while (Module32NextW(hSnapshot, &modEntry));
    }
    CloseHandle(hSnapshot);

    std::cout << "[*] Анализ архитектуры процесса ИИ:\n";
    if (isD3D12) std::cout << "    -> Графический API игры: DirectX 12\n";
    else if (isD3D11) std::cout << "    -> Графический API игры: DirectX 11\n";
    else if (isVulkan) std::cout << "    -> Графический API игры: Vulkan\n";
    else if (isOpenGL) std::cout << "    -> Графический API игры: OpenGL\n";
    else std::cout << "    -> Используется стандартный программный рендер Windows GDI\n";
}

// Создание DirectX устройства
winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice CreateDirect3DDevice() {
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

    void ProcessFrameToGPU(winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame const& frame) {
        auto d3dSurface = frame.Surface();

        winrt::com_ptr<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> interopAccess;
        d3dSurface.as(interopAccess);

        winrt::com_ptr<ID3D11Texture2D> d3dTexture;
        interopAccess->GetInterface(IID_PPV_ARGS(d3dTexture.put()));

        if (!d3dTexture) return;

        D3D11_TEXTURE2D_DESC desc;
        d3dTexture->GetDesc(&desc);

        // 1. Получаем аппаратный контекст устройства DirectX
        winrt::com_ptr<ID3D11Device> d3dDevice;
        d3dTexture->GetDevice(d3dDevice.put());
        winrt::com_ptr<ID3D11DeviceContext> context;
        d3dDevice->GetImmediateContext(context.put());

        // 2. Создаем временную Staging-текстуру, к памяти которой CPU и CUDA имеют легальный доступ
        D3D11_TEXTURE2D_DESC stagingDesc = desc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;

        winrt::com_ptr<ID3D11Texture2D> stagingTexture;
        HRESULT hr = d3dDevice->CreateTexture2D(&stagingDesc, nullptr, stagingTexture.put());
        if (FAILED(hr)) return;

        // Скоростное копирование внутри ресурсов D3D11
        context->CopyResource(stagingTexture.get(), d3dTexture.get());

        D3D11_MAPPED_SUBRESOURCE mapped;
        // Блокируем текстуру для безопасного чтения адресов
        if (SUCCEEDED(context->Map(stagingTexture.get(), 0, D3D11_MAP_READ, 0, &mapped))) {
            try {
                // 3. Создаем промежуточную CPU-матрицу OpenCV, которая смотрит на выровненные пиксели памяти
                // mapped.RowPitch — это аппаратный шаг строки (alignment stride) в DirectX, предотвращающий смещение адресов
                cv::Mat cpuFrame(desc.Height, desc.Width, CV_8UC4, mapped.pData, mapped.RowPitch);

                // 4. БЕЗОПАСНАЯ ЗАГРУЗКА В CUDA: Выделяем легальную память в VRAM и загружаем туда кадр
                cv::cuda::GpuMat gpuFrame;
                gpuFrame.upload(cpuFrame); // Теперь CUDA полностью управляет этим регионом памяти!

                // 5. ВЫЧИСЛЕНИЯ НА ТЕНЗОРНЫХ ЯДРАХ: Конвертируем формат
                cv::cuda::GpuMat gpuReadyForAI;
                cv::cuda::cvtColor(gpuFrame, gpuReadyForAI, cv::COLOR_BGRA2BGR);

                // Выгружаем результат обратно для рендеринга окна
                cv::Mat finalCpuFrame;
                gpuReadyForAI.download(finalCpuFrame);

                // Разблокируем ресурсы DirectX как можно быстрее
                context->Unmap(stagingTexture.get(), 0);

                // 6. Отрисовка живого окна без мерцаний и падений памяти
                if (!finalCpuFrame.empty()) {
                    cv::imshow("Живой поток ИИ (CUDA)", finalCpuFrame);
                    if (cv::waitKey(1) == 27) { // Кнопка ESC
                        Stop();
                    }
                }

            }
            catch (const cv::Exception& e) {
                context->Unmap(stagingTexture.get(), 0);
                std::cerr << "[-] Ошибка внутри вычислительного ядра CUDA: " << e.what() << "\n";
            }
        }
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
        auto device = CreateDirect3DDevice();
        if (!device) return false;

        m_framePool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
            device,
            winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            2,
            size
        );

        // Поток кадров теперь идет непрерывно
        m_framePool.FrameArrived([this](winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender, winrt::Windows::Foundation::IInspectable const&) {
            auto frame = sender.TryGetNextFrame();
            if (frame) {
                this->ProcessFrameToGPU(frame);
            }
            });

        m_captureSession = m_framePool.CreateCaptureSession(m_captureItem);
        m_captureSession.StartCapture();
        m_isCapturing = true;

        std::cout << "[+] Высокоскоростная трансляция в VRAM запущена.\n";
        std::cout << "[!] Чтобы закрыть трансляцию, нажмите клавишу ESC на окне видео потока.\n";
        return true;
    }

    void Stop() {
        if (!m_isCapturing) return;
        m_isCapturing = false;

        if (m_captureSession) { m_captureSession.Close(); m_captureSession = nullptr; }
        if (m_framePool) { m_framePool.Close(); m_framePool = nullptr; }
        m_captureItem = nullptr;
        cv::destroyAllWindows();
        std::cout << "[*] Трансляция остановлена. Ресурсы очищены.\n";
    }

    bool IsCapturing() const { return m_isCapturing; }
};

int main2() {
    std::setlocale(LC_ALL, "Russian");
    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);

    DirectXCapture capturer;

    // !!! ВПИШИТЕ СЮДА ИМЯ ОКНА ДЛЯ ТЕСТА ЖИВОГО ПОТОКА !!!
    // Отлично подойдет запущенная игра или окно браузера с видео (например, L"YouTube - Google Chrome")
    const wchar_t* targetWindow = L"dotAGE";

    if (capturer.Initialize(targetWindow)) {
        DetectGameGraphicsAPI(targetWindow);
        if (capturer.Start()) {
            // Держим программу запущенной, пока идет захват
            while (capturer.IsCapturing()) {
                Sleep(100);
            }
        }
    }
    return 0;
}
