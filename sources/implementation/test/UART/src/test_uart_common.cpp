#include "test_uart_common.hpp"

bool fileExistsAndNotEmpty(const std::string& filename)
{
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    return file && file.tellg() > 0;
}


std::map<std::string, std::string> loadResponses(const std::string& filename)
{
    std::map<std::string, std::string> responses;
    std::ifstream file(filename);
    std::string line;

    while (std::getline(file, line)) {
        auto delimiterPos = line.find(':');
        if (delimiterPos != std::string::npos) {
            std::string key = line.substr(0, delimiterPos);
            std::string value = line.substr(delimiterPos + 1);
            responses[key] = value;
        }
    }

    return responses;
}
