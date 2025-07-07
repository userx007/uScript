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
 * @brief String contains a specified char
 */
/*--------------------------------------------------------------------------------------------------------*/

inline bool containsChar(const std::string& input, char ch)
{
    return input.find(ch) != std::string::npos;
}



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief String starts with a specified char
 */
/*--------------------------------------------------------------------------------------------------------*/

inline bool startsWithChar(const std::string& input, char ch)
{
    return !input.empty() && input.front() == ch;
}



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief String ends with a specified char
 */
/*--------------------------------------------------------------------------------------------------------*/

inline bool endsWithChar(const std::string& input, char ch)
{
    return !input.empty() && input.back() == ch;
}



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Splits a string at the first occurrence of a character delimiter, return to pair
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
 * @brief Splits a string at the first occurrence of a character delimiter, return to vector
 */
/*--------------------------------------------------------------------------------------------------------*/

inline void splitAtFirst(const std::string& input, char delimiter, std::vector<std::string>& result)
{
    result.clear();

    size_t pos = input.find(delimiter);
    if (pos == std::string::npos) {
        std::string trimmed = input;
        trimInPlace(trimmed);
        result.push_back(trimmed);
        return;
    }

    std::string first = input.substr(0, pos);
    std::string second = input.substr(pos + 1);
    trimInPlace(first);
    trimInPlace(second);

    result.push_back(first);
    result.push_back(second);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Splits a string at the first occurrence of a delimiter character,
 *        ignoring delimiters that appear inside double-quoted substrings.
 *        Trims both resulting parts.
 */
/*--------------------------------------------------------------------------------------------------------*/
inline void splitAtFirstQuotedAware(const std::string& input, char delimiter, std::pair<std::string, std::string>& result)
{
    bool inQuotes = false;
    size_t pos = std::string::npos;

    for (size_t i = 0; i < input.size(); ++i) {
        char c = input[i];

        if (c == '"') {
            inQuotes = !inQuotes; // Toggle quote state
        } else if (c == delimiter && !inQuotes) {
            pos = i;
            break;
        }
    }

    if (pos == std::string::npos) {
        result = {input, ""};
    } else {
        result = {input.substr(0, pos), input.substr(pos + 1)};
    }

    trimInPlace(result.first);
    trimInPlace(result.second);
}


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
 * @brief Remove empty spaces from a string
 */
/*--------------------------------------------------------------------------------------------------------*/

inline void removeSpaces(std::string& input)
{
    input.erase(std::remove(input.begin(), input.end(), ' '), input.end());

} /* removeSpaces() */


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Checks if the input string starts with `start` and ends with `end`.
 *        Returns true if both conditions are met and the input is long enough.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline bool isDecorated(const std::string& input, const std::string& start, const std::string& end)
{
    return input.size() >= start.size() + end.size() &&
           input.compare(0, start.size(), start) == 0 &&
           input.compare(input.size() - end.size(), end.size(), end) == 0;

} /* isDecorated() */


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Checks if the input string is decorated with `start` and `end`
 *        and contains non-empty content between them.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline bool isDecoratedNonempty(const std::string& input, const std::string& start, const std::string& end)
{
    return isDecorated(input, start, end) &&
           !input.substr(start.size(), input.size() - start.size() - end.size()).empty();

} /* isDecoratedNonempty() */


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Removes the `start` and `end` decorations from the input string,
 *        storing the result in `output`. Returns true if successful.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline bool undecorate(const std::string& input, const std::string& start, const std::string& end, std::string& output)
{
    if (!isDecorated(input, start, end))
        return false;

    output = input.substr(start.size(), input.size() - start.size() - end.size());
    return true;

} /* undecorate() */


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Convenience overload: removes surrounding double quotes from the input string.
 *        Returns true if the input is properly quoted.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline bool undecorate(const std::string& input, std::string& output)
{
    return undecorate(input, "\"", "\"", output);

} /* undecorate() */


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Validates if the input string matches one of the tagged formats:
 *        H"..." R"..." F"..." or just "..." (quoted string).
 *        Returns true if the format is valid or for plain undecorated strings
 */
/*--------------------------------------------------------------------------------------------------------*/

inline bool isValidTaggedOrPlainString(const std::string& input)
{
    // If the string does not contain any quotes, treat it as undecorated
    if (input.find('"') == std::string::npos) {
        return true;
    }

    // If quotes are present, use regex to validate tagged or quoted strings
    static const std::regex pattern(R"(^([HRF])?"[^"]*"$)");
    return std::regex_match(input, pattern);
}


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
 * \brief Split a string into substrings based on space separator
 * \note Quoted strings are supported; spaces inside quotes are not used as separators
 */
/*--------------------------------------------------------------------------------------------------------*/

inline void tokenizeSpace(const std::string& input, std::vector<std::string>& tokens)
{
    std::string strTemp(input);
    const std::regex rgx(R"(\s+(?=([^\"]*\"[^\"]*\")*[^\"]*$))");

    std::sregex_token_iterator iter(strTemp.begin(), strTemp.end(), rgx, -1);
    std::sregex_token_iterator iter_end;

    for (; iter != iter_end; ++iter)
    {
        std::string strToken = *iter;
        if (!strToken.empty())
        {
            tokens.push_back(strToken);
        }
    }
}



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
 * @param input         The input string to process. It will be modified in-place with macro replacements.
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

inline void replaceMacros(std::string& input, const std::unordered_map<std::string, std::string>& macroMap, char macroMarker)
{
    const std::string pattern = std::string("(?=(\\") + macroMarker + "([A-Za-z_][A-Za-z0-9_]*)))";
    const std::regex macroRegex(pattern);

    std::string result;
    std::size_t lastPos = 0;

    auto begin = std::sregex_iterator(input.begin(), input.end(), macroRegex);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        const std::smatch& match = *it;
        std::size_t matchPos = match.position(1);
        std::size_t matchLen = match.length(1);

        result.append(input, lastPos, matchPos - lastPos);

        const std::string key = match[2].str();
        auto macroIt = macroMap.find(key);
        if (macroIt != macroMap.end()) {
            result.append(macroIt->second);
        } else {
            result.append(match.str(1));
        }

        lastPos = matchPos + matchLen;
    }

    result.append(input, lastPos);
    input = std::move(result);
}

} /* namespace ustring */

#endif /* USTRING_UTILS_H */
