#pragma once
#include <Windows.h>
#include <opencv2/opencv.hpp>
#include <opencv2/core/cuda.hpp>
#include <string>
#include "AppProfile.h"

// Вперёд-объявление класса захвата, чтобы не делать цикличных инклудов
class DirectXCapture;

class InterfaceScanner {
private:
    int m_minZoneSize;

    // Внутренние низкоуровневые методы, скрытые от внешнего пользователя
    void ProcessAndFindZones(const cv::Mat& frameBefore, const cv::Mat& frameAfter, AppProfile& profile);

public:
    InterfaceScanner(int minZoneSize = 40);

    // Главная и единственная функция, которую будет вызывать внешняя система.
    // Она полностью инкапсулирует в себе весь процесс калибровки "1, 2, 1, 2"
    bool RunAutoCalibration(DirectXCapture& capturer, AppProfile& profile, HWND gameHwnd);
};
