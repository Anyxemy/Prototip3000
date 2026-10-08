#include "OllamaClient.h"
#include <windows.h>
#include <wininet.h>
#include <iostream>
#include <sstream>

#pragma comment(lib, "wininet.lib")

OllamaClient::OllamaClient(const std::string& modelName)
    : m_serverUrl("http://localhost:11434"), m_modelName(modelName) {
}

std::string OllamaClient::AskModel(const std::string& prompt) {
    // Формируем чистый JSON пакет без потоковой передачи (stream: false)
    std::string jsonPayload = "{\"model\": \"" + m_modelName + "\", \"prompt\": \"" + prompt + "\", \"stream\": false}";
    std::string responseData = "";

    HINTERNET hInternet = InternetOpenA("OllamaC++Client", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) return "ERROR_NET_INIT";

    // Стабильные 25 секунд таймаута на случай холодного старта GPU Blackwell
    DWORD timeout = 25000;
    InternetSetOptionA(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    HINTERNET hConnect = InternetConnectA(hInternet, "127.0.0.1", 11434, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        return "ERROR_NET_CONNECT";
    }

    const char* acceptTypes[] = { "application/json", NULL };
    HINTERNET hRequest = HttpOpenRequestA(hConnect, "POST", "/api/generate", NULL, NULL, acceptTypes, INTERNET_FLAG_RELOAD, 0);

    if (hRequest) {
        std::string headers = "Content-Type: application/json\r\n";

        BOOL bSend = HttpSendRequestA(
            hRequest,
            headers.c_str(), (DWORD)headers.length(),
            (LPVOID)jsonPayload.c_str(), (DWORD)jsonPayload.length()
        );

        if (bSend) {
            char buffer[1024];
            DWORD bytesRead = 0;
            while (InternetReadFile(hRequest, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
                buffer[bytesRead] = '\0';
                responseData.append(buffer, bytesRead);
            }
        }
        InternetCloseHandle(hRequest);
    }

    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);

    // ВЫСОКОСКОРОСТНОЙ АВТОНОМНЫЙ ПАРСЕР JSON (Вырезаем поле "response")
    std::string searchKey = "\"response\":\"";
    size_t startPos = responseData.find(searchKey);
    if (startPos != std::string::npos) {
        startPos += searchKey.length();
        size_t endPos = responseData.find("\"", startPos);
        if (endPos != std::string::npos) {
            // Возвращаем чистый текстовый ответ ИИ-судьи
            return responseData.substr(startPos, endPos - startPos);
        }
    }

    return responseData; // Если парсинг не удался, вернем сырой лог для диагностики
}
