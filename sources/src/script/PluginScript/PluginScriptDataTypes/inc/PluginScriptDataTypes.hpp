#ifndef PLUGINSCRIPTDATATYPES_HPP
#define PLUGINSCRIPTDATATYPES_HPP

#include <utility>
#include <string>



// the type of token parsed from input.

enum class TokenType
{
    EMPTY,                   // No content or an explicitly empty token
    HEXSTREAM,               // A stream of hexadecimal characters (e.g., H"4A6F686E")
    REGEX,                   // A regular expression pattern (e.g., R".*")
    FILENAME,                // A file name or file path (e.g., F"firmware.bin")
    STRING_DELIMITED,        // A string enclosed by specific start and end delimiters (e.g., "Hello World")
    STRING_DELIMITED_EMPTY,  // A delimited string with no content between the delimiters (e.g., "")
    STRING_RAW,              // A plain string without any enclosing delimiters (e.g., aaabbb )

    INVALID                  // An unrecognized or malformed token
};

using PToken = std::pair<TokenType, TokenType>;

using PluginScriptEntriesType = std::pair<std::string, std::string>;


#endif // PLUGINSCRIPTDATATYPES_HPP