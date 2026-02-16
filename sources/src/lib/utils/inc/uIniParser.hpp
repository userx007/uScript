#ifndef UINI_PARSER_HPP
#define UINI_PARSER_HPP

#include <fstream>
#include <sstream>
#include <map>
#include <string>
#include <string_view>
#include <optional>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <stdexcept>

/**
 * @brief Simple INI file parser without variable interpolation.
 * 
 * Features:
 * - Section-based configuration
 * - Comment support (# and ;)
 * - Whitespace handling
 * - Ordered key iteration (uses std::map)
 * - Zero-copy string operations where possible
 */
class IniParser
{
public:
    using KeyValueMap = std::map<std::string, std::string>;
    using SectionMap = std::map<std::string, KeyValueMap>;

    /**
     * @brief Default constructor
     */
    IniParser() = default;

    /**
     * @brief Constructor that loads from file
     * @param filename Path to INI file
     * @throws std::runtime_error if file cannot be loaded
     */
    explicit IniParser(const std::string& filename)
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
     * @brief Get a value from the INI file
     * @param section Section name
     * @param key Key name
     * @param defaultValue Default value if key not found
     * @return Value or default
     */
    [[nodiscard]] std::string getValue(const std::string& section, const std::string& key, 
                                       const std::string& defaultValue = "") const
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

    /**
     * @brief Get a value as std::optional (modern API)
     * @param section Section name
     * @param key Key name
     * @return Optional containing value if found
     */
    [[nodiscard]] std::optional<std::string> getValueOpt(const std::string& section, 
                                                          const std::string& key) const noexcept
    {
        auto secIt = iniData.find(section);
        if (secIt != iniData.end()) {
            auto keyIt = secIt->second.find(key);
            if (keyIt != secIt->second.end()) {
                return keyIt->second;
            }
        }
        return std::nullopt;
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
     * @brief Get entire section
     * @param section Section name
     * @param outMap Output map to populate
     * @return true if section exists, false otherwise
     */
    [[nodiscard]] bool getSection(const std::string& section, KeyValueMap& outMap) const
    {
        auto secIt = iniData.find(section);
        if (secIt != iniData.end()) {
            outMap = secIt->second;
            return true;
        }
        outMap.clear();
        return false;
    }

    /**
     * @brief Get entire section as optional
     * @param section Section name
     * @return Optional containing key-value map if section exists
     */
    [[nodiscard]] std::optional<KeyValueMap> getSectionOpt(const std::string& section) const
    {
        auto secIt = iniData.find(section);
        if (secIt != iniData.end()) {
            return secIt->second;
        }
        return std::nullopt;
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
     * @brief Get all section names (ordered)
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
     * @brief Get all keys in a section (ordered)
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

    /**
     * @brief Get value as integer
     * @param section Section name
     * @param key Key name
     * @param defaultValue Default value if key not found or conversion fails
     * @return Integer value
     */
    [[nodiscard]] int getInt(const std::string& section, const std::string& key, int defaultValue = 0) const noexcept
    {
        auto value = getValueOpt(section, key);
        if (!value) {
            return defaultValue;
        }

        try {
            return std::stoi(*value);
        } catch (...) {
            return defaultValue;
        }
    }

    /**
     * @brief Get value as long
     * @param section Section name
     * @param key Key name
     * @param defaultValue Default value if key not found or conversion fails
     * @return Long value
     */
    [[nodiscard]] long getLong(const std::string& section, const std::string& key, long defaultValue = 0) const noexcept
    {
        auto value = getValueOpt(section, key);
        if (!value) {
            return defaultValue;
        }

        try {
            return std::stol(*value);
        } catch (...) {
            return defaultValue;
        }
    }

    /**
     * @brief Get value as double
     * @param section Section name
     * @param key Key name
     * @param defaultValue Default value if key not found or conversion fails
     * @return Double value
     */
    [[nodiscard]] double getDouble(const std::string& section, const std::string& key, double defaultValue = 0.0) const noexcept
    {
        auto value = getValueOpt(section, key);
        if (!value) {
            return defaultValue;
        }

        try {
            return std::stod(*value);
        } catch (...) {
            return defaultValue;
        }
    }

    /**
     * @brief Get value as boolean
     * @param section Section name
     * @param key Key name
     * @param defaultValue Default value if key not found
     * @return Boolean value (true for "true", "1", "yes", "on" - case insensitive)
     */
    [[nodiscard]] bool getBool(const std::string& section, const std::string& key, bool defaultValue = false) const noexcept
    {
        auto value = getValueOpt(section, key);
        if (!value) {
            return defaultValue;
        }

        std::string lower = *value;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return std::tolower(c); });

        return lower == "true" || lower == "1" || lower == "yes" || lower == "on";
    }

    /**
     * @brief Merge another INI parser's data into this one
     * @param other INI parser to merge from
     * @param overwrite Whether to overwrite existing values
     */
    void merge(const IniParser& other, bool overwrite = true)
    {
        for (const auto& [section, kvMap] : other.iniData) {
            for (const auto& [key, value] : kvMap) {
                if (overwrite || !keyExists(section, key)) {
                    iniData[section][key] = value;
                }
            }
        }
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
};

/**
 * @brief Helper function to parse INI from file
 * @param filename Path to INI file
 * @return IniParser instance
 * @throws std::runtime_error if file cannot be loaded
 */
[[nodiscard]] inline IniParser loadIniFile(const std::string& filename)
{
    return IniParser(filename);
}

/**
 * @brief Helper function to parse INI from string
 * @param content INI content as string
 * @return IniParser instance
 * @throws std::runtime_error if parsing fails
 */
[[nodiscard]] inline IniParser parseIniString(const std::string& content)
{
    IniParser parser;
    if (!parser.loadFromString(content)) {
        throw std::runtime_error("Failed to parse INI string");
    }
    return parser;
}

#endif // UINI_PARSER_HPP
