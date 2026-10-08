#pragma once
#include <string>
#include <vector>
#include <map>
#include <opencv2/core.hpp>

// Перечисление типов зон, которые ИИ должен обрабатывать по-разному
enum class ZoneType {
    ResourceDigits,   // Зона текстовых цифр (дерево, золото)
    StatusBar,        // Шкала прогресса или угрозы
    ClickableButton,  // Интерактивная кнопка меню
    GameField         // Само игровое поле для поиска спрайтов
};

// Структура одной зоны интерфейса
struct TargetZone {
    std::string name;       // Уникальное имя (например, "Gold_Count")
    ZoneType type;          // Тип обработки
    cv::Rect rect;          // Прямоугольные координаты (X, Y, W, H)
    std::vector<cv::Point> polygon; // Продвинутая геометрия: точки многоугольника
};

class AppProfile {
private:
    std::string m_profileName;
    std::map<std::string, TargetZone> m_zones;

public:
    AppProfile(const std::string& profileName = "default");

    // Добавить или изменить зону
    void SetZone(const std::string& name, ZoneType type, cv::Rect rect, const std::vector<cv::Point>& poly = {});

    // Получить зону по имени
    TargetZone GetZone(const std::string& name);
    bool HasZone(const std::string& name) const;

    // Функции сериализации (сохранение/загрузка из файла на диске)
    bool SaveToFile(const std::string& filename);
    bool LoadFromFile(const std::string& filename);

    // Получить все зоны для перебора в цикле
    const std::map<std::string, TargetZone>& GetAllZones() const { return m_zones; }
};
