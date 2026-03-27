#ifndef U_COMM_SCRIPT_DATA_TYPES_HPP
#define U_COMM_SCRIPT_DATA_TYPES_HPP

#include "uSharedConfig.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <utility>

/**
 * @brief Direction of command execution
 */
enum class CommCommandDirection
{
    SEND_RECV,  ///< CommCommand starts with '>', send first then optionally receive
    RECV_SEND,  ///< CommCommand starts with '<', receive first then optionally send
    DELAY,      ///< CommCommand starts with '!', insert delay
    INVALID     ///< Neither above, wrong command format
};

/**
 * @brief Type of token parsed from script line
 */
enum class CommCommandTokenType
{
    EMPTY,                   ///< No content
    HEXSTREAM,               ///< Hexadecimal stream [exact buffer]  (e.g., H"4A6F686E") 
    REGEX,                   ///< Regular expression pattern (e.g., R".*")
    FILENAME,                ///< File name or path (e.g., F"firmware.bin")
    TOKEN_STRING,            ///< String Token [partial string] to wait for (e.g., T"OK")
    TOKEN_HEXSTREAM,         ///< Hexstream Token [partial buffer] to wait for (e.g., X"CAFE00FF124C")
    LINE,                    ///< Line terminated with LF or CRLF (e.g., L"data")
    SIZEOF,                  ///< Number of bytes to read (e.g., S"256")
    STRING_DELIMITED,        ///< String with delimiters (e.g., "HelloWorld" or "Hello World" or "Hello || World")
    STRING_DELIMITED_EMPTY,  ///< Empty delimited string (e.g., "")
    STRING_RAW,              ///< Plain string without delimiters (e.g., HelloWorld / Hello World )
    ANYTHING,                ///< Receive anything found
    INVALID                  ///< Unrecognized or malformed token
};

/**
 * @brief Script token structure containing parsed command information
 */
struct CommCommand
{
    CommCommandDirection direction;                               ///< Send-Recv or Recv-Send
    std::pair<std::string, std::string> values;                   ///< First and second expression values
    std::pair<CommCommandTokenType, CommCommandTokenType> tokens; ///< First and second expression token types
    int iLineNumber;

    CommCommand()
        : direction(CommCommandDirection::INVALID)
        , values{"", ""}
        , tokens{CommCommandTokenType::INVALID, CommCommandTokenType::INVALID}
        , iLineNumber{0}
    {}
};

/**
 * @brief definition of storage structure for plugin tokens
 */
struct CommCommandsType
{
    std::vector<CommCommand> vCommands;
    std::unordered_map<std::string, std::string> mapMacros;
};

/**
 * @brief Read operation types for driver interface
 */
enum class CommCommandReadType
{
    DEFAULT,        ///< Read exact number of bytes
    LINE,           ///< Read until newline delimiter
    TOKEN_STRING,   ///< Read until specific string token is found
    TOKEN_HEXSTREAM ///< Read until specific hexstream token is found
};

// Helper functions for enum to string conversion
inline const char* getDirectionName(CommCommandDirection dir)
{
    switch (dir) {
        case CommCommandDirection::SEND_RECV: return "SEND_RECV";
        case CommCommandDirection::RECV_SEND: return "RECV_SEND";
        case CommCommandDirection::DELAY:     return "DELAY";
        case CommCommandDirection::INVALID:   return "INVALID";
        default:                              return "UNKNOWN";
    }
}

inline const char* getTokenTypeName(CommCommandTokenType type)
{
    switch (type) {
        case CommCommandTokenType::EMPTY:                  return "EMPTY";
        case CommCommandTokenType::HEXSTREAM:              return "HEXSTREAM";
        case CommCommandTokenType::REGEX:                  return "REGEX";
        case CommCommandTokenType::FILENAME:               return "FILENAME";
        case CommCommandTokenType::TOKEN_STRING:           return "TOKEN_STRING";
        case CommCommandTokenType::TOKEN_HEXSTREAM:        return "TOKEN_HEXSTREAM";
        case CommCommandTokenType::LINE:                   return "LINE";
        case CommCommandTokenType::SIZEOF:                 return "SIZEOF";
        case CommCommandTokenType::STRING_DELIMITED:       return "STRING_DELIMITED";
        case CommCommandTokenType::STRING_DELIMITED_EMPTY: return "STRING_DELIMITED_EMPTY";
        case CommCommandTokenType::STRING_RAW:             return "STRING_RAW";
        case CommCommandTokenType::ANYTHING:               return "ANYTHING";
        case CommCommandTokenType::INVALID:                return "INVALID";
        default:                                           return "UNKNOWN";
    }
}

#endif // U_COMM_SCRIPT_DATA_TYPES_HPP
