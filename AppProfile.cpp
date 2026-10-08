#include "AppProfile.h"
#include <fstream>
#include <iostream>
#include <sstream>

// Конструктор теперь абсолютно чист и не содержит мусорных координат
AppProfile::AppProfile(const std::string& profileName) : m_profileName(profileName) {
    std::cout << "[+] Создан пустой профиль приложения: " << m_profileName << "\n";
}

void AppProfile::SetZone(const std::string& name, ZoneType type, cv::Rect rect, const std::vector<cv::Point>& poly) {
    TargetZone zone = { name, type, rect, poly };
    m_zones[name] = zone;
}

TargetZone AppProfile::GetZone(const std::string& name) {
    return m_zones[name];
}

bool AppProfile::HasZone(const std::string& name) const {
    return m_zones.find(name) != m_zones.end();
}

bool AppProfile::SaveToFile(const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) return false;

    for (const auto& [name, zone] : m_zones) {
        file << name << "|"
            << static_cast<int>(zone.type) << "|"
            << zone.rect.x << "|"
            << zone.rect.y << "|"
            << zone.rect.width << "|"
            << zone.rect.height << "\n";
    }

    std::cout << "[*] Физически записано " << m_zones.size() << " зон в файл: " << filename << "\n";
    return true;
}

bool AppProfile::LoadFromFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) return false;

    m_zones.clear();
    std::string line;
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string name, typeStr, xStr, yStr, wStr, hStr;

        if (std::getline(ss, name, '|') &&
            std::getline(ss, typeStr, '|') &&
            std::getline(ss, xStr, '|') &&
            std::getline(ss, yStr, '|') &&
            std::getline(ss, wStr, '|') &&
            std::getline(ss, hStr, '|')) {

            cv::Rect r(std::stoi(xStr), std::stoi(yStr), std::stoi(wStr), std::stoi(hStr));
            ZoneType t = static_cast<ZoneType>(std::stoi(typeStr));
            SetZone(name, t, r);
        }
    }
    std::cout << "[+] Профиль успешно восстановил " << m_zones.size() << " зон из файла.\n";
    return true;
}
