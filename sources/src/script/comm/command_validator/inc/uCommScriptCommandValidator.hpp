#ifndef COMMSCRIPTCOMMANDVALIDATOR_HPP
#define COMMSCRIPTCOMMANDVALIDATOR_HPP

#include "uSharedConfig.hpp"
#include "uCommScriptDataTypes.hpp"
#include "IScriptCommandValidator.hpp"

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

#define LT_HDR     "COMM_CMD_V  |"
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
 *   @ MESSAGE                    (log MESSAGE at INFO severity, no I/O)
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
class CommScriptCommandValidator : public IScriptCommandValidator<CommCommand>
{
    public:

        bool validateCommand (int iLineNumber, const std::string& command, CommCommand& token ) noexcept override
        {
            ItemParser itemParser;
            bool bRetVal = itemParser.parse(command, token);
            token.iLineNumber = iLineNumber;

            auto lineNr = ustring::fmtLineNr(iLineNumber);
            LOG_PRINT((bRetVal ? LOG_VERBOSE : LOG_ERROR), LOG_HDR; LOG_STRING(lineNr.data());
                        LOG_STRING(getDirectionName(token.direction)); 
                        LOG_STRING("["); LOG_STRING(token.values.first); 
                        LOG_STRING(":"); LOG_STRING(token.values.second); 
                        LOG_STRING("]=["); LOG_STRING(getTokenTypeName(token.tokens.first)); 
                        LOG_STRING(":"); LOG_STRING(getTokenTypeName(token.tokens.second));
                        LOG_STRING("] xtra=["); LOG_STRING(token.xtra_params.first);
                        LOG_STRING(":"); LOG_STRING(token.xtra_params.second);
                        LOG_STRING("]"));
            return bRetVal;
        }

    private:

        class ItemParser
        {
            public:

                /**
                 * @brief Parse and validate a command command
                 * @param command The command string to parse
                 * @param result Output parameter containing parsed command
                 * @return true if parsing and validation succeeded, false otherwise
                 */
                bool parse(std::string_view command, CommCommand& result)
                {
                    result = CommCommand{};

                    if (command.empty()) {
                        return false;
                    }

                    /* Determine direction from first character */
                    if (!parseDirection(command, result.direction)) {
                        return false;
                    }

                    /* Skip direction character and leading whitespace */
                    command.remove_prefix(1);
                    ustring::skipWhitespace(command);

                    std::string field1, field2;
                    bool separatorFound = false;

                    /* if print message: take the entire remainder of the line verbatim
                     * (leading whitespace already skipped above), trim trailing
                     * whitespace, and bypass pipe-splitting / xtra_params / the
                     * send-recv semantic rules entirely - a print statement is just
                     * a message. Macro substitution ($NAME) already happened on the
                     * raw line in CommScriptValidator::validateScript() before this
                     * parse() call, so any macros are already expanded here. */
                    if (result.direction == CommCommandDirection::PRINT) {
                        std::string message(command);
                        ustring::trimInPlace(message);

                        if (message.empty()) {
                            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Empty message for '@' print command"));
                            return false;
                        }

                        result.values = std::make_pair(message, std::string{});
                        result.tokens = std::make_pair(CommCommandTokenType::STRING_RAW, CommCommandTokenType::EMPTY);
                        return true;
                    }

                    /* if delay */
                    if (result.direction == CommCommandDirection::DELAY) {
                        if (command.empty() || !ustring::splitValueUnit(command, std::array<std::string_view, 3>{TIME_MICROSECONDS, TIME_MILISECONDS, TIME_SECONDS}, field1, field2)) {
                            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid delay value/format"));
                            return false;
                        }
                    /* command */
                    } else {
                        /* Strip optional '~ xtra_params' suffix (outside quotes) before field splitting */
                        std::string xtraRaw;
                        std::string commandBody;
                        if (!splitXtraParams(command, commandBody, xtraRaw)) {
                            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid xtra_params suffix"));
                            return false;
                        }

                        std::string_view bodyView(commandBody);

                        /* Split into two fields by pipe separator (respecting quotes) */
                        if (!splitFields(bodyView, field1, field2, separatorFound)) {
                            return false;
                        }

                        /* Validate field presence */
                        if (separatorFound && field1.empty()) {
                            return false;
                        }

                        /* Parse '~ param' or '~ param1 / param2' */
                        if (!xtraRaw.empty()) {
                            if (!parseXtraParamsSuffix(xtraRaw, separatorFound, result.xtra_params)) {
                                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid xtra_params format"));
                                return false;
                            }
                        }
                    }

                    result.values = std::make_pair(field1, field2);
                    return evaluateAndValidate(result, separatorFound);

                } /* parse() */


            private:

                /**
                 * @brief Parse direction indicator from command
                 * @param command Command string (must start with >, < or !)
                 * @param direction Output parameter for parsed direction
                 * @return true if valid direction found
                 */
                bool parseDirection(std::string_view command, CommCommandDirection& direction) const
                {
                    char firstChar = command.front();
                    switch (firstChar) {
                        case '>': 
                            direction = CommCommandDirection::SEND_RECV; 
                            return true;
                        case '<': 
                            direction = CommCommandDirection::RECV_SEND;
                            return true;
                        case '!':
                            direction = CommCommandDirection::DELAY;
                            return true;
                        case '@':
                            direction = CommCommandDirection::PRINT;
                            return true;
                        default: 
                            return false;
                    }
                }

                /**
                 * @brief Split command into two fields by pipe separator
                 * @param command Command string to split
                 * @param field1 Output first field
                 * @param field2 Output second field
                 * @param separatorFound Output whether separator was found
                 * @return true if split successful
                 * 
                 * Handles quoted strings correctly - pipes inside quotes are preserved
                 */
                bool splitFields(std::string_view command, std::string& field1, std::string& field2, bool& separatorFound) const
                {
                    bool insideQuote = false;

                    for (char ch : command) {
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
                 * @brief Split command body from optional '~ xtra_params' suffix
                 * @param command Full command string (after direction char was removed)
                 * @param body    Output: portion before '~' (trimmed)
                 * @param xtraRaw Output: raw text after '~' (trimmed), empty when absent
                 * @return true always (malformed tilde is caught later by parseXtraParamsSuffix)
                 * 
                 * The tilde character is only a separator when it appears outside quotes.
                 * A quoted '~' inside H"..." / "..." / etc. is treated as data.
                 */
                bool splitXtraParams(std::string_view command, std::string& body, std::string& xtraRaw) const
                {
                    bool insideQuote = false;
                    std::size_t tildePos = std::string_view::npos;

                    for (std::size_t i = 0; i < command.size(); ++i) {
                        char ch = command[i];
                        if (ch == '"') {
                            insideQuote = !insideQuote;
                        } else if (ch == '~' && !insideQuote) {
                            tildePos = i;
                            break;
                        }
                    }

                    if (tildePos == std::string_view::npos) {
                        body    = std::string(command);
                        xtraRaw = "";
                    } else {
                        body    = std::string(command.substr(0, tildePos));
                        xtraRaw = std::string(command.substr(tildePos + 1));
                        ustring::trimInPlace(body);
                        ustring::trimInPlace(xtraRaw);
                    }
                    return true;
                }

                /**
                 * @brief Parse the raw text after '~' into xtra_params for each operation
                 * @param xtraRaw     Trimmed text that followed '~' in the command
                 * @param dualOp      true when the command has a '|' separator (two operations)
                 * @param xtraParams  Output pair: (first_op_param, second_op_param)
                 * @return true if the suffix is valid for the given operation count
                 * 
                 * Rules:
                 *  - '~ param'          → param applied to both operations (first and second)
                 *  - '~ param1 / param2'→ param1 to first operation, param2 to second
                 *  - '/ ...'            → rejected (empty left side is not allowed)
                 *  - '~ param1 / param2' when dualOp == false → rejected ('/' not allowed for
                 *                         single-operation commands)
                 */
                bool parseXtraParamsSuffix(const std::string& xtraRaw,
                                           bool dualOp,
                                           std::pair<std::string, std::string>& xtraParams) const
                {
                    if (xtraRaw.empty()) {
                        return false;  /* '~' without any text is invalid */
                    }

                    /* Look for '/' separator in the xtra_params portion */
                    auto slashPos = xtraRaw.find('/');

                    if (slashPos == std::string::npos) {
                        /* '~ param' — apply to both operations */
                        xtraParams.first  = xtraRaw;
                        xtraParams.second = xtraRaw;
                        return true;
                    }

                    /* '/' present — only valid when the command has two operations */
                    if (!dualOp) {
                        LOG_PRINT(LOG_ERROR, LOG_HDR;
                                  LOG_STRING("xtra_params '/' separator not allowed for single-operation commands"));
                        return false;
                    }

                    std::string left  = xtraRaw.substr(0, slashPos);
                    std::string right = xtraRaw.substr(slashPos + 1);
                    ustring::trimInPlace(left);
                    ustring::trimInPlace(right);

                    /* Empty left side (e.g. '~ / param') is not allowed */
                    if (left.empty()) {
                        LOG_PRINT(LOG_ERROR, LOG_HDR;
                                  LOG_STRING("xtra_params: left side of '/' must not be empty"));
                        return false;
                    }

                    xtraParams.first  = left;
                    xtraParams.second = right;   /* right side may legitimately be empty */
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

                        /* Token String: T"value" - validate that value is non-empty */
                        if (ustring::undecorate(strItem, DECORATOR_TOKEN_STRING_START, DECORATOR_ANY_END, strOutValue)) {
                            outToken = !strOutValue.empty() ? CommCommandTokenType::TOKEN_STRING : CommCommandTokenType::INVALID;
                            break;
                        }

                        /* Token String: X"value" - validate that value is non-empty */
                        if (ustring::undecorate(strItem, DECORATOR_TOKEN_HEXSTREAM_START, DECORATOR_ANY_END, strOutValue)) {
                            outToken = !strOutValue.empty() ? CommCommandTokenType::TOKEN_HEXSTREAM : CommCommandTokenType::INVALID;
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
                            outToken = (!strOutValue.empty() && numeric::str2sizet(strOutValue, szSize)) ? CommCommandTokenType::SIZEOF : CommCommandTokenType::INVALID;
                            break;
                        }

                        /* Hex stream: H"hexstring" - validate hex format */
                        if (ustring::undecorate(strItem, DECORATOR_HEXLIFY_START, DECORATOR_ANY_END, strOutValue)) {
                            ustring::removeWhitespace(strOutValue);
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
                 * @param command Command to validate
                 * @param separatorFound Flag marking that a separator was found after first token
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
                bool evaluateAndValidate(CommCommand& command, bool separatorFound = false)
                {
                    CommCommandTokenType firstToken  = getTokenType(command.values.first);
                    CommCommandTokenType secondToken = getTokenType(command.values.second);
                    CommCommandDirection direction   = command.direction;

                    /* If pipe was present but recv side is empty → mark as hexdump recv */
                    if (separatorFound 
                        && direction == CommCommandDirection::SEND_RECV 
                        && secondToken == CommCommandTokenType::EMPTY)
                    {
                        secondToken = CommCommandTokenType::ANYTHING;
                    }

                    /* If direction is RECV_SEND and both tokens are empty the just read anything in buffer */
                    if (!separatorFound 
                        && direction == CommCommandDirection::RECV_SEND 
                        && firstToken == CommCommandTokenType::EMPTY
                        && secondToken == CommCommandTokenType::EMPTY)
                    {
                        firstToken = CommCommandTokenType::ANYTHING;
                    }

                    command.tokens = std::make_pair(firstToken, secondToken);

                    if (firstToken == CommCommandTokenType::INVALID || 
                        secondToken == CommCommandTokenType::INVALID) {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid token type detected"));
                        return false;
                    }

                    if (direction == CommCommandDirection::SEND_RECV) {
                        if (firstToken == CommCommandTokenType::TOKEN_STRING    ||
                            firstToken == CommCommandTokenType::TOKEN_HEXSTREAM ||
                            firstToken == CommCommandTokenType::SIZEOF          ||
                            firstToken == CommCommandTokenType::REGEX           ||
                            firstToken == CommCommandTokenType::EMPTY) {
                            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Cannot send TOKEN_*, SIZE, REGEX, or EMPTY"));
                            return false;
                        }
                    /* ANYTHING on the recv side is always valid — no further checks needed */
                    } else if (direction == CommCommandDirection::RECV_SEND) {
                        if (firstToken == CommCommandTokenType::STRING_DELIMITED_EMPTY ||
                            firstToken == CommCommandTokenType::EMPTY) {
                            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Cannot receive EMPTY or STRING_DELIMITED_EMPTY"));
                            return false;
                        }
                    } else if (direction == CommCommandDirection::DELAY) {
                        size_t szDelay = 0;
                        if (!(firstToken == CommCommandTokenType::STRING_RAW) && !numeric::str2sizet(command.values.first, szDelay)) {
                            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid value for delay"));
                            return false;
                        }
                    }

                    if ((firstToken == CommCommandTokenType::EMPTY && secondToken == CommCommandTokenType::EMPTY) ||
                        (firstToken == CommCommandTokenType::STRING_DELIMITED_EMPTY && secondToken == CommCommandTokenType::STRING_DELIMITED_EMPTY))
                    {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Both fields cannot be empty for sending"));
                        return false;
                    }

                    return true;
                }        

        }; /* class ItemParser */

}; /* class CommScriptCommandValidator */



#endif // COMMSCRIPTCOMMANDVALIDATOR_HPP
