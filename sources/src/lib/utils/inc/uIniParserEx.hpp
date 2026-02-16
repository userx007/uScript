#ifndef UINI_PARSER_EX_HPP
#define UINI_PARSER_EX_HPP

#include <string>
#include <string_view>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <optional>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cctype>

/**
 * @brief Enhanced INI file parser with variable interpolation support.
 * 
 * Features:
 * - Section-based configuration
 * - Variable interpolation: ${key} or ${section:key}
 * - Comment support (# and ;)
 * - Whitespace handling
 * - Circular reference detection
 * - Zero-copy string operations where possible
 */
class IniParserEx
{
public:
    using KeyValueMap = std::unordered_map<std::string, std::string>;
    using SectionMap = std::unordered_map<std::string, KeyValueMap>;

    /**
     * @brief Default constructor
     */
    IniParserEx() = default;

    /**
     * @brief Constructor that loads from file
     * @param filename Path to INI file
     * @throws std::runtime_error if file cannot be loaded
     */
    explicit IniParserEx(const std::string& filename)
    {
        if (!load(filename)) {
            throw std::runtime_error("Failed to load INI file: " + filename);
        }
    }

    /**
     * @brief Load INI file from disk
     * @param filename Path to INI file
     * @return true if successful, false otherwise
     */
    [[nodiscard]] bool load(const std::string& filename)
    {
        std::ifstream file(filename, std::ios::in);
        if (!file.is_open()) {
            return false;
        }

        return loadFromStream(file);
    }

    /**
     * @brief Load INI data from a stream
     * @param stream Input stream containing INI data
     * @return true if successful, false otherwise
     */
    [[nodiscard]] bool loadFromStream(std::istream& stream)
    {
        iniData.clear();
        std::string line;
        std::string currentSection;

        while (std::getline(stream, line)) {
            std::string_view lineView = trim(line);

            // Skip comments and empty lines
            if (lineView.empty() || lineView[0] == ';' || lineView[0] == '#') {
                continue;
            }

            // Detect section headers [Section]
            if (lineView.front() == '[' && lineView.back() == ']') {
                currentSection = std::string(lineView.substr(1, lineView.size() - 2));
                // Ensure section exists in map
                iniData[currentSection];
            } else {
                // Parse key-value pairs
                size_t delimiterPos = lineView.find('=');
                if (delimiterPos != std::string_view::npos) {
                    std::string_view keyView = trim(lineView.substr(0, delimiterPos));
                    std::string_view valueView = trim(lineView.substr(delimiterPos + 1));
                    
                    if (!keyView.empty()) {
                        iniData[currentSection][std::string(keyView)] = std::string(valueView);
                    }
                }
            }
        }

        return true;
    }

    /**
     * @brief Load INI data from a string
     * @param content String containing INI data
     * @return true if successful, false otherwise
     */
    [[nodiscard]] bool loadFromString(const std::string& content)
    {
        std::istringstream stream(content);
        return loadFromStream(stream);
    }

    /**
     * @brief Save INI data to file
     * @param filename Path to output file
     * @return true if successful, false otherwise
     */
    [[nodiscard]] bool save(const std::string& filename) const
    {
        std::ofstream file(filename, std::ios::out | std::ios::trunc);
        if (!file.is_open()) {
            return false;
        }

        return saveToStream(file);
    }

    /**
     * @brief Save INI data to stream
     * @param stream Output stream
     * @return true if successful, false otherwise
     */
    [[nodiscard]] bool saveToStream(std::ostream& stream) const
    {
        for (const auto& [section, kvMap] : iniData) {
            if (!section.empty()) {
                stream << '[' << section << "]\n";
            }
            
            for (const auto& [key, value] : kvMap) {
                stream << key << '=' << value << '\n';
            }
            
            stream << '\n'; // Blank line between sections
        }

        return stream.good();
    }

    /**
     * @brief Get a value with variable interpolation
     * @param section Section name
     * @param key Key name
     * @param defaultValue Default value if key not found
     * @param maxDepth Maximum recursion depth for variable resolution
     * @return Resolved value or default
     */
    [[nodiscard]] std::string getValue(const std::string& section, const std::string& key, 
                                       const std::string& defaultValue = "", int maxDepth = 10) const
    {
        if (maxDepth <= 0) {
            return defaultValue; // Prevent infinite recursion
        }

        auto secIt = iniData.find(section);
        if (secIt == iniData.end()) {
            return defaultValue;
        }

        auto keyIt = secIt->second.find(key);
        if (keyIt == secIt->second.end()) {
            return defaultValue;
        }

        return resolveVariables(secIt->second.at(key), section, maxDepth);
    }

    /**
     * @brief Get a value as std::optional (modern API)
     * @param section Section name
     * @param key Key name
     * @param resolve Whether to resolve variables
     * @return Optional containing value if found
     */
    [[nodiscard]] std::optional<std::string> getValueOpt(const std::string& section, 
                                                          const std::string& key, 
                                                          bool resolve = true) const
    {
        auto secIt = iniData.find(section);
        if (secIt == iniData.end()) {
            return std::nullopt;
        }

        auto keyIt = secIt->second.find(key);
        if (keyIt == secIt->second.end()) {
            return std::nullopt;
        }

        if (resolve) {
            return resolveVariables(keyIt->second, section, 10);
        }
        return keyIt->second;
    }

    /**
     * @brief Get raw (unresolved) value
     * @param section Section name
     * @param key Key name
     * @return Raw value without variable interpolation
     */
    [[nodiscard]] std::optional<std::string> getRawValue(const std::string& section, 
                                                          const std::string& key) const
    {
        return getValueOpt(section, key, false);
    }

    /**
     * @brief Set a value
     * @param section Section name
     * @param key Key name
     * @param value Value to set
     */
    void setValue(const std::string& section, const std::string& key, const std::string& value)
    {
        iniData[section][key] = value;
    }

    /**
     * @brief Get entire section (unresolved)
     * @param section Section name
     * @param outMap Output map to populate
     * @return true if section exists, false otherwise
     */
    [[nodiscard]] bool getSection(const std::string& section, KeyValueMap& outMap) const
    {
        auto it = iniData.find(section);
        if (it != iniData.end()) {
            outMap = it->second;
            return true;
        }
        outMap.clear();
        return false;
    }

    /**
     * @brief Get entire section with resolved variables
     * @param section Section name
     * @param outMap Output map to populate
     * @param maxDepth Maximum recursion depth
     * @return true if section exists, false otherwise
     */
    [[nodiscard]] bool getResolvedSection(const std::string& section, KeyValueMap& outMap, 
                                          int maxDepth = 10) const
    {
        auto it = iniData.find(section);
        if (it != iniData.end()) {
            outMap.clear();
            outMap.reserve(it->second.size());
            
            for (const auto& [key, value] : it->second) {
                outMap[key] = resolveVariables(value, section, maxDepth);
            }
            return true;
        }
        outMap.clear();
        return false;
    }

    /**
     * @brief Check if section exists
     * @param section Section name
     * @return true if section exists
     */
    [[nodiscard]] bool sectionExists(const std::string& section) const noexcept
    {
        return iniData.find(section) != iniData.end();
    }

    /**
     * @brief Check if key exists in section
     * @param section Section name
     * @param key Key name
     * @return true if key exists
     */
    [[nodiscard]] bool keyExists(const std::string& section, const std::string& key) const noexcept
    {
        auto secIt = iniData.find(section);
        if (secIt == iniData.end()) {
            return false;
        }
        return secIt->second.find(key) != secIt->second.end();
    }

    /**
     * @brief Get all section names
     * @return Vector of section names
     */
    [[nodiscard]] std::vector<std::string> getSections() const
    {
        std::vector<std::string> sections;
        sections.reserve(iniData.size());
        
        for (const auto& [section, _] : iniData) {
            sections.push_back(section);
        }
        
        return sections;
    }

    /**
     * @brief Get all keys in a section
     * @param section Section name
     * @return Vector of key names
     */
    [[nodiscard]] std::vector<std::string> getKeys(const std::string& section) const
    {
        std::vector<std::string> keys;
        
        auto it = iniData.find(section);
        if (it != iniData.end()) {
            keys.reserve(it->second.size());
            for (const auto& [key, _] : it->second) {
                keys.push_back(key);
            }
        }
        
        return keys;
    }

    /**
     * @brief Remove a section
     * @param section Section name
     * @return true if section was removed
     */
    bool removeSection(const std::string& section)
    {
        return iniData.erase(section) > 0;
    }

    /**
     * @brief Remove a key from a section
     * @param section Section name
     * @param key Key name
     * @return true if key was removed
     */
    bool removeKey(const std::string& section, const std::string& key)
    {
        auto it = iniData.find(section);
        if (it != iniData.end()) {
            return it->second.erase(key) > 0;
        }
        return false;
    }

    /**
     * @brief Clear all data
     */
    void clear() noexcept
    {
        iniData.clear();
    }

    /**
     * @brief Get number of sections
     * @return Section count
     */
    [[nodiscard]] size_t sectionCount() const noexcept
    {
        return iniData.size();
    }

    /**
     * @brief Get number of keys in a section
     * @param section Section name
     * @return Key count
     */
    [[nodiscard]] size_t keyCount(const std::string& section) const noexcept
    {
        auto it = iniData.find(section);
        return (it != iniData.end()) ? it->second.size() : 0;
    }

    /**
     * @brief Check if INI is empty
     * @return true if no data
     */
    [[nodiscard]] bool empty() const noexcept
    {
        return iniData.empty();
    }

    /**
     * @brief Direct access to internal data (const)
     * @return Const reference to section map
     */
    [[nodiscard]] const SectionMap& data() const noexcept
    {
        return iniData;
    }

private:
    SectionMap iniData;

    /**
     * @brief Trim whitespace from string view (zero-copy)
     * @param str Input string view
     * @return Trimmed string view
     */
    [[nodiscard]] static constexpr std::string_view trim(std::string_view str) noexcept
    {
        // Find first non-whitespace
        size_t first = 0;
        while (first < str.size() && std::isspace(static_cast<unsigned char>(str[first]))) {
            ++first;
        }

        if (first == str.size()) {
            return std::string_view();
        }

        // Find last non-whitespace
        size_t last = str.size();
        while (last > first && std::isspace(static_cast<unsigned char>(str[last - 1]))) {
            --last;
        }

        return str.substr(first, last - first);
    }

    /**
     * @brief Resolve variables in a value string (optimized, no regex)
     * @param value Input value with potential ${var} references
     * @param currentSection Current section for relative references
     * @param maxDepth Maximum recursion depth
     * @return Resolved string
     */
    [[nodiscard]] std::string resolveVariables(const std::string& value, 
                                               const std::string& currentSection, 
                                               int maxDepth) const
    {
        if (maxDepth <= 0 || value.find("${") == std::string::npos) {
            return value;
        }

        std::string result;
        result.reserve(value.size()); // Pre-allocate

        size_t pos = 0;
        while (pos < value.size()) {
            size_t varStart = value.find("${", pos);
            
            if (varStart == std::string::npos) {
                // No more variables, append rest
                result.append(value, pos, std::string::npos);
                break;
            }

            // Append text before variable
            result.append(value, pos, varStart - pos);

            size_t varEnd = value.find('}', varStart + 2);
            if (varEnd == std::string::npos) {
                // Malformed variable, just append as-is
                result.append(value, varStart, std::string::npos);
                break;
            }

            // Extract variable name
            std::string varName = value.substr(varStart + 2, varEnd - varStart - 2);
            
            // Check for section:key format
            size_t colonPos = varName.find(':');
            std::string varValue;
            
            if (colonPos != std::string::npos) {
                // Cross-section reference: ${section:key}
                std::string varSection = varName.substr(0, colonPos);
                std::string varKey = varName.substr(colonPos + 1);
                varValue = getValue(varSection, varKey, "", maxDepth - 1);
            } else {
                // Same-section reference: ${key}
                varValue = getValue(currentSection, varName, "", maxDepth - 1);
            }

            result.append(varValue);
            pos = varEnd + 1;
        }

        return result;
    }
};

/**
 * @brief Helper function to parse INI from file
 * @param filename Path to INI file
 * @return IniParserEx instance
 * @throws std::runtime_error if file cannot be loaded
 */
[[nodiscard]] inline IniParserEx loadIniFile(const std::string& filename)
{
    return IniParserEx(filename);
}

/**
 * @brief Helper function to parse INI from string
 * @param content INI content as string
 * @return IniParserEx instance
 * @throws std::runtime_error if parsing fails
 */
[[nodiscard]] inline IniParserEx parseIniString(const std::string& content)
{
    IniParserEx parser;
    if (!parser.loadFromString(content)) {
        throw std::runtime_error("Failed to parse INI string");
    }
    return parser;
}

#endif // UINI_PARSER_EX_HPP
