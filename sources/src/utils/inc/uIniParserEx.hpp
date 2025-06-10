#ifndef UINI_PARSER_EX_HPP
#define UINI_PARSER_EX_HPP

#include <string>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <regex>

class IniParserEx
{
public:

    bool load(const std::string& filename)
    {
        std::ifstream file(filename);
        if (!file.is_open()) {
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

    std::string getValue(const std::string& section, const std::string& key, const std::string& defaultValue = "", int depth = 5) const
    {
        if (depth <= 0) {
            return defaultValue; // prevent infinite recursion
        }

        auto secIt = iniData.find(section);
        if (secIt != iniData.end()) {
            auto keyIt = secIt->second.find(key);
            if (keyIt != secIt->second.end()) {
                std::string value = keyIt->second;
                std::regex varRegex(R"(\$\{([^}]+)\})");
                std::smatch match;
                while (std::regex_search(value, match, varRegex)) {
                    std::string varName = match[1].str();
                    std::string varValue;
                    auto colonPos = varName.find(':');
                    if (colonPos != std::string::npos) {
                        std::string varSection = varName.substr(0, colonPos);
                        std::string varKey = varName.substr(colonPos + 1);
                        varValue = getValue(varSection, varKey, "", depth - 1);
                    } else {
                        varValue = getValue(section, varName, "", depth - 1);
                    }
                    value.replace(match.position(0), match.length(0), varValue);
                }
                return value;
            }
        }
        return defaultValue;
    }

    bool getSection(const std::string& section, std::unordered_map<std::string, std::string>& outMap) const
    {
        auto it = iniData.find(section);
        if (it != iniData.end()) {
            outMap = it->second;
            return true;
        }
        outMap.clear();
        return false;
    }

    bool getResolvedSection(const std::string& section, std::unordered_map<std::string, std::string>& outMap, int depth = 5) const
    {
        auto it = iniData.find(section);
        if (it != iniData.end()) {
            outMap.clear();
            for (const auto& kv : it->second) {
                outMap[kv.first] = getValue(section, kv.first, "", depth);
            }
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

    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> iniData;

    static std::string trim(const std::string& str)
    {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, last - first + 1);
    }
};

#endif // UINI_PARSER_EX_HPP
