#include "InterfaceScanner.h"
#include "DirectXCapture.h"
#include <opencv2/cudaarithm.hpp>
#include <opencv2/cudaimgproc.hpp>
#include <iostream>
#include <windows.h>

InterfaceScanner::InterfaceScanner(int minZoneSize) : m_minZoneSize(minZoneSize) {}

bool InterfaceScanner::RunAutoCalibration(DirectXCapture& capturer, AppProfile& profile, HWND gameHwnd) {
    std::cout << "\n[!] ЗАПУСК МНОГОСТУПЕНЧАТОГО ИИ-СКАНЕРА (12 ИТЕРАЦИЙ СДВИГА) [!]\n";

    AppProfile emptyTemp;
    if (!capturer.Start(emptyTemp)) return false;

    // 1. Стабилизация потока
    std::cout << "[*] Ожидание первого стабильного кадра...\n";
    cv::Mat frameBase;
    for (int i = 0; i < 15; ++i) {
        Sleep(100);
        auto check = capturer.GetLatestZones();
        frameBase = check["Full_Screen_Snapshot"];
        if (!frameBase.empty()) break;
    }
    if (frameBase.empty()) { capturer.Stop(); return false; }

    cv::cuda::GpuMat gpuBase, grayBase;
    gpuBase.upload(frameBase);
    cv::cuda::cvtColor(gpuBase, grayBase, cv::COLOR_BGRA2GRAY);

    // Финальная маска: изначально всё белое (255)
    cv::cuda::GpuMat gpuFinalStaticMask(grayBase.size(), CV_8UC1, cv::Scalar(255));

    // Направления движения
    struct Direction { DWORD key; std::string name; };
    std::vector<Direction> dirs = {
        { 0x41, "ВЛЕВО (A)" },
        { 0x53, "ВНИЗ (S)" },
        { 0x44, "ВПРАВО (D)" },
        { 0x57, "ВВЕРХ (W)" }
    };

    SetForegroundWindow(gameHwnd);
    Sleep(200);

    int totalScanCounter = 0;

    // ОБХОД ПО КРЕСУ: 4 направления
    for (const auto& dir : dirs) {
        // Каждое направление разбиваем на 3 отдельных микросдвига по вашему правилу
        for (int subStep = 1; subStep <= 3; ++subStep) {
            totalScanCounter++;
            std::cout << "  [Сканирование " << totalScanCounter << "/12] Сдвиг "
                << dir.name << ", микрошаг " << subStep << "...\n";

            // Симулируем короткий физический толчок камеры
            for (int i = 0; i < 4; ++i) { // Короткое зажатие (4 импульса)
                PostMessageW(gameHwnd, WM_KEYDOWN, dir.key, 0);
                Sleep(20);
            }
            PostMessageW(gameHwnd, WM_KEYUP, dir.key, 0xC0000001);

            // Даем Unity-движку честное время прорисовать изменения пикселей
            Sleep(350);

            // Перехватываем промежуточный кадр из VRAM
            auto zonesCurrent = capturer.GetLatestZones();
            cv::Mat cpuFrameCurrent = zonesCurrent["Full_Screen_Snapshot"];
            if (cpuFrameCurrent.empty()) {
                std::cout << "  [-] Пропуск итерации: пустой буфер кадра.\n";
                continue;
            }

            // Вычисляем дифференциал на GPU Blackwell
            cv::cuda::GpuMat gpuCurrent, grayCurrent, gpuDiff, gpuThresh;
            gpuCurrent.upload(cpuFrameCurrent);
            cv::cuda::cvtColor(gpuCurrent, grayCurrent, cv::COLOR_BGRA2GRAY);

            cv::cuda::absdiff(grayBase, grayCurrent, gpuDiff);
            cv::cuda::threshold(gpuDiff, gpuThresh, 12, 255, cv::THRESH_BINARY); // Снизили порог чувствительности до 12

            // Инвертируем маску (неподвижное = 255)
            cv::cuda::GpuMat gpuStepStaticMask;
            cv::cuda::bitwise_not(gpuThresh, gpuStepStaticMask);

            // ЖЕСТКОЕ ПЕРЕМНОЖЕНИЕ МАСОК (AND)
            cv::cuda::bitwise_and(gpuFinalStaticMask, gpuStepStaticMask, gpuFinalStaticMask);

            // Текущий кадр становится базовым для следующего микрошага
            grayBase = grayCurrent.clone();
        }
    }

    capturer.Stop(); // Сбор данных завершен

    // Выгружаем накопленную 12-кратную матрицу в ОЗУ
    cv::Mat cpuFinalStaticMask;
    gpuFinalStaticMask.download(cpuFinalStaticMask);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(cpuFinalStaticMask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    int staticZoneCounter = 0;
    cv::Mat visualCanvas = frameBase.clone();

    for (const auto& contour : contours) {
        cv::Rect boundingRect = cv::boundingRect(contour);

        // Усиленный геометрический фильтр для чистки остаточного шума
        if (boundingRect.width >= 30 && boundingRect.height >= 20) {
            if (boundingRect.width > 1900 || boundingRect.height > 1050) continue;

            staticZoneCounter++;
            std::string zoneName = "Static_Interface_Zone_" + std::to_string(staticZoneCounter);
            profile.SetZone(zoneName, ZoneType::ResourceDigits, boundingRect);

            // Отрисовка рамок (непрозрачный Alpha-канал 255)
            cv::rectangle(visualCanvas, boundingRect, cv::Scalar(0, 0, 255, 255), 2); // Красный
            cv::putText(visualCanvas, zoneName, cv::Point(boundingRect.x, boundingRect.y - 5),
                cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(255, 0, 0, 255), 1); // Синий
        }
    }

    cv::imwrite("interface_scan_result.png", visualCanvas);
    std::cout << "[***] ДИАГНОСТИКА: Итоговая 12-ступенчатая карта сохранена в 'interface_scan_result.png'\n";

    std::string filename = "dotage_interface.prof";
    return profile.SaveToFile(filename);
}


// Внутренний приватный метод вычисления разметки на видеокарте Blackwell
void InterfaceScanner::ProcessAndFindZones(const cv::Mat& frameBefore, const cv::Mat& frameAfter, AppProfile& profile) {
    cv::cuda::GpuMat gpuFrame1, gpuFrame2;
    gpuFrame1.upload(frameBefore);
    gpuFrame2.upload(frameAfter);

    cv::cuda::GpuMat gray1, gpuGray2;
    cv::cuda::cvtColor(gpuFrame1, gray1, cv::COLOR_BGRA2GRAY);
    cv::cuda::cvtColor(gpuFrame2, gpuGray2, cv::COLOR_BGRA2GRAY);

    cv::cuda::GpuMat gpuDiff;
    cv::cuda::absdiff(gray1, gpuGray2, gpuDiff);

    cv::cuda::GpuMat gpuThreshold;
    cv::cuda::threshold(gpuDiff, gpuThreshold, 15, 255, cv::THRESH_BINARY);

    cv::Mat cpuMask;
    gpuThreshold.download(cpuMask);

    cv::Mat cpuStaticMask;
    cv::bitwise_not(cpuMask, cpuStaticMask);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(cpuStaticMask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    int staticZoneCounter = 0;
    for (const auto& contour : contours) {
        cv::Rect boundingRect = cv::boundingRect(contour);

        if (boundingRect.width >= m_minZoneSize && boundingRect.height >= m_minZoneSize) {
            if (boundingRect.width > 1910 && boundingRect.height > 1070) continue;

            staticZoneCounter++;
            std::string zoneName = "Static_Interface_Zone_" + std::to_string(staticZoneCounter);

            // Наполняем профиль
            profile.SetZone(zoneName, ZoneType::ResourceDigits, boundingRect);
        }
    }
}
