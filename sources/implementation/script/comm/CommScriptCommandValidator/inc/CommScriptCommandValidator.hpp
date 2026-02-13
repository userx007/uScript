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

/**
 * @brief Validator for communication script commands
 * 
 * Validates commands in the format:
 *   > EXPRESSION1 | EXPRESSION2  (send then receive)
 *   < EXPRESSION1 | EXPRESSION2  (receive then send)
 *   > EXPRESSION1                (send only)
 *   < EXPRESSION1                (receive only)
 * 
 * Expressions can be decorated with:
 *   F"filename.bin"  - File (must exist and be non-empty)
 *   R"pattern.*"     - Regex (validated pattern)
 *   H"4A6F686E"      - Hex stream (validated hex string)
 *   T"OK"            - Token
 *   L"data"          - Line
 *   S"256"           - Size (validated numeric)
 *   "hello"          - Delimited string
 *   raw_string       - Raw string (no quotes)
 */
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

                /**
                 * @brief Parse and validate a command item
                 * @param item The command string to parse
                 * @param result Output parameter containing parsed command
                 * @return true if parsing and validation succeeded, false otherwise
                 */
                bool parse(std::string_view item, CommCommand& result)
                {
                    result = CommCommand{};

                    if (item.empty()) {
                        return false;
                    }

                    /* Determine direction from first character */
                    if (!parseDirection(item, result.direction)) {
                        return false;
                    }

                    /* Skip direction character and leading whitespace */
                    item.remove_prefix(1);
                    ustring::skipWhitespace(item);

                    /* Split into two fields by pipe separator (respecting quotes) */
                    std::string field1, field2;
                    bool separatorFound = false;
                    
                    if (!splitFields(item, field1, field2, separatorFound)) {
                        return false;
                    }

                    /* Validate field presence */
                    if ((separatorFound && field1.empty()) || (separatorFound && field2.empty())) {
                        return false;
                    }

                    result.values = std::make_pair(field1, field2);
                    return evaluateAndValidate(result);

                } /* parse() */


            private:

                /**
                 * @brief Parse direction indicator from command
                 * @param item Command string (must start with > or <)
                 * @param direction Output parameter for parsed direction
                 * @return true if valid direction found
                 */
                bool parseDirection(std::string_view item, CommCommandDirection& direction) const
                {
                    char firstChar = item.front();
                    switch (firstChar) {
                        case '>': 
                            direction = CommCommandDirection::SEND_RECV; 
                            return true;
                        case '<': 
                            direction = CommCommandDirection::RECV_SEND;
                            return true;
                        default: 
                            return false;
                    }
                }

                /**
                 * @brief Split command into two fields by pipe separator
                 * @param item Command string to split
                 * @param field1 Output first field
                 * @param field2 Output second field
                 * @param separatorFound Output whether separator was found
                 * @return true if split successful
                 * 
                 * Handles quoted strings correctly - pipes inside quotes are preserved
                 */
                bool splitFields(std::string_view item, std::string& field1, std::string& field2, bool& separatorFound) const
                {
                    bool insideQuote = false;

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

                    /* Trim whitespace from both fields */
                    ustring::trimInPlace(field1);
                    ustring::trimInPlace(field2);
                    
                    return true;
                }

                /**
                 * @brief Determine token type for an expression with validation
                 * @param strItem Input/output parameter - expression string (modified to contain extracted value)
                 * @return Detected token type with validation
                 * 
                 * Validates:
                 * - File existence and non-empty for FILENAME type
                 * - Valid hex string for HEXSTREAM type
                 * - Valid numeric for SIZE type
                 * - Valid regex pattern for REGEX type
                 */
                CommCommandTokenType getTokenType(std::string& strItem) const
                {
                    std::string strOutValue;
                    CommCommandTokenType outToken = CommCommandTokenType::INVALID;

                    do {
                        /* Empty expression */
                        if (strItem.empty()) {
                            outToken = CommCommandTokenType::EMPTY;
                            break;
                        }

                        /* Delimited string: "content" or "" */
                        if (ustring::undecorate(strItem, DECORATOR_STRING_START, DECORATOR_ANY_END, strOutValue)) {
                            outToken = !strOutValue.empty() ? CommCommandTokenType::STRING_DELIMITED : CommCommandTokenType::STRING_DELIMITED_EMPTY;
                            break;
                        }

                        /* Regex pattern: R"pattern" - validate that pattern is non-empty */
                        if (ustring::undecorate(strItem, DECORATOR_REGEX_START, DECORATOR_ANY_END, strOutValue)) {
                            outToken = !strOutValue.empty() ? CommCommandTokenType::REGEX : CommCommandTokenType::INVALID;
                            break;
                        }

                        /* Token: T"value" - validate that value is non-empty */
                        if (ustring::undecorate(strItem, DECORATOR_TOKEN_START, DECORATOR_ANY_END, strOutValue)) {
                            outToken = !strOutValue.empty() ? CommCommandTokenType::TOKEN : CommCommandTokenType::INVALID;
                            break;
                        }

                        /* Line: L"content" - validate that content is non-empty */
                        if (ustring::undecorate(strItem, DECORATOR_LINE_START, DECORATOR_ANY_END, strOutValue)) {
                            outToken = !strOutValue.empty() ? CommCommandTokenType::LINE : CommCommandTokenType::INVALID;
                            break;
                        }

                        /* Size: S"number" - validate numeric and non-empty */
                        if (ustring::undecorate(strItem, DECORATOR_SIZE_START, DECORATOR_ANY_END, strOutValue)) {
                            size_t szSize = 0;
                            outToken = (!strOutValue.empty() && numeric::str2sizet(strOutValue, szSize)) ? CommCommandTokenType::SIZE : CommCommandTokenType::INVALID;
                            break;
                        }

                        /* Hex stream: H"hexstring" - validate hex format */
                        if (ustring::undecorate(strItem, DECORATOR_HEXLIFY_START, DECORATOR_ANY_END, strOutValue)) {
                            outToken = (!strOutValue.empty() && hexutils::isHexlified(strOutValue)) ? CommCommandTokenType::HEXSTREAM : CommCommandTokenType::INVALID;
                            break;
                        }

                        /* File: F"filename.bin" or F"filename.bin,options" - validate file exists and is non-empty */
                        if (ustring::undecorate(strItem, DECORATOR_FILENAME_START, DECORATOR_ANY_END, strOutValue)) {
                            /* Extract filename part (before optional comma-separated options) */
                            std::string filename = std::string(ustring::substringUntil(strOutValue, CHAR_SEPARATOR_COMMA));
                            outToken = (!strOutValue.empty() && ufile::fileExistsAndNotEmpty(filename)) ? CommCommandTokenType::FILENAME : CommCommandTokenType::INVALID;
                            break;
                        }

                        /* Validate raw string format */
                        if (!ustring::isValidTaggedOrPlainString(strItem)) {
                            outToken = CommCommandTokenType::INVALID;
                            break;
                        }

                        /* Raw undecorated string */
                        outToken = CommCommandTokenType::STRING_RAW;
                        strOutValue = strItem;

                    } while(false);

                    strItem.assign(strOutValue);
                    return outToken;

                } /* getTokenType() */


                /**
                 * @brief Evaluate and validate command semantics
                 * @param item Command to validate
                 * @return true if command configuration is valid
                 * 
                 * Validates rules such as:
                 * - Cannot send tokens (only receive them)
                 * - Cannot send regex patterns (only receive/match them)
                 * - Cannot send size specifiers (only receive with size)
                 * - Cannot receive files (only send them)
                 * - Cannot have both fields empty
                 * - Cannot send or receive empty expressions
                 */
                bool evaluateAndValidate(CommCommand& item)
                {
                    /* Determine token types for both expressions */
                    CommCommandTokenType firstToken  = getTokenType(item.values.first);
                    CommCommandTokenType secondToken = getTokenType(item.values.second);
                    CommCommandDirection direction   = item.direction;

                    item.tokens = std::make_pair(firstToken, secondToken);

                    /* Validation rules for invalid configurations */
                    if (firstToken == CommCommandTokenType::INVALID || secondToken == CommCommandTokenType::INVALID) {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid token type detected"));
                        return false;
                    }

                    /* Direction-specific validation rules */
                    if (direction == CommCommandDirection::SEND_RECV) {
                        /* SEND operations - validate what cannot be sent */
                        if (firstToken == CommCommandTokenType::TOKEN ||
                            firstToken == CommCommandTokenType::SIZE  ||
                            firstToken == CommCommandTokenType::REGEX ||
                            firstToken == CommCommandTokenType::EMPTY) {
                            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Cannot send TOKEN, SIZE, REGEX, or EMPTY"));
                            return false;
                        }
                    } else if (direction == CommCommandDirection::RECV_SEND) {
                        /* RECEIVE operations - validate what cannot be received */
                        if (firstToken == CommCommandTokenType::FILENAME ||
                            firstToken == CommCommandTokenType::EMPTY) {
                            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Cannot receive FILENAME or EMPTY"));
                            return false;
                        }
                    }

                    /* Both fields cannot be empty */
                    if (firstToken == CommCommandTokenType::EMPTY && secondToken == CommCommandTokenType::EMPTY) {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Both fields cannot be empty"));
                        return false;
                    }

                    return true;

                } /* evaluateAndValidate() */

        }; /* class ItemParser */

}; /* class CommScriptCommandValidator */



#endif // COMMSCRIPTCOMMANDVALIDATOR_HPP
