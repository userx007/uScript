#ifndef ULINEPARSER_HPP
#define ULINEPARSER_HPP

#include <string>
#include <string_view>
#include <utility>
#include <cctype>
#include <algorithm>

enum class Direction {
    INPUT,
    OUTPUT,
    INVALID
};

// the type of token parsed from input
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

struct ParsedLine
{
    Direction direction = Direction::INVALID;
    std::pair<std::string, std::string> values = {"", ""};
    std::pair<TokenType, TokenType> tokens     = {TokenType::INVALID, TokenType::INVALID};
};

class LineParser
{
    public:

        bool parse(std::string_view line, ParsedLine& result) const
        {
            result = ParsedLine{};

            if (line.empty()) return false;

            // Determine direction
            char firstChar = line.front();
            switch (firstChar) {
                case '>': result.direction = Direction::OUTPUT; break;
                case '<': result.direction = Direction::INPUT;  break;
                default: return false;
            }

            line.remove_prefix(1);
            skipWhitespace(line);

            std::string field1, field2;
            bool insideQuote = false;
            bool separatorFound = false;

            for (char ch : line) {
                if (ch == '"') {
                    insideQuote = !insideQuote;
                } else if (ch == '|' && !insideQuote) {
                    if (separatorFound) return false;  // Multiple separators
                    separatorFound = true;
                } else {
                    (separatorFound ? field2 : field1) += ch;
                }
            }

            trim(field1);
            trim(field2);

            // Invalid if separator exists but one side is empty
            if ((separatorFound && field1.empty()) || (separatorFound && field2.empty())) {
                return false;
            }

            result.values = std::make_pair(field1, field2);
            return evalLine(result.values, result.tokens);
        }

    private:

        static void skipWhitespace(std::string_view& sv)
        {
            while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())))
                sv.remove_prefix(1);
        }

        static void trim(std::string& s)
        {
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c){ return !std::isspace(c); }));
            s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c){ return !std::isspace(c); }).base(), s.end());
        }

        static const std::string& getTokenName(TokenType type) const
        {
            switch(type)
            {
                case TokenType::EMPTY:                  { static const std::string name = "EMPTY";                   return name; }
                case TokenType::HEXSTREAM:              { static const std::string name = "HEXSTREAM";               return name; }
                case TokenType::REGEX:                  { static const std::string name = "REGEX";                   return name; }
                case TokenType::TOKEN   :               { static const std::string name = "TOKEN";                   return name; }
                case TokenType::FILENAME:               { static const std::string name = "FILENAME";                return name; }
                case TokenType::STRING_DELIMITED:       { static const std::string name = "STRING_DELIMITED";        return name; }
                case TokenType::STRING_DELIMITED_EMPTY: { static const std::string name = "STRING_DELIMITED_EMPTY";  return name; }
                case TokenType::STRING_RAW:             { static const std::string name = "STRING_RAW";              return name; }
                case TokenType::INVALID:                { static const std::string name = "INVALID";                 return name; }
                default:                                { static const std::string name = "UNKNOWN";                 return name; }
            }
        }

        TokenType getTokenType (const std::string& strItem) const
        {
            if (strItem.empty()) {
                return TokenType::EMPTY;
            }

            if (ustring::isDecoratedNonempty(strItem, DECORATOR_STRING_START, DECORATOR_ANY_END)) {
                return TokenType::STRING_DELIMITED;
            }

            if (ustring::isDecorated(strItem, DECORATOR_STRING_START, DECORATOR_ANY_END)) {
                return TokenType::STRING_DELIMITED_EMPTY;
            }

            std::string output;

            if (ustring::undecorate(strItem, DECORATOR_REGEX_START, DECORATOR_ANY_END, output)) {
                return output.empty() ? TokenType::INVALID : TokenType::REGEX;
            }

            if (ustring::undecorate(strItem, DECORATOR_TOKEN_START, DECORATOR_ANY_END, output)) {
                return output.empty() ? TokenType::INVALID : TokenType::TOKEN;
            }

            if (ustring::undecorate(strItem, DECORATOR_HEXLIFY_START, DECORATOR_ANY_END, output)) {
                return (!output.empty() && hexutils::isHexlified(output)) ? TokenType::HEXSTREAM : TokenType::INVALID;
            }

            if (ustring::undecorate(strItem, DECORATOR_FILENAME_START, DECORATOR_ANY_END, output)) {
                return (!output.empty() && ufile::fileExistsAndNotEmpty(std::string(ustring::substringUntil(output, CHAR_SEPARATOR_COMMA)))) ? TokenType::FILENAME : TokenType::INVALID;
            }

            if (!ustring::isValidTaggedOrPlainString(strItem)) {
                return TokenType::INVALID;
            }

            return TokenType::STRING_RAW;

        } /* getTokenType () */

        bool evalLine (ParsedLine& line)
        {
            enum TokenType firstToken  = getTokenType(line.values.first);
            enum TokenType secondToken = getTokenType(line.values.second);
            enum Direction direction   = line.direction;

            line.tokens = std::make_pair(firstToken, secondToken);

            // reject wrong configurations
            if (((TokenType::INVALID  == firstToken) || (TokenType::INVALID == secondToken)) ||  // any of them is invalid
                ((TokenType::FILENAME == firstToken) && (Direction::INPUT   == direction))   ||  // can't receive a file
                ((TokenType::TOKEN    == firstToken) && (Direction::OUTPUT  == direction))   ||  // can't send a token
                ((TokenType::REGEX    == firstToken) && (Direction::OUTPUT  == direction))   ||  // can't send a regex
                ((TokenType::EMPTY    == firstToken) && (TokenType::EMPTY   == secondToken))     // both empty
               )
            {
                return false;
            }
            return true;

        } /* evalLine() */

};

#endif // ULINEPARSER_HPP