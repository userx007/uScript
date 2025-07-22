#ifndef PLUGINSCRIPTDATATYPES_HPP
#define PLUGINSCRIPTDATATYPES_HPP

#include <utility>
#include <string>
#include <vector>
#include <unordered_map>

/////////////////////////////////////////////////////////////////////////////////
//                               DATATYPES                                     //
/////////////////////////////////////////////////////////////////////////////////

// the type of token parsed from  the script line
enum class Direction
{
    SEND_RECV,               // command starts with '>', send the first token [ | receive the second token ]
    RECV_SEND,               // command starts with '<', receive the first token [ | send the second token ]
    INVALID                  // neither above, therefore a wrong command format
};


// the type of token parsed from  the script line
enum class TokenType
{
    EMPTY,                   // No content
    HEXSTREAM,               // A stream of hexadecimal characters (e.g., H"4A6F686E")
    REGEX,                   // A regular expression pattern (e.g., R".*")
    FILENAME,                // A file name or file path (e.g., F"firmware.bin")
    TOKEN,                   // A token to be waited for
    LINE,                    // A line terminated with LF (Unix like): \n or CRLF (Windows): \r\n
    STRING_DELIMITED,        // A string enclosed by specific start and end delimiters (e.g., "Hello World")
    STRING_DELIMITED_EMPTY,  // A delimited string with no content between the delimiters (e.g., "")
    STRING_RAW,              // A plain string without any enclosing delimiters (e.g., aaabbb )
    INVALID                  // An unrecognized or malformed token
};


// plugin token structure definition
struct PToken
{
    Direction direction                        = Direction::INVALID;
    std::pair<std::string, std::string> values = {"", ""};
    std::pair<TokenType, TokenType>     tokens = {TokenType::INVALID, TokenType::INVALID};
};


// definition of storage structure for plugin tokens
struct PluginScriptEntriesType
{
    std::vector<PToken> vCommands;
    std::unordered_map<std::string, std::string> mapMacros;
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
        case TokenType::LINE:                   { static const std::string name = "LINE";                    return name; }
        case TokenType::STRING_DELIMITED:       { static const std::string name = "STRING_DELIMITED";        return name; }
        case TokenType::STRING_DELIMITED_EMPTY: { static const std::string name = "STRING_DELIMITED_EMPTY";  return name; }
        case TokenType::STRING_RAW:             { static const std::string name = "STRING_RAW";              return name; }
        case TokenType::INVALID:                { static const std::string name = "INVALID";                 return name; }
        default:                                { static const std::string name = "UNKNOWN";                 return name; }
    }

} /* getTokenName() */


inline const std::string& getDirName(Direction dir)
{
    switch(dir)
    {
        case Direction::RECV_SEND:              { static const std::string name = "RECV_SEND";               return name; }
        case Direction::SEND_RECV:              { static const std::string name = "SEND_RECV";               return name; }
        case Direction::INVALID:                { static const std::string name = "INVALID";                 return name; }
        default:                                { static const std::string name = "UNKNOWN";                 return name; }
    }

} /* getDirName() */


#endif // PLUGINSCRIPTDATATYPES_HPP