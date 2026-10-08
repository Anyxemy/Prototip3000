#pragma once
#include <windows.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <opencv2/opencv.hpp>
#include <opencv2/core/cuda.hpp>
#include <map>
#include <mutex>
#include "AppProfile.h"

class DirectXCapture {
private:
    HWND m_hwnd = nullptr;
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_captureItem{ nullptr };
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool m_framePool{ nullptr };
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession m_captureSession{ nullptr };
    bool m_isCapturing = false;

    const AppProfile* m_currentProfile = nullptr;

    // Хранилище для вырезанных картинок зон и мьютекс для безопасного обмена между потоками
    std::map<std::string, cv::Mat> m_outputFrames;
    std::mutex m_frameMutex;

    void ProcessFrameToGPU(winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame const& frame);

public:
    DirectXCapture();
    ~DirectXCapture();

    bool Initialize(const wchar_t* windowTitle);
    bool Start(const AppProfile& profile);
    void Stop();
    bool IsCapturing() const;

    // Новый метод: позволяет главному потоку безопасно забрать картинки зон
    std::map<std::string, cv::Mat> GetLatestZones();
};
