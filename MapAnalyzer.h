#pragma once
#include <opencv2/opencv.hpp>
#include <opencv2/core/cuda.hpp>
#include "AppProfile.h"

struct MapResources {
    float treePercent = 0.0f;  // Процент леса на старте
    float waterPercent = 0.0f; // Процент воды
    float stonePercent = 0.0f; // Процент камня
};

class MapAnalyzer {
public:
    MapAnalyzer();

    // Функция принимает полный BGR-кадр из VRAM, маску интерфейса 
    // и вычисляет реальное количество ресурсов на игровом поле
    MapResources AnalyzeMap(const cv::cuda::GpuMat& gpuBGR, const AppProfile& profile);
};
