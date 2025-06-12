#ifndef USTRING_UTILS_H
#define USTRING_UTILS_H

#include <algorithm>
#include <utility>
#include <cctype>
#include <sstream>
#include <vector>
#include <string>
#include <regex>
#include <unordered_map>



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @namespace ustring
 * @brief Provides utility functions for string processing
 */
/*--------------------------------------------------------------------------------------------------------*/

namespace ustring
{

/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Trims leading and trailing whitespace from a string.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline std::string trim(const std::string& input)
{
    auto start = std::find_if_not(input.begin(), input.end(), ::isspace);
    auto end = std::find_if_not(input.rbegin(), input.rend(), ::isspace).base();
    return (start < end) ? std::string(start, end) : std::string();

} /* trim() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Trims leading and trailing whitespace from a string in place.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline void trimInPlace(std::string& input)
{
    input.erase(input.begin(), std::find_if_not(input.begin(), input.end(), ::isspace));
    input.erase(std::find_if_not(input.rbegin(), input.rend(), ::isspace).base(), input.end());

} /* trimInPlace() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Trims leading and trailing whitespace from each string in a vector in place.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline void trimInPlace(std::vector<std::string>& vstr)
{
    std::for_each(vstr.begin(), vstr.end(), [&](auto & item) {
        trimInPlace(item);
    });


} /* trimInPlace() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to lowercase.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline std::string tolowercase(const std::string& input)
{
    std::string result = input;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    return result;

} /* tolowercase() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to lowercase in place.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline void tolowercase(std::string& input)
{
    std::transform(input.begin(), input.end(), input.begin(), [](unsigned char c) {
        return std::tolower(c);
    });

} /* tolowercase() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to uppercase.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline std::string touppercase(const std::string& input)
{
    std::string result = input;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return std::toupper(c);
    });
    return result;

} /* touppercase() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to uppercase in place.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline void touppercase(std::string& input)
{
    std::transform(input.begin(), input.end(), input.begin(), [](unsigned char c) {
        return std::toupper(c);
    });

} /* touppercase() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Splits a string at the first occurrence of a character delimiter.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline void splitAtFirst(const std::string& input, char delimiter, std::pair<std::string, std::string>& result)
{
    size_t pos = input.find(delimiter);
    if (pos == std::string::npos) {
        result = {input, ""};
        return;
    }
    result = {input.substr(0, pos), input.substr(pos + 1)};
    trimInPlace(result.first);
    trimInPlace(result.second);

} /* splitAtFirst() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Splits a string at the first occurrence of a string delimiter.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline void splitAtFirst(const std::string& input, const std::string& delimiter, std::pair<std::string, std::string>& result)
{
    size_t pos = input.find(delimiter);
    if (pos == std::string::npos) {
        result = {input, ""};
        return;
    }
    result = {input.substr(0, pos), input.substr(pos + delimiter.length())};
    trimInPlace(result.first);
    trimInPlace(result.second);

} /* splitAtFirst() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Tokenizes a string using whitespace as the delimiter.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline void tokenize(const std::string& input, std::vector<std::string>& tokens)
{
    std::stringstream ss(input);
    std::string token;
    while (ss >> token) {
        trimInPlace(token);
        tokens.push_back(token);
    }

} /* tokenize() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Tokenizes a string using a character delimiter.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline void tokenize(const std::string& input, char delimiter, std::vector<std::string>& tokens)
{
    std::stringstream ss(input);
    std::string token;
    while (std::getline(ss, token, delimiter)) {
        trimInPlace(token);
        tokens.push_back(token);
    }

} /* tokenize() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Tokenizes a string using a string delimiter.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline void tokenize(const std::string& input, const std::string& delimiter, std::vector<std::string>& tokens)
{
    tokens.clear();
    std::string::size_type start = 0;
    std::string::size_type end = input.find(delimiter);
    while (end != std::string::npos) {
        tokens.push_back(input.substr(start, end - start));
        start = end + delimiter.length();
        end = input.find(delimiter, start);
    }
    tokens.push_back(input.substr(start));
    trimInPlace(tokens);

} /* tokenize() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Tokenizes a string using multiple delimiters, splitting at the closest match.
 *
 * This function splits the input string into tokens based on a list of delimiters prioritizing
 * longer delimiters to avoid partial matches
 *
 * All delimiters are considered simultaneously, and the one that appears first in the
 * remaining input is used for the next split. This continues until the end of the string.
  */
/*--------------------------------------------------------------------------------------------------------*/

inline void tokenize(const std::string& input, const std::vector<std::string>& delimiters, std::vector<std::string>& tokens)
{
    tokens.clear();
    size_t start = 0;
    size_t inputLength = input.length();

    // Sort delimiters by length descending to prioritize longer matches
    std::vector<std::string> sortedDelimiters = delimiters;
    std::sort(sortedDelimiters.begin(), sortedDelimiters.end(),
    [](const std::string& a, const std::string& b) {
        return a.length() > b.length();
    });

    while (start < inputLength) {
        size_t minPos = std::string::npos;
        size_t delimLen = 0;

        for (const auto& delim : sortedDelimiters) {
            size_t pos = input.find(delim, start);
            if (pos != std::string::npos && (minPos == std::string::npos || pos < minPos)) {
                minPos = pos;
                delimLen = delim.length();
            }
        }

        if (minPos != std::string::npos) {
            std::string token = input.substr(start, minPos - start);
            if (!token.empty()) {
                tokens.push_back(token);
            }
            start = minPos + delimLen;
        } else {
            std::string token = input.substr(start);
            if (!token.empty()) {
                tokens.push_back(token);
            }
            break;
        }
    }

    trimInPlace(tokens); // Optional: trims whitespace from each token

} /* tokenize() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Tokenizes a string using a sequence of delimiters, applying each delimiter in the order they appear
 * in the `delimiters` vector, and only once per delimiter.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline void tokenizeEx(const std::string& input, const std::vector<std::string>& delimiters, std::vector<std::string>& tokens)
{
    size_t start = 0;
    for (const auto& delimiter : delimiters) {
        size_t pos = input.find(delimiter, start);
        if (pos != std::string::npos) {
            tokens.push_back(input.substr(start, pos - start));
            start = pos + delimiter.length();
        }
    }
    if (start < input.size()) {
        tokens.push_back(input.substr(start));
    }
    trimInPlace(tokens);

} /* tokenizeEx2() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Joins a vector of strings into a single string with a delimiter.
*/
/*--------------------------------------------------------------------------------------------------------*/

inline std::string joinStrings(const std::vector<std::string>& strings, const std::string& delimiter = " ")
{
    std::string result;
    for (size_t i = 0; i < strings.size(); ++i) {
        result += strings[i];
        if (i != strings.size() - 1) {
            result += delimiter;
        }
    }
    return result;

} /* joinStrings() */


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Joins a vector of strings into a single string with a delimiter and stores the result in an output parameter.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline void joinStrings(const std::vector<std::string>& strings, const std::string& delimiter, std::string& outResult)
{
    outResult.clear();  // Ensure the output string is empty before starting
    for (size_t i = 0; i < strings.size(); ++i) {
        outResult += strings[i];
        if (i != strings.size() - 1) {
            outResult += delimiter;
        }
    }
} /* joinStrings() */


/**
 * @brief Replaces macro placeholders in a string with corresponding values from a macro map.
 *
 * This function searches the input string for macro placeholders marked by a specific character
 * (e.g., '$', '#', etc.) followed by a valid identifier (letters, digits, and underscores, starting with a letter or underscore).
 * It replaces each recognized macro with its corresponding value from the provided macro map.
 * If a macro is not found in the map, it is left unchanged in the string.
 *
 * @param str         The input string to process. It will be modified in-place with macro replacements.
 * @param macroMap    A map containing macro names as keys and their replacement strings as values.
 * @param macroMarker A character used to identify the beginning of a macro (e.g., '$' or '#').
 *
 * @note The macro name must match the regex pattern: [A-Za-z_][A-Za-z0-9_]*
 *       and must be immediately preceded by the macroMarker character.
 *
 * @example
 * std::unordered_map<std::string, std::string> macros = { {"NAME", "Alice"}, {"AGE", "30"} };
 * std::string text = "Hello $NAME, you are $AGE years old.";
 * replaceConstantMacros(text, macros, '$');
 * -> text becomes: "Hello Alice, you are 30 years old."
 */
inline void replaceMacros(std::string& str, const std::unordered_map<std::string, std::string>& macroMap, char macroMarker)
{
    // Build the regex pattern dynamically using the macro marker
    const std::string marker(1, macroMarker);
    const std::string pattern = R"(\)" + marker + R"(([A-Za-z_][A-Za-z0-9_]*))";
    const std::regex macroRegex(pattern);

    std::smatch match;
    std::string result;
    std::string::const_iterator searchStart(str.cbegin());

    while (std::regex_search(searchStart, str.cend(), match, macroRegex)) {
        result.append(searchStart, match[0].first); // Append text before match
        std::string key = match[1].str();

        auto it = macroMap.find(key);
        if (it != macroMap.end()) {
            result.append(it->second); // Replace macro
        } else {
            result.append(match[0].str()); // Leave unknown macro unchanged
        }

        searchStart = match[0].second; // Move past the match
    }

    result.append(searchStart, str.cend()); // Append the rest of the string
    str = std::move(result);

} /* replaceMacros() */


} /* namespace ustring */

#endif /* USTRING_UTILS_H */
