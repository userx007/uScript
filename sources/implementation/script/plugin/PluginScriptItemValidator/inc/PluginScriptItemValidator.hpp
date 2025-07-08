#ifndef PLUGINSCRIPTITEMVALIDATOR_HPP
#define PLUGINSCRIPTITEMVALIDATOR_HPP

#include "CommonSettings.hpp"
#include "IItemValidator.hpp"
#include "PluginScriptDataTypes.hpp"

#include "uString.hpp"
#include "uHexlify.hpp"
#include "uFile.hpp"
#include "uFileChunkReader.hpp"
#include "uLogger.hpp"

#include <iostream>
#include <regex>
#include <string>
#include <utility>


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

            LOG_PRINT((bRetVal ? LOG_VERBOSE : LOG_ERROR), LOG_HDR; LOG_STRING(result.first); LOG_STRING("|"); LOG_STRING(result.second); LOG_STRING("=>"); LOG_STRING(getTokenName(token.first)); LOG_STRING("|") LOG_STRING(getTokenName(token.second)));

            return bRetVal;
        }

    private:

        bool validateTokens(std::pair<std::string, std::string> &result, PToken& token) const
        {
            enum TokenType sendToken = GetTokenType(result.first);
            enum TokenType recvToken = GetTokenType(result.second);

            token = std::make_pair(sendToken, recvToken);

            // reject wrong configurations
            if (  (TokenType::INVALID  == sendToken) || (TokenType::INVALID  == recvToken) ||
                  (TokenType::REGEX    == sendToken) ||
                  (TokenType::FILENAME == recvToken) ||
                 ((TokenType::EMPTY    == sendToken) && (TokenType::EMPTY == recvToken))
               )
            {
                return false;
            }

            return true;
        }


        TokenType GetTokenType(const std::string& strItem) const
        {
            if (strItem.empty()) {
                return TokenType::EMPTY;
            }

            if (ustring::isDecoratedNonempty(strItem, DECORATOR_STRING, DECORATOR_STRING)) {
                return TokenType::STRING_DELIMITED;
            }

            if (ustring::isDecorated(strItem, DECORATOR_STRING, DECORATOR_STRING)) {
                return TokenType::STRING_DELIMITED_EMPTY;
            }

            std::string output;

            if (ustring::undecorate(strItem, DECORATOR_REGEX_START, DECORATOR_ANY_END, output)) {
                return output.empty() ? TokenType::INVALID : TokenType::REGEX;
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
        }
};



#endif // PLUGINSCRIPTITEMVALIDATOR_HPP