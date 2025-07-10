#ifndef PLUGINSCRIPTDATATYPES_HPP
#define PLUGINSCRIPTDATATYPES_HPP

#include <utility>
#include <string>
#include <vector>

/////////////////////////////////////////////////////////////////////////////////
//                               DATATYPES                                     //
/////////////////////////////////////////////////////////////////////////////////

// the type of token parsed from  the script line
enum class Direction
{
    INPUT,                   // > send the first token [ | wait for the second token ]
    OUTPUT,                  // < receive the first token [ | send the second token ]
    INVALID
};


// the type of token parsed from  the script line
enum class TokenType
{
    EMPTY,                   // No content
    HEXSTREAM,               // A stream of hexadecimal characters (e.g., H"4A6F686E")
    REGEX,                   // A regular expression pattern (e.g., R".*")
    FILENAME,                // A file name or file path (e.g., F"firmware.bin")
    TOKEN,                   // a token to be waited for
    STRING_DELIMITED,        // A string enclosed by specific start and end delimiters (e.g., "Hello World")
    STRING_DELIMITED_EMPTY,  // A delimited string with no content between the delimiters (e.g., "")
    STRING_RAW,              // A plain string without any enclosing delimiters (e.g., aaabbb )
    INVALID                  // An unrecognized or malformed token
};


struct PToken
{
    Direction direction                        = Direction::INVALID;
    std::pair<std::string, std::string> values = {"", ""};
    std::pair<TokenType, TokenType>     tokens = {TokenType::INVALID, TokenType::INVALID};
};


struct PluginScriptEntriesType
{
    std::vector<PToken> vCommands;
};


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

inline const std::string& getDirName(Direction dir)
{
    switch(dir)
    {
        case Direction::INPUT:                  { static const std::string name = "INPUT";                   return name; }
        case Direction::OUTPUT:                 { static const std::string name = "OUTPUT";                  return name; }
        case Direction::INVALID:                { static const std::string name = "INVALID";                 return name; }
        default:                                { static const std::string name = "UNKNOWN";                 return name; }
    }
}


#endif // PLUGINSCRIPTDATATYPES_HPP