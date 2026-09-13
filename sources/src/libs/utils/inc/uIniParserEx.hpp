#ifndef UINI_PARSER_EX_HPP
#define UINI_PARSER_EX_HPP

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <optional>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <functional>

/**
 * @brief Enhanced INI file parser with variable interpolation support.
 * 
 * Features:
 * - Section-based configuration
 * - Variable interpolation: ${key} or ${section:key}
 * - Section inclusion: ${SECTION_NAME} on its own line copies all keys
 *   from that section into the current one. Explicitly defined keys always
 *   take precedence over included ones, regardless of line order.
 *   Nested and circular includes are handled safely.
 * - Comment support (# and ;)
 * - Whitespace handling
 * - Circular reference detection
 * - Zero-copy string operations where possible
 */
class IniParserEx
{
public:
    using KeyValueMap = std::unordered_map<std::string, std::string>;
    using SectionMap  = std::unordered_map<std::string, KeyValueMap>;

    // -----------------------------------------------------------------------
    //  Construction
    // -----------------------------------------------------------------------

    IniParserEx() = default;

    /**
     * @brief Constructor that loads from file.
     * @note On failure the object is left in the empty/default state; check
     *       load()'s return value or call empty() afterwards.
     */
    explicit IniParserEx(const std::string& filename)
    {
        (void)load(filename);
    }

    // -----------------------------------------------------------------------
    //  Load
    // -----------------------------------------------------------------------

    [[nodiscard]] bool load(const std::string& filename)
    {
        std::ifstream file(filename, std::ios::in);
        if (!file.is_open()) return false;
        return loadFromStream(file);
    }

    /**
     * @brief Load INI data from a stream.
     *
     * Standalone lines of the form  ${SECTION_NAME}  (no '=' present) are
     * treated as section-include directives.  After all lines are read the
     * includes are resolved: every key from the referenced section is copied
     * into the including section, but only if that key was *not* set
     * explicitly — so explicit keys always win.
     */
    [[nodiscard]] bool loadFromStream(std::istream& stream)
    {
        iniData.clear();

        // section → ordered list of sections whose keys it wants to import
        std::unordered_map<std::string, std::vector<std::string>> sectionIncludes;

        std::string line;
        std::string currentSection;

        while (std::getline(stream, line)) {
            std::string_view sv = trim(line);

            if (sv.empty() || sv[0] == ';' || sv[0] == '#') continue;

            if (sv.front() == '[' && sv.back() == ']') {
                // ── section header ──────────────────────────────────────────
                currentSection = std::string(sv.substr(1, sv.size() - 2));
                iniData[currentSection]; // ensure entry exists
            } else if (isSectionInclude(sv)) {
                // ── standalone ${SECTION} include directive ─────────────────
                // sv is guaranteed to be "${...}" with no '=' by isSectionInclude()
                std::string ref = std::string(sv.substr(2, sv.size() - 3));
                sectionIncludes[currentSection].push_back(std::move(ref));
            } else {
                // ── ordinary key = value ────────────────────────────────────
                size_t eq = sv.find('=');
                if (eq != std::string_view::npos) {
                    auto keyView = trim(sv.substr(0, eq));
                    auto valView = trim(sv.substr(eq + 1));
                    if (!keyView.empty()) {
                        iniData[currentSection][std::string(keyView)] =
                            std::string(valView);
                    }
                }
            }
        }

        resolveSectionIncludes(sectionIncludes);
        return true;
    }

    [[nodiscard]] bool loadFromString(const std::string& content)
    {
        std::istringstream stream(content);
        return loadFromStream(stream);
    }

    // -----------------------------------------------------------------------
    //  Save
    // -----------------------------------------------------------------------

    [[nodiscard]] bool save(const std::string& filename) const
    {
        std::ofstream file(filename, std::ios::out | std::ios::trunc);
        if (!file.is_open()) return false;
        return saveToStream(file);
    }

    [[nodiscard]] bool saveToStream(std::ostream& stream) const
    {
        for (const auto& [section, kvMap] : iniData) {
            if (!section.empty()) stream << '[' << section << "]\n";
            for (const auto& [key, value] : kvMap)
                stream << key << '=' << value << '\n';
            stream << '\n';
        }
        return stream.good();
    }

    // -----------------------------------------------------------------------
    //  Read
    // -----------------------------------------------------------------------

    [[nodiscard]] std::string getValue(const std::string& section,
                                       const std::string& key,
                                       const std::string& defaultValue = "",
                                       int maxDepth = 10) const
    {
        if (maxDepth <= 0) return defaultValue;

        auto secIt = iniData.find(section);
        if (secIt == iniData.end()) return defaultValue;

        auto keyIt = secIt->second.find(key);
        if (keyIt == secIt->second.end()) return defaultValue;

        return resolveVariables(keyIt->second, section, maxDepth);
    }

    [[nodiscard]] std::optional<std::string> getValueOpt(const std::string& section,
                                                          const std::string& key,
                                                          bool resolve = true) const
    {
        auto secIt = iniData.find(section);
        if (secIt == iniData.end()) return std::nullopt;

        auto keyIt = secIt->second.find(key);
        if (keyIt == secIt->second.end()) return std::nullopt;

        if (resolve) return resolveVariables(keyIt->second, section, 10);
        return keyIt->second;
    }

    [[nodiscard]] std::optional<std::string> getRawValue(const std::string& section,
                                                          const std::string& key) const
    {
        return getValueOpt(section, key, false);
    }

    // -----------------------------------------------------------------------
    //  Write
    // -----------------------------------------------------------------------

    void setValue(const std::string& section,
                  const std::string& key,
                  const std::string& value)
    {
        iniData[section][key] = value;
    }

    // -----------------------------------------------------------------------
    //  Section helpers
    // -----------------------------------------------------------------------

    [[nodiscard]] bool getSection(const std::string& section, KeyValueMap& outMap) const
    {
        auto it = iniData.find(section);
        if (it == iniData.end()) { outMap.clear(); return false; }
        outMap = it->second;
        return true;
    }

    [[nodiscard]] bool getResolvedSection(const std::string& section,
                                          KeyValueMap& outMap,
                                          int maxDepth = 10) const
    {
        auto it = iniData.find(section);
        if (it == iniData.end()) { outMap.clear(); return false; }

        outMap.clear();
        outMap.reserve(it->second.size());
        for (const auto& [key, value] : it->second)
            outMap[key] = resolveVariables(value, section, maxDepth);
        return true;
    }

    [[nodiscard]] bool sectionExists(const std::string& section) const noexcept
    {
        return iniData.find(section) != iniData.end();
    }

    [[nodiscard]] bool keyExists(const std::string& section,
                                  const std::string& key) const noexcept
    {
        auto secIt = iniData.find(section);
        if (secIt == iniData.end()) return false;
        return secIt->second.find(key) != secIt->second.end();
    }

    [[nodiscard]] std::vector<std::string> getSections() const
    {
        std::vector<std::string> sections;
        sections.reserve(iniData.size());
        for (const auto& [sec, _] : iniData) sections.push_back(sec);
        return sections;
    }

    [[nodiscard]] std::vector<std::string> getKeys(const std::string& section) const
    {
        std::vector<std::string> keys;
        auto it = iniData.find(section);
        if (it != iniData.end()) {
            keys.reserve(it->second.size());
            for (const auto& [key, _] : it->second) keys.push_back(key);
        }
        return keys;
    }

    bool removeSection(const std::string& section)
    {
        return iniData.erase(section) > 0;
    }

    bool removeKey(const std::string& section, const std::string& key)
    {
        auto it = iniData.find(section);
        if (it != iniData.end()) return it->second.erase(key) > 0;
        return false;
    }

    // -----------------------------------------------------------------------
    //  Misc
    // -----------------------------------------------------------------------

    void clear() noexcept { iniData.clear(); }

    [[nodiscard]] size_t sectionCount() const noexcept { return iniData.size(); }

    [[nodiscard]] size_t keyCount(const std::string& section) const noexcept
    {
        auto it = iniData.find(section);
        return (it != iniData.end()) ? it->second.size() : 0;
    }

    [[nodiscard]] bool empty() const noexcept { return iniData.empty(); }

    [[nodiscard]] const SectionMap& data() const noexcept { return iniData; }

private:
    SectionMap iniData;

    // -----------------------------------------------------------------------
    //  Helpers
    // -----------------------------------------------------------------------

    /**
     * @brief Return true when @p sv is a standalone section-include token.
     *
     * A section include looks like  ${NAME}  with:
     *   - starts with "${"
     *   - ends with "}"
     *   - no '=' anywhere (that would make it a value with an interpolation)
     *   - at least one character between the braces
     */
    [[nodiscard]] static bool isSectionInclude(std::string_view sv) noexcept
    {
        return sv.size() > 3                    // minimum: "${X}"
            && sv[0] == '$' && sv[1] == '{'
            && sv.back() == '}'
            && sv.find('=') == std::string_view::npos;
    }

    /**
     * @brief Merge included sections into their referencing sections.
     *
     * Uses depth-first resolution so that if B includes C and A includes B,
     * A sees C's keys as well.  Circular includes are detected and skipped
     * (the cycle-forming edge is simply ignored).
     *
     * Precedence rule: explicit keys always win — included keys are inserted
     * only when the target section does not already have that key.
     */
    void resolveSectionIncludes(
        const std::unordered_map<std::string, std::vector<std::string>>& includes)
    {
        std::unordered_set<std::string> resolved;
        std::unordered_set<std::string> inProgress; // cycle detection

        // Recursive lambda — resolves `section` fully before returning.
        std::function<void(const std::string&)> resolve =
            [&](const std::string& section)
        {
            if (resolved.count(section)) return;     // already done
            if (!includes.count(section)) {          // no includes → nothing to do
                resolved.insert(section);
                return;
            }
            if (inProgress.count(section)) return;   // cycle detected, skip edge

            inProgress.insert(section);

            for (const auto& src : includes.at(section)) {
                if (src == section) continue;        // self-include, skip

                // Make sure the source section is itself fully resolved first
                // (handles transitive / nested includes).
                resolve(src);

                auto srcIt = iniData.find(src);
                if (srcIt == iniData.end()) continue; // referenced section missing

                auto& target = iniData[section];
                for (const auto& [key, value] : srcIt->second) {
                    // emplace does nothing if the key already exists → explicit
                    // keys defined in the target section always take precedence.
                    target.emplace(key, value);
                }
            }

            inProgress.erase(section);
            resolved.insert(section);
        };

        for (const auto& [section, _] : includes)
            resolve(section);
    }

    [[nodiscard]] static constexpr std::string_view trim(std::string_view str) noexcept
    {
        size_t first = 0;
        while (first < str.size() &&
               std::isspace(static_cast<unsigned char>(str[first])))
            ++first;

        if (first == str.size()) return {};

        size_t last = str.size();
        while (last > first &&
               std::isspace(static_cast<unsigned char>(str[last - 1])))
            --last;

        return str.substr(first, last - first);
    }

    [[nodiscard]] std::string resolveVariables(const std::string& value,
                                               const std::string& currentSection,
                                               int maxDepth) const
    {
        if (maxDepth <= 0 || value.find("${") == std::string::npos)
            return value;

        std::string result;
        result.reserve(value.size());

        size_t pos = 0;
        while (pos < value.size()) {
            size_t varStart = value.find("${", pos);
            if (varStart == std::string::npos) {
                result.append(value, pos, std::string::npos);
                break;
            }

            result.append(value, pos, varStart - pos);

            size_t varEnd = value.find('}', varStart + 2);
            if (varEnd == std::string::npos) {
                result.append(value, varStart, std::string::npos);
                break;
            }

            std::string varName = value.substr(varStart + 2, varEnd - varStart - 2);

            size_t colonPos = varName.find(':');
            std::string varValue;

            if (colonPos != std::string::npos) {
                // Cross-section reference: ${section:key}
                varValue = getValue(varName.substr(0, colonPos),
                                    varName.substr(colonPos + 1),
                                    "", maxDepth - 1);
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

// ---------------------------------------------------------------------------
//  Free helpers
// ---------------------------------------------------------------------------

[[nodiscard]] inline std::optional<IniParserEx> loadIniFile(const std::string& filename)
{
    IniParserEx parser;
    if (!parser.load(filename)) return std::nullopt;
    return parser;
}

[[nodiscard]] inline std::optional<IniParserEx> parseIniString(const std::string& content)
{
    IniParserEx parser;
    if (!parser.loadFromString(content)) return std::nullopt;
    return parser;
}

#endif // UINI_PARSER_EX_HPP
