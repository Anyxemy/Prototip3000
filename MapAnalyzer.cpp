#include "MapAnalyzer.h"
#include <opencv2/cudaarithm.hpp>
#include <opencv2/cudaimgproc.hpp>
#include <iostream>

MapAnalyzer::MapAnalyzer() {}

MapResources MapAnalyzer::AnalyzeMap(const cv::cuda::GpuMat& gpuBGR, const AppProfile& profile) {
    MapResources res;
    if (gpuBGR.empty()) return res;

    // 1. Создаем маску игрового поля, где изначально всё разрешено (255)
    cv::cuda::GpuMat gpuMapMask(gpuBGR.size(), CV_8UC1, cv::Scalar(255));

    // 2. ИСКЛЮЧАЕМ ИНТЕРФЕЙС: закрашиваем черным (0) все 6 зон из файла данных
    for (const auto& [name, zone] : profile.GetAllZones()) {
        // Чтобы не тратить время на перерисовку, просто создаем локальный регион и обнуляем его
        if (zone.rect.x + zone.rect.width <= gpuBGR.cols && zone.rect.y + zone.rect.height <= gpuBGR.rows) {
            cv::cuda::GpuMat subMask(gpuMapMask, zone.rect);
            subMask.setTo(cv::Scalar(0)); // Эта зона интерфейса больше не учитывается ИИ!
        }
    }

    // Переводим кадр в формат HSV на GPU (в нем гораздо точнее настраивать диапазоны цветов)
    cv::cuda::GpuMat gpuHSV;
    cv::cuda::cvtColor(gpuBGR, gpuHSV, cv::COLOR_BGR2HSV);

    // 3. НАСТРОЙКА ЦВЕТОВЫХ ДИАПАЗОНОВ ДЛЯ ПИКСЕЛЬ-АРТА dotAGE
    // (Значения HSV можно будет слегка подкорректировать под точные оттенки игры)
    cv::Scalar lowGreen(35, 40, 40);   cv::Scalar highGreen(85, 255, 255);  // Лес
    cv::Scalar lowBlue(90, 50, 50);    cv::Scalar highBlue(130, 255, 255);  // Вода

    cv::cuda::GpuMat maskGreen, maskBlue;

    // Мгновенный поиск пикселей нужного цвета силами CUDA
    cv::cuda::inRange(gpuHSV, lowGreen, highGreen, maskGreen);
    cv::cuda::inRange(gpuHSV, lowBlue, highBlue, maskBlue);

    // Накладываем маску исключения интерфейса, чтобы пиксели плашек ресурсов не считались картой
    cv::cuda::GpuMat finalGreen, finalBlue;
    cv::cuda::bitwise_and(maskGreen, gpuMapMask, finalGreen);
    cv::cuda::bitwise_and(maskBlue, gpuMapMask, finalBlue);

    // Подсчитываем количество светящихся пикселей ресурсов на видеокарте
    int greenPixels = cv::cuda::countNonZero(finalGreen);
    int bluePixels = cv::cuda::countNonZero(finalBlue);

    // Вычисляем общую площадь чистого игрового поля (без учета 6 зон интерфейса)
    int totalPlayablePixels = cv::cuda::countNonZero(gpuMapMask);

    if (totalPlayablePixels > 0) {
        res.treePercent = (static_cast<float>(greenPixels) / totalPlayablePixels) * 100.0f;
        res.waterPercent = (static_cast<float>(bluePixels) / totalPlayablePixels) * 100.0f;
    }

    return res;
}
