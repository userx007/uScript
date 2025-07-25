#ifndef PLUGINSCRIPTITEMVALIDATOR_HPP
#define PLUGINSCRIPTITEMVALIDATOR_HPP

#include "CommonSettings.hpp"
#include "IScriptItemValidator.hpp"
#include "PluginScriptDataTypes.hpp"

#include "uString.hpp"
#include "uHexlify.hpp"
#include "uFile.hpp"
#include "uFileChunkReader.hpp"
#include "uLogger.hpp"

#include <cctype>
#include <regex>
#include <string>
#include <utility>
#include <string_view>
#include <algorithm>

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

class PluginScriptItemValidator : public IScriptItemValidator<PToken>
{
    public:

        bool validateItem ( const std::string& item, PToken& token ) noexcept override
        {
            ItemParser itemParser;
            bool bRetVal = itemParser.parse(item, token);

            LOG_PRINT((bRetVal ? LOG_VERBOSE : LOG_ERROR), LOG_HDR; LOG_STRING(getDirName(token.direction)); LOG_STRING("|"); LOG_STRING(token.values.first); LOG_STRING("|"); LOG_STRING(token.values.second); LOG_STRING("| =>"); LOG_STRING(getTokenName(token.tokens.first)); LOG_STRING("|"); LOG_STRING(getTokenName(token.tokens.second)));
            return bRetVal;
        }

    private:

        class ItemParser
        {
            public:

                bool parse(std::string_view item, PToken& result)
                {
                    result = PToken{};

                    if (item.empty()) {
                        return false;
                    }

                    /* Determine direction */
                    char firstChar = item.front();
                    switch (firstChar) {
                        case '>': result.direction = Direction::SEND_RECV; break;
                        case '<': result.direction = Direction::RECV_SEND;  break;
                        default: return false;
                    }

                    item.remove_prefix(1);
                    ustring::skipWhitespace(item);

                    std::string field1, field2;
                    bool insideQuote = false;
                    bool separatorFound = false;

                    for (char ch : item) {
                        if (ch == '"') {
                            /* Preserve quotes as characters */
                            (separatorFound ? field2 : field1) += ch;
                            insideQuote = !insideQuote;
                        } else if (ch == '|' && !insideQuote) {
                            if (separatorFound) {
                                return false;  /* Multiple separators outside quotes */
                            }
                            separatorFound = true;
                        } else {
                            (separatorFound ? field2 : field1) += ch;
                        }
                    }

                    ustring::trimInPlace(field1);
                    ustring::trimInPlace(field2);

                    if ((separatorFound && field1.empty()) || (separatorFound && field2.empty())) {
                        return false;
                    }

                    result.values = std::make_pair(field1, field2);
                    return evalItem(result);

                } /* parse() */


            private:

                TokenType getTokenType (std::string& strItem) const
                {
                    std::string strOutValue = "";
                    TokenType outToken = TokenType::INVALID;

                    do {
                        if (strItem.empty()) {
                            outToken = TokenType::EMPTY;
                            break;
                        }

                        if (ustring::undecorate(strItem, DECORATOR_STRING_START, DECORATOR_ANY_END, strOutValue)) {
                            outToken = !strOutValue.empty() ? TokenType::STRING_DELIMITED : TokenType::STRING_DELIMITED_EMPTY;
                            break;
                        }

                        if (ustring::undecorate(strItem, DECORATOR_REGEX_START, DECORATOR_ANY_END, strOutValue)) {
                            outToken = !strOutValue.empty() ? TokenType::REGEX : TokenType::INVALID;
                            break;
                        }

                        if (ustring::undecorate(strItem, DECORATOR_TOKEN_START, DECORATOR_ANY_END, strOutValue)) {
                            outToken = !strOutValue.empty() ? TokenType::TOKEN : TokenType::INVALID;
                            break;
                        }

                        if (ustring::undecorate(strItem, DECORATOR_LINE_START, DECORATOR_ANY_END, strOutValue)) {
                            outToken = !strOutValue.empty() ? TokenType::LINE : TokenType::INVALID;
                            break;
                        }

                        if (ustring::undecorate(strItem, DECORATOR_HEXLIFY_START, DECORATOR_ANY_END, strOutValue)) {
                            outToken = (!strOutValue.empty() && hexutils::isHexlified(strOutValue)) ? TokenType::HEXSTREAM : TokenType::INVALID;
                            break;
                        }

                        if (ustring::undecorate(strItem, DECORATOR_FILENAME_START, DECORATOR_ANY_END, strOutValue)) {
                            outToken = (!strOutValue.empty() && ufile::fileExistsAndNotEmpty(std::string(ustring::substringUntil(strOutValue, CHAR_SEPARATOR_COMMA)))) ? TokenType::FILENAME : TokenType::INVALID;
                            break;
                        }

                        if (!ustring::isValidTaggedOrPlainString(strItem)) {
                            outToken = TokenType::INVALID;
                            break;
                        }

                         outToken = TokenType::STRING_RAW;
                         strOutValue = strItem;

                    } while(false);

                    strItem.assign(strOutValue);
                    return outToken;

                } /* getTokenType () */


                bool evalItem (PToken& item)
                {
                    std::string strOutValue;

                    enum TokenType firstToken  = getTokenType(item.values.first);
                    enum TokenType secondToken = getTokenType(item.values.second);
                    enum Direction direction   = item.direction;

                    item.tokens = std::make_pair(firstToken, secondToken);

                    // reject wrong configurations
                    if (((TokenType::INVALID  == firstToken) || (TokenType::INVALID   == secondToken)) ||  // any of them is invalid
                        ((TokenType::FILENAME == firstToken) && (Direction::RECV_SEND == direction))   ||  // can't receive a file
                        ((TokenType::TOKEN    == firstToken) && (Direction::SEND_RECV == direction))   ||  // can't send a token
                        ((TokenType::REGEX    == firstToken) && (Direction::SEND_RECV == direction))   ||  // can't send a regex
                        ((TokenType::EMPTY    == firstToken) && (Direction::SEND_RECV == direction))   ||  // can't send an empty
                        ((TokenType::EMPTY    == firstToken) && (Direction::RECV_SEND == direction))   ||  // can't receive an empty
                        ((TokenType::EMPTY    == firstToken) && (TokenType::EMPTY     == secondToken)))    // both empty
                    {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid request!"));
                        return false;
                    }
                    return true;

                } /* evalitem() */

        }; /* class ItemParser  { ... } */

}; /* class PluginScriptItemValidator { ... } */



#endif // PLUGINSCRIPTITEMVALIDATOR_HPP