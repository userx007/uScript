#ifndef USTRING_UTILS_H
#define USTRING_UTILS_H

#include <algorithm>
#include <utility>
#include <sstream>
#include <vector>
#include <string>
#include <regex>
#include <unordered_map>
#include <string_view>
#include <span>
#include <cctype>
#include <cstdint>
#include <optional>
#include <charconv>

/*--------------------------------------------------------------------------------------------------------*/
/**
 * @namespace ustring
 * @brief Provides utility functions for string processing
 */
/*--------------------------------------------------------------------------------------------------------*/

namespace ustring
{

/*========================================================================================================*/
/*                                      WHITESPACE & TRIMMING                                             */
/*========================================================================================================*/

/**
 * @brief Safe character classification (prevents UB with negative chars)
 */
inline bool is_space(char ch) noexcept
{
    return std::isspace(static_cast<unsigned char>(ch));
}

inline bool is_digit(char ch) noexcept
{
    return std::isdigit(static_cast<unsigned char>(ch));
}

inline bool is_alpha(char ch) noexcept
{
    return std::isalpha(static_cast<unsigned char>(ch));
}

inline bool is_alnum(char ch) noexcept
{
    return std::isalnum(static_cast<unsigned char>(ch));
}

/**
 * @brief Trims leading and trailing whitespace (returns new string)
 */
inline std::string trim(std::string_view input)
{
    auto start = std::find_if_not(input.begin(), input.end(), is_space);
    auto end = std::find_if_not(input.rbegin(), input.rend(), is_space).base();
    return (start < end) ? std::string(start, end) : std::string();
}

/**
 * @brief Trims leading whitespace (returns new string)
 */
inline std::string trim_left(std::string_view input)
{
    auto start = std::find_if_not(input.begin(), input.end(), is_space);
    return std::string(start, input.end());
}

/**
 * @brief Trims trailing whitespace (returns new string)
 */
inline std::string trim_right(std::string_view input)
{
    auto end = std::find_if_not(input.rbegin(), input.rend(), is_space).base();
    return std::string(input.begin(), end);
}

/**
 * @brief Trims leading and trailing whitespace in place
 */
inline void trimInPlace(std::string& input)
{
    // Trim leading
    input.erase(input.begin(), std::find_if_not(input.begin(), input.end(), is_space));
    // Trim trailing
    input.erase(std::find_if_not(input.rbegin(), input.rend(), is_space).base(), input.end());
}

/**
 * @brief Trims leading and trailing whitespace from each string in a vector
 */
inline void trimInPlace(std::vector<std::string>& vstr)
{
    for (auto& str : vstr) {
        trimInPlace(str);
    }
}

/**
 * @brief Skip leading whitespaces in a string_view
 */
inline std::string_view skipWhitespace(std::string_view sv) noexcept
{
    while (!sv.empty() && is_space(sv.front())) {
        sv.remove_prefix(1);
    }
    return sv;
}

/**
 * @brief Remove all whitespace from string in place
 */
inline void removeWhitespace(std::string& input)
{
    input.erase(std::remove_if(input.begin(), input.end(), is_space), input.end());
}

/**
 * @brief Remove all spaces from string in place
 */
inline void removeSpaces(std::string& input)
{
    input.erase(std::remove(input.begin(), input.end(), ' '), input.end());
}

/*========================================================================================================*/
/*                                       CASE CONVERSION                                                  */
/*========================================================================================================*/

/**
 * @brief Converts a string to lowercase (returns new string)
 */
inline std::string tolowercase(std::string_view input)
{
    std::string result;
    result.reserve(input.size());
    std::transform(input.begin(), input.end(), std::back_inserter(result),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

/**
 * @brief Converts a string to lowercase in place
 */
inline void tolowercase(std::string& input)
{
    std::transform(input.begin(), input.end(), input.begin(),
                   [](unsigned char c) { return std::tolower(c); });
}

/**
 * @brief Converts a string to uppercase (returns new string)
 */
inline std::string touppercase(std::string_view input)
{
    std::string result;
    result.reserve(input.size());
    std::transform(input.begin(), input.end(), std::back_inserter(result),
                   [](unsigned char c) { return std::toupper(c); });
    return result;
}

/**
 * @brief Converts a string to uppercase in place
 */
inline void touppercase(std::string& input)
{
    std::transform(input.begin(), input.end(), input.begin(),
                   [](unsigned char c) { return std::toupper(c); });
}

/*========================================================================================================*/
/*                                    STRING COMPARISON                                                   */
/*========================================================================================================*/

/**
 * @brief Case-insensitive string comparison
 */
inline bool equals_ignore_case(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(),
                     [](char c1, char c2) {
                         return std::tolower(static_cast<unsigned char>(c1)) ==
                                std::tolower(static_cast<unsigned char>(c2));
                     });
}

/**
 * @brief Check if string contains substring (case-sensitive)
 */
inline bool contains(std::string_view haystack, std::string_view needle) noexcept
{
    return haystack.find(needle) != std::string_view::npos;
}

/**
 * @brief Check if string contains character
 */
inline bool containsChar(std::string_view input, char ch) noexcept
{
    return input.find(ch) != std::string_view::npos;
}

/**
 * @brief Check if string starts with prefix
 */
inline bool starts_with(std::string_view input, std::string_view prefix) noexcept
{
    return input.size() >= prefix.size() &&
           input.substr(0, prefix.size()) == prefix;
}

/**
 * @brief Check if string starts with character
 */
inline bool startsWithChar(std::string_view input, char ch) noexcept
{
    return !input.empty() && input.front() == ch;
}

/**
 * @brief Check if string ends with suffix
 */
inline bool ends_with(std::string_view input, std::string_view suffix) noexcept
{
    return input.size() >= suffix.size() &&
           input.substr(input.size() - suffix.size()) == suffix;
}

/**
 * @brief Check if string ends with character
 */
inline bool endsWithChar(std::string_view input, char ch) noexcept
{
    return !input.empty() && input.back() == ch;
}

/*========================================================================================================*/
/*                                    STRING SPLITTING                                                    */
/*========================================================================================================*/

/**
 * @brief Split at first occurrence of delimiter (returns pair)
 */
inline std::pair<std::string, std::string> splitAtFirst(std::string_view input, char delimiter)
{
    size_t pos = input.find(delimiter);
    if (pos == std::string_view::npos) {
        return {std::string(input), ""};
    }
    return {trim(input.substr(0, pos)), trim(input.substr(pos + 1))};
}

/**
 * @brief Split at first occurrence of delimiter (output to pair reference)
 */
inline void splitAtFirst(std::string_view input, char delimiter, 
                         std::pair<std::string, std::string>& result)
{
    result = splitAtFirst(input, delimiter);
}

/**
 * @brief Split at first occurrence of delimiter (output to vector)
 */
inline void splitAtFirst(std::string_view input, char delimiter, 
                         std::vector<std::string>& result)
{
    result.clear();
    auto [first, second] = splitAtFirst(input, delimiter);
    result.push_back(std::move(first));
    if (!second.empty()) {
        result.push_back(std::move(second));
    }
}

/**
 * @brief Split at first occurrence of string delimiter
 */
inline std::pair<std::string, std::string> splitAtFirst(std::string_view input, 
                                                         std::string_view delimiter)
{
    size_t pos = input.find(delimiter);
    if (pos == std::string_view::npos) {
        return {std::string(input), ""};
    }
    return {trim(input.substr(0, pos)), 
            trim(input.substr(pos + delimiter.length()))};
}

/**
 * @brief Split at first occurrence of string delimiter (output to pair)
 */
inline void splitAtFirst(std::string_view input, std::string_view delimiter,
                         std::pair<std::string, std::string>& result)
{
    result = splitAtFirst(input, delimiter);
}

/**
 * @brief Split in reverse at last occurrence of delimiter
 */
inline std::pair<std::string, std::string> splitReverseAtChar(std::string_view input, char ch)
{
    size_t pos = input.rfind(ch);
    if (pos == std::string_view::npos) {
        return {std::string(input), ""};
    }
    return {trim(input.substr(0, pos)), trim(input.substr(pos + 1))};
}

/**
 * @brief Split in reverse at last occurrence of delimiter (output params)
 */
inline void splitReverseAtChar(std::string_view input, std::string& left, 
                               std::string& right, char ch)
{
    auto [l, r] = splitReverseAtChar(input, ch);
    left = std::move(l);
    right = std::move(r);
}

/**
 * @brief Get substring until delimiter (or entire string if not found)
 */
inline std::string_view substringUntil(std::string_view input, char delimiter) noexcept
{
    size_t pos = input.find(delimiter);
    std::string_view result = (pos != std::string_view::npos) ? input.substr(0, pos) : input;
    
    // Trim
    while (!result.empty() && is_space(result.front())) result.remove_prefix(1);
    while (!result.empty() && is_space(result.back())) result.remove_suffix(1);
    
    return result;
}

/**
 * @brief Split at first delimiter, ignoring delimiters inside quotes
 */
inline std::pair<std::string, std::string> splitAtFirstQuotedAware(std::string_view input, char delimiter)
{
    bool inQuotes = false;
    size_t pos = std::string_view::npos;

    for (size_t i = 0; i < input.size(); ++i) {
        char c = input[i];
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == delimiter && !inQuotes) {
            pos = i;
            break;
        }
    }

    if (pos == std::string_view::npos) {
        return {std::string(input), ""};
    }
    return {trim(input.substr(0, pos)), trim(input.substr(pos + 1))};
}

/**
 * @brief Split at first delimiter (quote-aware, output to pair)
 */
inline void splitAtFirstQuotedAware(std::string_view input, char delimiter,
                                    std::pair<std::string, std::string>& result)
{
    result = splitAtFirstQuotedAware(input, delimiter);
}

/*========================================================================================================*/
/*                                    STRING DECORATION                                                   */
/*========================================================================================================*/

/**
 * @brief Check if string is decorated with start/end markers
 */
inline bool isDecorated(std::string_view input, std::string_view start, 
                       std::string_view end) noexcept
{
    return input.size() >= start.size() + end.size() &&
           starts_with(input, start) &&
           ends_with(input, end);
}

/**
 * @brief Check if decorated with non-empty content between markers
 */
inline bool isDecoratedNonempty(std::string_view input, std::string_view start,
                                std::string_view end) noexcept
{
    return isDecorated(input, start, end) &&
           input.size() > start.size() + end.size();
}

/**
 * @brief Remove decoration markers (returns new string)
 */
inline std::optional<std::string> undecorate(std::string_view input, 
                                             std::string_view start,
                                             std::string_view end)
{
    if (!isDecorated(input, start, end)) {
        return std::nullopt;
    }
    
    auto content = input.substr(start.size(), input.size() - start.size() - end.size());
    return std::string(content);
}

/**
 * @brief Remove decoration markers (output parameter version)
 */
inline bool undecorate(std::string_view input, std::string_view start,
                      std::string_view end, std::string& output)
{
    auto result = undecorate(input, start, end);
    if (result) {
        output = std::move(*result);
        return true;
    }
    return false;
}

/**
 * @brief Remove decoration markers in place
 */
inline bool undecorate(std::string& input, std::string_view start, std::string_view end)
{
    if (!isDecorated(input, start, end)) {
        return false;
    }
    input = input.substr(start.size(), input.size() - start.size() - end.size());
    return true;
}

/**
 * @brief Remove surrounding double quotes
 */
inline std::optional<std::string> undecorate(std::string_view input)
{
    return undecorate(input, "\"", "\"");
}

/**
 * @brief Remove surrounding double quotes (output parameter)
 */
inline bool undecorate(std::string_view input, std::string& output)
{
    return undecorate(input, "\"", "\"", output);
}

/**
 * @brief Remove surrounding double quotes in place
 */
inline bool undecorate(std::string& input)
{
    return undecorate(input, "\"", "\"");
}

/*========================================================================================================*/
/*                                    STRING VALIDATION                                                   */
/*========================================================================================================*/

/**
 * @brief Validate tagged or plain quoted string format
 */
inline bool isValidTaggedOrPlainString(std::string_view input)
{
    // No quotes means undecorated (valid)
    if (input.find('"') == std::string_view::npos) {
        return true;
    }

    // Cached regex pattern
    static const std::regex pattern(R"(^([HRF])?"[^"]*"$)", std::regex::optimize);
    return std::regex_match(input.begin(), input.end(), pattern);
}

/**
 * @brief Validate macro usage format
 */
inline bool isValidMacroUsage(std::string_view input)
{
    static const std::regex rgx(R"(^!?\$[a-zA-Z_][a-zA-Z0-9_]+$)", 
                               std::regex::ECMAScript | std::regex::optimize);
    return std::regex_match(input.begin(), input.end(), rgx);
}

/**
 * @brief Check if string is in condition format
 */
inline bool isConditionFormat(std::string_view input)
{
    static const std::regex pattern(R"(^\|\s+\S.*$)", std::regex::optimize);
    return std::regex_match(input.begin(), input.end(), pattern);
}

/**
 * @brief Extract condition from formatted string
 */
inline std::optional<std::string> extractCondition(std::string_view input)
{
    std::match_results<std::string_view::const_iterator> match;
    static const std::regex pattern(R"(^\|\s+(\S.*))", std::regex::optimize);

    if (std::regex_match(input.begin(), input.end(), match, pattern) && match.size() > 1) {
        return std::string(match[1].first, match[1].second);
    }
    return std::nullopt;
}

/**
 * @brief Extract condition (output parameter version)
 */
inline bool extractCondition(std::string_view input, std::string& conditionOut)
{
    auto result = extractCondition(input);
    if (result) {
        conditionOut = std::move(*result);
        return true;
    }
    conditionOut.clear();
    return false;
}

/*========================================================================================================*/
/*                                    STRING TOKENIZATION                                                 */
/*========================================================================================================*/

/**
 * @brief Tokenize using whitespace as delimiter
 */
inline std::vector<std::string> tokenize(std::string_view input)
{
    std::vector<std::string> tokens;
    std::string_view remaining = input;
    
    while (!remaining.empty()) {
        // Skip leading whitespace
        while (!remaining.empty() && is_space(remaining.front())) {
            remaining.remove_prefix(1);
        }
        
        if (remaining.empty()) break;
        
        // Find next whitespace
        size_t pos = 0;
        while (pos < remaining.size() && !is_space(remaining[pos])) {
            ++pos;
        }
        
        tokens.emplace_back(remaining.substr(0, pos));
        remaining.remove_prefix(pos);
    }
    
    return tokens;
}

/**
 * @brief Tokenize using whitespace (output parameter)
 */
inline void tokenize(std::string_view input, std::vector<std::string>& tokens)
{
    tokens = tokenize(input);
}

/**
 * @brief Tokenize using character delimiter
 */
inline std::vector<std::string> tokenize(std::string_view input, char delimiter)
{
    std::vector<std::string> tokens;
    size_t start = 0;
    size_t pos = 0;
    
    while ((pos = input.find(delimiter, start)) != std::string_view::npos) {
        tokens.push_back(trim(input.substr(start, pos - start)));
        start = pos + 1;
    }
    
    if (start <= input.size()) {
        tokens.push_back(trim(input.substr(start)));
    }
    
    return tokens;
}

/**
 * @brief Tokenize using character delimiter (output parameter)
 */
inline void tokenize(std::string_view input, char delimiter, 
                    std::vector<std::string>& tokens)
{
    tokens = tokenize(input, delimiter);
}

/**
 * @brief Tokenize using string delimiter
 */
inline std::vector<std::string> tokenize(std::string_view input, std::string_view delimiter)
{
    std::vector<std::string> tokens;
    if (delimiter.empty()) return tokens;
    
    size_t start = 0;
    size_t pos = 0;
    
    while ((pos = input.find(delimiter, start)) != std::string_view::npos) {
        tokens.push_back(trim(input.substr(start, pos - start)));
        start = pos + delimiter.length();
    }
    
    if (start <= input.size()) {
        tokens.push_back(trim(input.substr(start)));
    }
    
    return tokens;
}

/**
 * @brief Tokenize using string delimiter (output parameter)
 */
inline void tokenize(std::string_view input, std::string_view delimiter,
                    std::vector<std::string>& tokens)
{
    tokens = tokenize(input, delimiter);
}

/**
 * @brief Tokenize using multiple delimiters (closest match priority)
 */
inline std::vector<std::string> tokenize(std::string_view input, 
                                        const std::vector<std::string>& delimiters)
{
    std::vector<std::string> tokens;
    if (delimiters.empty()) {
        tokens.push_back(trim(input));
        return tokens;
    }
    
    // Sort delimiters by length (longest first) to prioritize longer matches
    std::vector<std::string> sortedDelims = delimiters;
    std::sort(sortedDelims.begin(), sortedDelims.end(),
              [](const auto& a, const auto& b) { return a.length() > b.length(); });
    
    size_t start = 0;
    while (start < input.length()) {
        size_t minPos = std::string_view::npos;
        size_t delimLen = 0;
        
        // Find closest delimiter
        for (const auto& delim : sortedDelims) {
            size_t pos = input.find(delim, start);
            if (pos != std::string_view::npos && 
                (minPos == std::string_view::npos || pos < minPos)) {
                minPos = pos;
                delimLen = delim.length();
            }
        }
        
        if (minPos != std::string_view::npos) {
            auto token = trim(input.substr(start, minPos - start));
            if (!token.empty()) {
                tokens.push_back(std::string(token));
            }
            start = minPos + delimLen;
        } else {
            auto token = trim(input.substr(start));
            if (!token.empty()) {
                tokens.push_back(std::string(token));
            }
            break;
        }
    }
    
    return tokens;
}

/**
 * @brief Tokenize using multiple delimiters (output parameter)
 */
inline void tokenize(std::string_view input, const std::vector<std::string>& delimiters,
                    std::vector<std::string>& tokens)
{
    tokens = tokenize(input, delimiters);
}

/**
 * @brief Tokenize using ordered sequence of delimiters
 */
inline std::vector<std::string> tokenizeEx(std::string_view input,
                                           const std::vector<std::string>& delimiters)
{
    std::vector<std::string> tokens;
    tokens.reserve(delimiters.size() + 1);
    
    size_t start = 0;
    for (const auto& delimiter : delimiters) {
        size_t pos = input.find(delimiter, start);
        if (pos != std::string_view::npos) {
            tokens.push_back(trim(input.substr(start, pos - start)));
            start = pos + delimiter.length();
        }
    }
    
    if (start < input.size()) {
        tokens.push_back(trim(input.substr(start)));
    }
    
    return tokens;
}

/**
 * @brief Tokenize using ordered sequence of delimiters (output parameter)
 */
inline void tokenizeEx(std::string_view input, const std::vector<std::string>& delimiters,
                      std::vector<std::string>& tokens)
{
    tokens = tokenizeEx(input, delimiters);
}

/**
 * @brief Tokenize by spaces, respecting quoted strings (manual implementation, faster)
 */
inline std::vector<std::string> tokenizeSpaceQuotesAware(std::string_view input)
{
    std::vector<std::string> tokens;
    bool in_quotes = false;
    size_t token_start = 0;
    bool in_token = false;
    
    for (size_t i = 0; i < input.size(); ++i) {
        char ch = input[i];
        
        if (ch == '"') {
            in_quotes = !in_quotes;
            if (!in_token) {
                token_start = i;
                in_token = true;
            }
        } else if (is_space(ch) && !in_quotes) {
            if (in_token) {
                // End of token
                std::string token = trim(input.substr(token_start, i - token_start));
                if (!token.empty()) {
                    tokens.push_back(std::move(token));
                }
                in_token = false;
            }
        } else {
            if (!in_token) {
                token_start = i;
                in_token = true;
            }
        }
    }
    
    // Handle last token
    if (in_token) {
        std::string token = trim(input.substr(token_start));
        if (!token.empty()) {
            tokens.push_back(std::move(token));
        }
    }
    
    return tokens;
}


/**
 * @brief Tokenize by spaces (quote-aware, output parameter)
 */
inline void tokenizeSpaceQuotesAware(std::string_view input, 
                                    std::vector<std::string>& tokens)
{
    tokens = tokenizeSpaceQuotesAware(input);
}


/*========================================================================================================*/
/*                                    STRING JOINING                                                      */
/*========================================================================================================*/

/**
 * @brief Join strings with delimiter (returns new string)
 */
inline std::string joinStrings(const std::vector<std::string>& strings, 
                               std::string_view delimiter)
{
    if (strings.empty()) return "";
    
    // Calculate total size to avoid reallocations
    size_t total_size = 0;
    for (const auto& s : strings) {
        total_size += s.size();
    }
    total_size += delimiter.size() * (strings.size() - 1);
    
    std::string result;
    result.reserve(total_size);
    
    result += strings[0];
    for (size_t i = 1; i < strings.size(); ++i) {
        result += delimiter;
        result += strings[i];
    }
    
    return result;
}

/**
 * @brief Join strings with character delimiter
 */
inline std::string joinStrings(const std::vector<std::string>& strings, char delimiter)
{
    if (strings.empty()) return "";
    
    size_t total_size = 0;
    for (const auto& s : strings) {
        total_size += s.size();
    }
    total_size += (strings.size() - 1);  // delimiters
    
    std::string result;
    result.reserve(total_size);
    
    result += strings[0];
    for (size_t i = 1; i < strings.size(); ++i) {
        result += delimiter;
        result += strings[i];
    }
    
    return result;
}

/**
 * @brief Join strings with delimiter (output parameter)
 */
inline void joinStrings(const std::vector<std::string>& strings,
                       std::string_view delimiter, std::string& outResult)
{
    outResult = joinStrings(strings, delimiter);
}

/*========================================================================================================*/
/*                                    STRING REPLACEMENT                                                  */
/*========================================================================================================*/

/**
 * @brief Replace all occurrences of a substring
 */
inline std::string replace_all(std::string_view input, std::string_view from, 
                              std::string_view to)
{
    if (from.empty()) return std::string(input);
    
    std::string result;
    result.reserve(input.size());
    
    size_t start = 0;
    size_t pos = 0;
    
    while ((pos = input.find(from, start)) != std::string_view::npos) {
        result.append(input.substr(start, pos - start));
        result.append(to);
        start = pos + from.length();
    }
    
    result.append(input.substr(start));
    return result;
}

/**
 * @brief Replace all occurrences in place
 */
inline void replace_all_inplace(std::string& input, std::string_view from, 
                               std::string_view to)
{
    if (from.empty()) return;
    
    size_t pos = 0;
    while ((pos = input.find(from, pos)) != std::string::npos) {
        input.replace(pos, from.length(), to);
        pos += to.length();
    }
}

/**
 * @brief Replace macros in string using map
 */
inline void replaceMacros(std::string& input, 
                         const std::unordered_map<std::string, std::string>& macroMap,
                         char macroMarker)
{
    std::string result;
    result.reserve(input.size() * 1.2);  // Reserve extra space
    
    size_t i = 0;
    while (i < input.size()) {
        if (input[i] == macroMarker && i + 1 < input.size() && 
            (is_alpha(input[i + 1]) || input[i + 1] == '_')) {
            
            // Extract macro name
            size_t start = i + 1;
            size_t end = start;
            while (end < input.size() && (is_alnum(input[end]) || input[end] == '_')) {
                ++end;
            }
            
            std::string macroName = input.substr(start, end - start);
            auto it = macroMap.find(macroName);
            
            if (it != macroMap.end()) {
                result += it->second;
            } else {
                result += input[i];  // Keep marker
                result += macroName;
            }
            
            i = end;
        } else {
            result += input[i];
            ++i;
        }
    }
    
    input = std::move(result);
}

/*========================================================================================================*/
/*                                    TYPE CONVERSIONS                                                    */
/*========================================================================================================*/

/**
 * @brief Convert string to vector of bytes
 */
inline std::vector<uint8_t> stringToVector(std::string_view input)
{
    std::string_view view = input;
    
    // Remove quotes if present
    if (view.size() >= 2 && view.front() == '"' && view.back() == '"') {
        view.remove_prefix(1);
        view.remove_suffix(1);
    }
    
    std::vector<uint8_t> output;
    output.reserve(view.size() + 1);
    output.assign(view.begin(), view.end());
    output.push_back('\0');
    
    return output;
}

/**
 * @brief Convert string to vector (output parameter)
 */
inline bool stringToVector(std::string_view input, std::vector<uint8_t>& output)
{
    output = stringToVector(input);
    return true;
}

/**
 * @brief Replace null terminator with newline
 */
inline void replaceNullWithNewline(std::vector<uint8_t>& data)
{
    if (!data.empty() && data.back() == '\0') {
        data.back() = '\n';
        data.push_back('\0');
    }
}

/**
 * @brief Convert span to string
 */
inline std::string spanToString(std::span<const uint8_t> span)
{
    return std::string(reinterpret_cast<const char*>(span.data()), span.size());
}

/**
 * @brief Convert string to span
 */
inline std::span<const uint8_t> stringToSpan(std::string_view str) noexcept
{
    return std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(str.data()), str.size());
}

/*========================================================================================================*/
/*                                    NUMBER PARSING                                                      */
/*========================================================================================================*/

/**
 * @brief Parse integer from string (modern, fast)
 */
template<typename T = int>
inline std::optional<T> parse_int(std::string_view str) noexcept
{
    T value;
    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);
    if (ec == std::errc() && ptr == str.data() + str.size()) {
        return value;
    }
    return std::nullopt;
}

/**
 * @brief Parse double from string
 */
inline std::optional<double> parse_double(std::string_view str)
{
    try {
        size_t pos;
        double val = std::stod(std::string(str), &pos);
        if (pos == str.size()) {
            return val;
        }
    } catch (...) {}
    return std::nullopt;
}

} /* namespace ustring */

#endif /* USTRING_UTILS_H */