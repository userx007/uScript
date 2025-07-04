#ifndef PLUGINSCRIPTITEMVALIDATOR_HPP
#define PLUGINSCRIPTITEMVALIDATOR_HPP

#include "CommonSettings.hpp"
#include "IItemValidator.hpp"

#include "uString.hpp"
#include "uHexlify.hpp"

#include <iostream>
#include <regex>
#include <string>


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Represents the type of token parsed from input.
 */
/*--------------------------------------------------------------------------------------------------------*/

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


/////////////////////////////////////////////////////////////////////////////////
//                            CLASS IMPLEMENTATION                             //
/////////////////////////////////////////////////////////////////////////////////


class PluginScriptItemValidator : public IItemValidator<PToken>
{
    public:

        bool validateItem ( const std::string& item, PToken& token ) noexcept override
        {
            std::pair<std::string, std::string> result;

            ustring::splitAtFirstQuotedAware(item, CHAR_SEPARATOR_VERTICAL_BAR, result);
            return validateTokens(result, token);

        }

    private:

        bool validateTokens(std::pair<std::string, std::string> &result, PToken& token) const
        {
            TokenType sendToken   = GetTokenType(result.first);
            TokenType answerToken = GetTokenType(result.second);

            // reject wrong configurations
            if ( ((TokenType::EMPTY == sendToken) && (TokenType::EMPTY    == answerToken))   ||
                 ((TokenType::EMPTY == sendToken) && (TokenType::FILENAME == answerToken)) )
            {
                return false;
            }

            // validate the content where possible
            if( ((TokenType::HEXSTREAM == sendToken)   && (false == hexutils::isHexlified(result.first))) ||
                ((TokenType::HEXSTREAM == answerToken) && (false == hexutils::isHexlified(result.second))))
            {
                return false;
            }

            token = std::make_pair(sendToken, answerToken);

            return true;
        }


        TokenType GetTokenType (const std::string& strItem) const
        {
            if (true == strItem.empty()) {
                return TokenType::EMPTY;
            }

            if (true == ustring::isDecoratedNonempty(strItem, std::string("H\""), std::string("\""))) {
                return TokenType::HEXSTREAM;
            }

            if (true == ustring::isDecoratedNonempty(strItem, std::string("R\""), std::string("\""))) {
                return TokenType::REGEX;
            }

            if (true == ustring::isDecoratedNonempty(strItem, std::string("F\""), std::string("\""))) {
                return TokenType::FILENAME;
            }

            if (true == ustring::isDecoratedNonempty(strItem, std::string("\""), std::string("\""))) {
                return TokenType::STRING_DELIMITED;
            }

            if (true == ustring::isDecorated(strItem, std::string("\""), std::string("\""))) {
                return TokenType::STRING_DELIMITED_EMPTY;
            }

            return TokenType::STRING_RAW;
        }

};



#endif // PLUGINSCRIPTITEMVALIDATOR_HPP