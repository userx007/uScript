#ifndef PLUGINSCRIPTITEMVALIDATOR_HPP
#define PLUGINSCRIPTITEMVALIDATOR_HPP

#include "CommonSettings.hpp"
#include "IItemValidator.hpp"

#include "uString.hpp"
#include "uHexlify.hpp"
#include "uFile.hpp"
#include "uLogger.hpp"

#include <iostream>
#include <regex>
#include <string>


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////


#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "PSITEMVALID:"
#define LOG_HDR    LOG_STRING(LT_HDR)



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
            bool bRetVal = validateTokens(result, token);

            LOG_PRINT((bRetVal ? LOG_VERBOSE : LOG_ERROR), LOG_HDR; LOG_STRING(item); LOG_STRING("=>"); LOG_STRING(getTokenName(token.first)); LOG_STRING(getTokenName(token.second)));

            return bRetVal;
        }

    private:

        bool validateTokens(std::pair<std::string, std::string> &result, PToken& token) const
        {
            TokenType sendToken   = GetTokenType(result.first);
            TokenType answerToken = GetTokenType(result.second);

            // reject wrong configurations
            if (  (TokenType::REGEX    == sendToken)   ||
                  (TokenType::FILENAME == answerToken) ||
                 ((TokenType::EMPTY    == sendToken) && (TokenType::EMPTY    == answerToken))
               )
            {
                return false;
            }

            // validate the content where possible
            if( ((TokenType::HEXSTREAM == sendToken)   && (false == hexutils::isHexlified(result.first))) ||
                ((TokenType::HEXSTREAM == answerToken) && (false == hexutils::isHexlified(result.second))) ||
                ((TokenType::FILENAME  == sendToken)   && (false == ufile::fileExistsAndNotEmpty(result.first))) )
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


        const std::string& getTokenName(TokenType type)
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

};



#endif // PLUGINSCRIPTITEMVALIDATOR_HPP