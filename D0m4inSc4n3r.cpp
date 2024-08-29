#include <iostream>
#include <vector>
#include <string>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <mutex>
#include <unordered_set>
#include <regex>

// Функция для записи ответа cURL в строку
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// Функция для выполнения запроса и получения ответа
std::string performRequest(const std::string& url) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            std::cerr << "Не удалось установить соединение: " << curl_easy_strerror(res) << std::endl;
            std::cerr << "URL: " << url << std::endl;  // Выводим URL для проверки
            readBuffer.clear(); // Очищаем буфер в случае ошибки
        }

        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
    return readBuffer;
}

// Преобразование строки в список уникальных доменов, поддоменов и субдоменов
std::vector<std::string> extractDomains(const std::string& content) {
    std::unordered_set<std::string> uniqueDomains;
    std::regex domainRegex(R"((?:[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?\.)+[a-zA-Z]{2,})");
    auto wordsBegin = std::sregex_iterator(content.begin(), content.end(), domainRegex);
    auto wordsEnd = std::sregex_iterator();

    for (std::sregex_iterator i = wordsBegin; i != wordsEnd; ++i) {
        std::smatch match = *i;
        std::string domain = match.str();
        uniqueDomains.insert(domain);
    }

    return std::vector<std::string>(uniqueDomains.begin(), uniqueDomains.end());
}

// Пример функции для поиска связанных доменов
std::vector<std::string> findRelatedDomains(const std::string& domain) {
    std::unordered_set<std::string> uniqueDomains;  // Используем set для удаления дубликатов
    std::string apiUrl = "https://crt.sh/?q=" + domain + "&output=json"; // Замените на реальный API
    std::string response = performRequest(apiUrl);

    if (response.empty()) {
        // Если ответ пустой, значит соединение не удалось
        return {};
    }

    // Вывод для отладки
    // std::cout << "Response: " << response << std::endl;

    // Парсинг JSON-ответа
    try {
        auto jsonResponse = nlohmann::json::parse(response);
        for (const auto& item : jsonResponse) {
            if (item.contains("name_value")) {
                std::string nameValue = item["name_value"].get<std::string>();
                auto domains = extractDomains(nameValue);
                uniqueDomains.insert(domains.begin(), domains.end());
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Ошибка при разборе JSON-ответа: " << e.what() << std::endl;
    }

    // Переносим уникальные домены в вектор
    return std::vector<std::string>(uniqueDomains.begin(), uniqueDomains.end());
}

// Многопоточная функция для поиска связанных доменов
void findDomainsInThread(const std::string& domain, std::vector<std::string>& result, std::mutex& resultMutex) {
    auto domains = findRelatedDomains(domain);
    std::lock_guard<std::mutex> guard(resultMutex);
    result.insert(result.end(), domains.begin(), domains.end());
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <domain>" << std::endl;
        return 1;
    }

    std::string domain = argv[1];
    std::vector<std::string> allRelatedDomains;
    std::mutex resultMutex;

    // Запуск нескольких потоков для поиска
    const int numThreads = 4; // Количество потоков для параллельного поиска
    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back(findDomainsInThread, domain, std::ref(allRelatedDomains), std::ref(resultMutex));
    }

    // Ожидание завершения всех потоков
    for (auto& th : threads) {
        if (th.joinable()) {
            th.join();
        }
    }

    // Удаление дубликатов (если это необходимо)
    std::unordered_set<std::string> uniqueDomains(allRelatedDomains.begin(), allRelatedDomains.end());
    allRelatedDomains.assign(uniqueDomains.begin(), uniqueDomains.end());

    std::cout << "Связанные домены для " << domain << ":\n";
    if (allRelatedDomains.empty()) {
        std::cout << "Не удалось найти связанные домены.\n";
    } else {
        for (const std::string& d : allRelatedDomains) {
            std::cout << d << std::endl;
        }
    }

    return 0;
}
