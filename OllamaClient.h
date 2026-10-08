#pragma once // Защита от повторного включения файла при компиляции
#include <string>

class OllamaClient {
private:
    std::string m_serverUrl;
    std::string m_modelName;

public:
    // Конструктор: задаем адрес локального сервера и имя модели
    OllamaClient(const std::string& modelName = "qwen2.5-coder:7b");

    // Главная функция: отправляет текстовый запрос и возвращает ответ ИИ
    std::string AskModel(const std::string& prompt);
};
