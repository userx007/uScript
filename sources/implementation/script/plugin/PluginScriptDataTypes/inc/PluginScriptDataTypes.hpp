#ifndef PLUGINSCRIPTDATATYPES_HPP
#define PLUGINSCRIPTDATATYPES_HPP

#include <utility>
#include <string>
#include <vector>

/////////////////////////////////////////////////////////////////////////////////
//                               DATATYPES                                     //
/////////////////////////////////////////////////////////////////////////////////


// the type of token parsed from input
enum class TokenType
{
    EMPTY,                   // No content
    HEXSTREAM,               // A stream of hexadecimal characters (e.g., H"4A6F686E")
    REGEX,                   // A regular expression pattern (e.g., R".*")
    FILENAME,                // A file name or file path (e.g., F"firmware.bin")
    STRING_DELIMITED,        // A string enclosed by specific start and end delimiters (e.g., "Hello World")
    STRING_DELIMITED_EMPTY,  // A delimited string with no content between the delimiters (e.g., "")
    STRING_RAW,              // A plain string without any enclosing delimiters (e.g., aaabbb )

    INVALID                  // An unrecognized or malformed token
};

using PToken                = typename std::pair<TokenType, TokenType>;
using PData                 = typename std::pair<std::string, std::string>;
using PCommandType          = typename std::pair<PData, PToken>;
using PCommandStorageType   = typename std::vector<PCommandType>;

struct PluginScriptEntries {
    PCommandStorageType vCommands;
};

using PluginScriptEntriesType = PluginScriptEntries;


/////////////////////////////////////////////////////////////////////////////////
//                 DATATYPES LOGGING SUPPORT (type to string)                  //
/////////////////////////////////////////////////////////////////////////////////


inline const std::string& getTokenName(TokenType type)
{
    switch(type)
    {
        case TokenType::EMPTY:                  { static const std::string name = "EMPTY";                   return name; }
        case TokenType::HEXSTREAM:              { static const std::string name = "HEXSTREAM";               return name; }
        case TokenType::REGEX:                  { static const std::string name = "REGEX";                   return name; }
        case TokenType::FILENAME:               { static const std::string name = "FILENAME";                return name; }
        case TokenType::STRING_DELIMITED:       { static const std::string name = "STRING_DELIMITED";        return name; }
        case TokenType::STRING_DELIMITED_EMPTY: { static const std::string name = "STRING_DELIMITED_EMPTY";  return name; }
        case TokenType::STRING_RAW:             { static const std::string name = "STRING_RAW";              return name; }
        case TokenType::INVALID:                { static const std::string name = "INVALID";                 return name; }
        default:                                { static const std::string name = "UNKNOWN";                 return name; }
    }
}

#endif // PLUGINSCRIPTDATATYPES_HPP