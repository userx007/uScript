#ifndef UINI_PARSER_H
#define UINI_PARSER_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <string>

class IniParser
{
public:
    bool load(const std::string& filename)
    {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Error: Could not open file " << filename << std::endl;
            return false;
        }

        std::string line, currentSection;
        while (std::getline(file, line)) {
            line = trim(line); // Remove leading/trailing spaces

            // Skip comments and empty lines
            if (line.empty() || line[0] == ';' || line[0] == '#') {
                continue;
            }

            // Detect section headers [Common], [Script], [Plugin1] ... [PluginN]
            if (line.front() == '[' && line.back() == ']') {
                currentSection = line.substr(1, line.size() - 2);
            } else {
                // Parse key-value pairs
                size_t delimiterPos = line.find('=');
                if (delimiterPos != std::string::npos) {
                    std::string key = trim(line.substr(0, delimiterPos));
                    std::string value = trim(line.substr(delimiterPos + 1));
                    iniData[currentSection][key] = value;
                }
            }
        }
        file.close();
        return true;
    }

    std::string getValue(const std::string& section, const std::string& key, const std::string& defaultValue = "") const
    {
        auto secIt = iniData.find(section);
        if (secIt != iniData.end()) {
            auto keyIt = secIt->second.find(key);
            if (keyIt != secIt->second.end()) {
                return keyIt->second;
            }
        }
        return defaultValue;
    }

    bool getSection(const std::string& section, std::map<std::string, std::string>& outMap) const
    {
        auto secIt = iniData.find(section);
        if (secIt != iniData.end()) {
            outMap = secIt->second;
            return true;
        }
        outMap.clear();
        return false;
    }

    bool sectionExists(const std::string& section) const
    {
        return iniData.find(section) != iniData.end();
    }

private:
    std::map<std::string, std::map<std::string, std::string>> iniData;

    static std::string trim(const std::string& str)
    {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, last - first + 1);
    }
};

#endif // UINI_PARSER_H
