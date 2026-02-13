#ifndef COMMSCRIPTCOMMANDVALIDATOR_HPP
#define COMMSCRIPTCOMMANDVALIDATOR_HPP

#include "SharedSettings.hpp"
#include "CommScriptDataTypes.hpp"
#include "IScriptItemValidator.hpp"

#include "uString.hpp"
#include "uHexlify.hpp"
#include "uFile.hpp"
#include "uNumeric.hpp"
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

class CommScriptCommandValidator : public IScriptItemValidator<CommCommand>
{
    public:

        bool validateItem ( const std::string& item, CommCommand& token ) noexcept override
        {
            ItemParser itemParser;
            bool bRetVal = itemParser.parse(item, token);

            LOG_PRINT((bRetVal ? LOG_VERBOSE : LOG_ERROR), LOG_HDR; LOG_STRING(getDirectionName(token.direction)); LOG_STRING("|"); LOG_STRING(token.values.first); LOG_STRING("|"); LOG_STRING(token.values.second); LOG_STRING("| =>"); LOG_STRING(getTokenTypeName(token.tokens.first)); LOG_STRING("|"); LOG_STRING(getTokenTypeName(token.tokens.second)));
            return bRetVal;
        }

    private:

        class ItemParser
        {
            public:

                bool parse(std::string_view item, CommCommand& result)
                {
                    result = CommCommand{};

                    if (item.empty()) {
                        return false;
                    }

                    /* Determine direction */
                    char firstChar = item.front();
                    switch (firstChar) {
                        case '>': result.direction = CommCommandDirection::SEND_RECV; break;
                        case '<': result.direction = CommCommandDirection::RECV_SEND;  break;
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

                enum CommCommandTokenType getTokenType (std::string& strItem) const
                {
                    std::string strOutValue = "";
                    enum CommCommandTokenType outToken = CommCommandTokenType::INVALID;

                    do {
                        if (strItem.empty()) {
                            outToken = CommCommandTokenType::EMPTY;
                            break;
                        }

                        if (ustring::undecorate(strItem, DECORATOR_STRING_START, DECORATOR_ANY_END, strOutValue)) {
                            outToken = !strOutValue.empty() ? CommCommandTokenType::STRING_DELIMITED : CommCommandTokenType::STRING_DELIMITED_EMPTY;
                            break;
                        }

                        if (ustring::undecorate(strItem, DECORATOR_REGEX_START, DECORATOR_ANY_END, strOutValue)) {
                            outToken = !strOutValue.empty() ? CommCommandTokenType::REGEX : CommCommandTokenType::INVALID;
                            break;
                        }

                        if (ustring::undecorate(strItem, DECORATOR_TOKEN_START, DECORATOR_ANY_END, strOutValue)) {
                            outToken = !strOutValue.empty() ? CommCommandTokenType::TOKEN : CommCommandTokenType::INVALID;
                            break;
                        }

                        if (ustring::undecorate(strItem, DECORATOR_LINE_START, DECORATOR_ANY_END, strOutValue)) {
                            outToken = !strOutValue.empty() ? CommCommandTokenType::LINE : CommCommandTokenType::INVALID;
                            break;
                        }

                        if (ustring::undecorate(strItem, DECORATOR_SIZE_START, DECORATOR_ANY_END, strOutValue)) {
                            size_t szSize = 0;
                            outToken = !strOutValue.empty() && numeric::str2sizet(strOutValue, szSize) ? CommCommandTokenType::SIZE : CommCommandTokenType::INVALID;
                            break;
                        }

                        if (ustring::undecorate(strItem, DECORATOR_HEXLIFY_START, DECORATOR_ANY_END, strOutValue)) {
                            outToken = (!strOutValue.empty() && hexutils::isHexlified(strOutValue)) ? CommCommandTokenType::HEXSTREAM : CommCommandTokenType::INVALID;
                            break;
                        }

                        if (ustring::undecorate(strItem, DECORATOR_FILENAME_START, DECORATOR_ANY_END, strOutValue)) {
                            outToken = (!strOutValue.empty() && ufile::fileExistsAndNotEmpty(std::string(ustring::substringUntil(strOutValue, CHAR_SEPARATOR_COMMA)))) ? CommCommandTokenType::FILENAME : CommCommandTokenType::INVALID;
                            break;
                        }

                        if (!ustring::isValidTaggedOrPlainString(strItem)) {
                            outToken = CommCommandTokenType::INVALID;
                            break;
                        }

                         outToken = CommCommandTokenType::STRING_RAW;
                         strOutValue = strItem;

                    } while(false);

                    strItem.assign(strOutValue);
                    return outToken;

                } /* getTokenType () */


                bool evalItem (CommCommand& item)
                {
                    std::string strOutValue;

                    enum CommCommandTokenType firstToken  = getTokenType(item.values.first);
                    enum CommCommandTokenType secondToken = getTokenType(item.values.second);
                    enum CommCommandDirection direction   = item.direction;

                    item.tokens = std::make_pair(firstToken, secondToken);

                    // reject wrong configurations
                    if (((CommCommandTokenType::INVALID  == firstToken) || (CommCommandTokenType::INVALID   == secondToken)) ||  // any of them is invalid
                        ((CommCommandTokenType::FILENAME == firstToken) && (CommCommandDirection::RECV_SEND == direction))   ||  // can't receive a file
                        ((CommCommandTokenType::TOKEN    == firstToken) && (CommCommandDirection::SEND_RECV == direction))   ||  // can't send a token
                        ((CommCommandTokenType::SIZE     == firstToken) && (CommCommandDirection::SEND_RECV == direction))   ||  // can't send a size
                        ((CommCommandTokenType::REGEX    == firstToken) && (CommCommandDirection::SEND_RECV == direction))   ||  // can't send a regex
                        ((CommCommandTokenType::EMPTY    == firstToken) && (CommCommandDirection::SEND_RECV == direction))   ||  // can't send an empty
                        ((CommCommandTokenType::EMPTY    == firstToken) && (CommCommandDirection::RECV_SEND == direction))   ||  // can't receive an empty
                        ((CommCommandTokenType::EMPTY    == firstToken) && (CommCommandTokenType::EMPTY     == secondToken)))    // both empty
                    {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid request!"));
                        return false;
                    }
                    return true;

                } /* evalitem() */

        }; /* class ItemParser  { ... } */

}; /* class CommScriptCommandValidator { ... } */



#endif // COMMSCRIPTCOMMANDVALIDATOR_HPP