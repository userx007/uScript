#ifndef PLUGINITEMVALIDATOR_HPP
#define PLUGINITEMVALIDATOR_HPP

#include "CommonSettings.hpp"
#include "IItemValidator.hpp"

#include <iostream>
#include <regex>
#include <string>


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Represents the type of token parsed from input.
 */
/*--------------------------------------------------------------------------------------------------------*/

enum class Token
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


/////////////////////////////////////////////////////////////////////////////////
//                            CLASS IMPLEMENTATION                             //
/////////////////////////////////////////////////////////////////////////////////


class PluginScriptItemValidator : public IItemValidator<Token>
{
    public:

        bool validateItem ( const std::string& item, Token& token ) noexcept override
        {
            std::pair<std::string, std::string> result;

            splitAtFirstQuotedAware(item, CHAR_SEPARATOR_VERTICAL_BAR, result);
            return validateTokens(result);

        }

    private:

        Token GetTokenType (const std::string& strItem) const
        {
            if (true == strItem.empty()) {
                return eItemType = Token::EMPTY;
            }

            if (true == ustring::isDecoratedNonempty(strItem, std::string("H\""), std::string("\""))) {
                return eItemType = Token::HEXSTREAM;
            }

            if (true == ustring::isDecoratedNonempty(strItem, std::string("R\""), std::string("\""))) {
                return eItemType = Token::REGEX;
            }

            if (true == ustring::isDecoratedNonempty(strItem, std::string("F\""), std::string("\""))) {
                return eItemType = Token::FILENAME;
            }

            if (true == ustring::isDecoratedNonempty(strItem, std::string("\""), std::string("\""))) {
                return eItemType = Token::STRING_DELIMITED;
            }

            if (true == ustring::isDecorated(strItem, std::string("\""), std::string("\""))) {
                return eItemType = Token::STRING_DELIMITED_EMPTY;
            }
            return eItemType = Token::STRING_RAW;
        }

        bool ValidateTokens(std::pair<std::string, std::string> &result) const
        {
            Token sendToken   = GetTokenType(result.first);
            Token answerToken = GetTokenType(result.second);

            // reject wrong configurations
            if ( ((Token::EMPTY == sendToken) && (Token::EMPTY    == answerToken))   ||
                 ((Token::EMPTY == sendToken) && (Token::FILENAME == answerToken)) )
            {
                return false;
            }

            // validate the content where possible
            if( ((Token::HEXSTREAM == sendToken)   && (false == isHexlified(result.first))) ||
                ((Token::HEXSTREAM == answerToken) && (false == isHexlified(result.second))))
            {
                return false;
            }

            return true;
        }

        bool isHexlified(const std::string& input)
        {
            static const std::regex hexRegex("^(?:[0-9A-Fa-f]{2})+$");
            return std::regex_match(input, hexRegex);
        }
}



#endif // PLUGINITEMVALIDATOR_HPP