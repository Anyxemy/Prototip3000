#define NOMINMAX // Защита от макросов Windows min/max
#include <iostream>
#include <fstream>
#include "AppProfile.h"
#include "DirectXCapture.h"
#include "InterfaceScanner.h"
#include "OllamaClient.h"

// Вспомогательная простая функция проверки: существует ли файл на диске?
bool IsProfileFileExists(const std::string& filename) {
    std::ifstream file(filename);
    return file.good();
}

int main() {
    SetConsoleCP(65001); SetConsoleOutputCP(65001);
    std::cout << "==================================================\n";
    std::cout << "[*] Умный ИИ-Комплекс Автоматизации dotAGE Запущен\n";
    std::cout << "==================================================\n\n";

    const wchar_t* gameWindowName = L"dotAGE";
    std::string profileConfigName = "dotage_interface.prof";

    DirectXCapture capturer;
    if (!capturer.Initialize(gameWindowName)) {
        std::wcout << L"[-] КРИТИЧЕСКАЯ ОШИБКА: Окно игры '" << gameWindowName << L"' не найдено!\n";
        system("pause"); return -1;
    }

    AppProfile currentProfile("dotAGE_Survival");

    // УМНАЯ КОРНЕВАЯ ЛОГИКА ЗАПУСКА:
    if (!IsProfileFileExists(profileConfigName)) {
        // Файла нет — запускаем автокалибровку интерфейса "1, 2, 1, 2" силами сканера
        InterfaceScanner autoScanner(40);

        // Получаем HWND из нашего класса зрения для фоновой отправки PostMessage кнопок
        HWND gameHwnd = FindWindowW(NULL, gameWindowName);

        if (!autoScanner.RunAutoCalibration(capturer, currentProfile, gameHwnd)) {
            std::cout << "[-] Не удалось выполнить автоматическое сканирование. Выход.\n";
            return -1;
        }
    }
    else {
        // ТИХИЙ СТАРТ: Файл найден, мгновенно считываем сохраненную геометрию интерфейса
        std::cout << "[+] Обнаружена сохраненная конфигурация интерфейса. Выполняю тихий старт...\n";
        currentProfile.LoadFromFile(profileConfigName);
    }

    // ТЕСТ ЛОКАЛЬНОГО ИИ С УВЕЛИЧЕННЫМ ТАЙМАУТОМ
    OllamaClient ai("qwen2.5-coder:7b");
    std::cout << "[+] Проверка связи с Ollama GPU вычислителем...\n";
    std::string response = ai.AskModel("Hi! Output exactly one word: 'READY'");
    std::cout << "[Сервер ИИ подключен]: " << response << "\n\n";

    // ПЕРЕХОД К ПОСТОЯННОМУ МОНИТОРИНГУ КАРТЫ
    if (capturer.Start(currentProfile)) {
        std::cout << "[+] ИИ-комплекс успешно ведет скрытый анализ процедурной карты в VRAM.\n";
        std::cout << "[!] Чтобы полностью остановить работу комплекса, нажмите ENTER в этой консоли...\n";

        std::cin.get(); // Держим консоль, пока вы не решите прервать сессию
        capturer.Stop();
    }

    std::cout << "[*] Работа комплекса полностью завершена. Всего доброго!\n";
    return 0;
}
