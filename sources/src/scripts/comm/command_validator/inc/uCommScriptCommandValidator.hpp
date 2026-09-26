#ifndef COMMSCRIPTCOMMANDVALIDATOR_HPP
#define COMMSCRIPTCOMMANDVALIDATOR_HPP

#include "IScriptCommandValidator.hpp"
#include "uCommScriptDataTypes.hpp"
#include "uFile.hpp"
#include "uFileChunkReader.hpp"
#include "uHexlify.hpp"
#include "uLogger.hpp"
#include "uNumeric.hpp"
#include "uSharedConfig.hpp"
#include "uString.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <string>
#include <string_view>
#include <utility>

/////////////////////////////////////////////////////////////////////////////////
//                            LOG DEFINITIONS                                  //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
#undef LT_HDR
#endif
#ifdef LOG_HDR
#undef LOG_HDR
#endif

#define LT_HDR  "COMM_CMD_V  |"
#define LOG_HDR LOG_STRING(LT_HDR)

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
class CommScriptCommandValidator : public IScriptCommandValidator<CommCommand> {
    public:
        bool validateCommand(int iLineNumber, const std::string &strCommand, CommCommand &sToken) noexcept override
        {
            ItemParser itemParser;
            bool bRetVal      = itemParser.parse(strCommand, sToken);
            sToken.iLineNumber = iLineNumber;

            auto lineNr       = ustring::fmtLineNr(iLineNumber);
            LOG_PRINT((bRetVal ? LOG_WERBOSE : LOG_ERROR), LOG_HDR; LOG_STRING(lineNr.data());
                      LOG_STRING(getDirectionName(sToken.direction));
                      LOG_STRING("["); LOG_STRING(sToken.values.first);
                      LOG_STRING(":"); LOG_STRING(sToken.values.second);
                      LOG_STRING("]=["); LOG_STRING(getTokenTypeName(sToken.tokens.first));
                      LOG_STRING(":"); LOG_STRING(getTokenTypeName(sToken.tokens.second));
                      LOG_STRING("] xtra=["); LOG_STRING(sToken.xtra_params.first);
                      LOG_STRING(":"); LOG_STRING(sToken.xtra_params.second);
                      LOG_STRING("]"));
            return bRetVal;
        }

    private:
        class ItemParser {
            public:
                /**
                 * @brief Parse and validate a command command
                 * @param command The command string to parse
                 * @param result Output parameter containing parsed command
                 * @return true if parsing and validation succeeded, false otherwise
                 */
                bool parse(std::string_view command, CommCommand &sResult)
                {
                    sResult = CommCommand{};

                    if (command.empty()) {
                        return false;
                    }

                    /* Determine direction from first character */
                    if (!parseDirection(command, sResult.direction)) {
                        return false;
                    }

                    /* Skip direction character and leading whitespace */
                    command.remove_prefix(1);
                    ustring::skipWhitespace(command);

                    /* if print message: take the entire remainder of the line verbatim
                     * (leading whitespace already skipped above), trim trailing
                     * whitespace, and bypass pipe-splitting / xtra_params / the
                     * send-recv semantic rules entirely - a print statement is just
                     * a message. Macro substitution ($NAME) already happened on the
                     * raw line in CommScriptValidator::validateScript() before this
                     * parse() call, so any macros are already expanded here. */
                    if (sResult.direction == CommCommandDirection::PRINT) {
                        std::string message(command);
                        ustring::trimInPlace(message);

                        if (message.empty()) {
                            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Empty message for '@' print command"));
                            return false;
                        }

                        sResult.values = std::make_pair(std::move(message), std::string{});
                        sResult.tokens = std::make_pair(CommCommandTokenType::STRING_RAW, CommCommandTokenType::EMPTY);
                        return true;
                    }

                    /* if delay: values are already just a trivially short numeric +
                     * unit pair produced by splitValueUnit(), so the owned-string
                     * getTokenType() wrapper (one extra move, no measurable cost here)
                     * is used rather than duplicating classify()'s call sites. */
                    if (sResult.direction == CommCommandDirection::DELAY) {
                        std::string field1, field2;
                        if (command.empty() || !ustring::splitValueUnit(command, std::array<std::string_view, 3>{TIME_MICROSECONDS, TIME_MILISECONDS, TIME_SECONDS}, field1, field2)) {
                            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid delay value/format"));
                            return false;
                        }

                        sResult.values           = std::make_pair(std::move(field1), std::move(field2));
                        CommCommandTokenType t1 = getTokenType(sResult.values.first);
                        CommCommandTokenType t2 = getTokenType(sResult.values.second);
                        return evaluateAndValidate(sResult, t1, t2, /*separatorFound=*/false);
                    }

                    /* command */

                    /* Single quote-aware pass over `command`: locates the two
                     * pipe-separated fields and the optional '~ xtra_params'
                     * suffix as zero-copy string_views - no intermediate
                     * `std::string` body/field buffers. */
                    std::string_view field1View, field2View, xtraView;
                    bool separatorFound = false;
                    bool hasXtra        = false;

                    if (!splitCommandBody(command, field1View, field2View, xtraView, separatorFound, hasXtra)) {
                        return false; /* multiple '|' separators outside quotes */
                    }

                    /* Validate field presence */
                    if (separatorFound && field1View.empty()) {
                        return false;
                    }

                    /* Classify + extract directly into sResult.values - exactly one
                     * allocation per field (see classify()'s doc comment), instead
                     * of building an intermediate owned field1/field2 first and
                     * reclassifying/moving it afterward. */
                    CommCommandTokenType firstToken  = classify(field1View, sResult.values.first);
                    CommCommandTokenType secondToken = classify(field2View, sResult.values.second);

                    /* Parse '~ param' or '~ param1 | param2' */
                    if (hasXtra && !xtraView.empty()) {
                        if (!parseXtraParamsSuffix(xtraView, separatorFound, sResult.xtra_params)) {
                            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid xtra_params format"));
                            return false;
                        }
                    }

                    return evaluateAndValidate(sResult, firstToken, secondToken, separatorFound);

                } /* parse() */

            private:
                /**
                 * @brief Parse direction indicator from command
                 * @param command Command string (must start with >, < or !)
                 * @param direction Output parameter for parsed direction
                 * @return true if valid direction found
                 */
                bool parseDirection(std::string_view command, CommCommandDirection &eDirection) const
                {
                    char firstChar = command.front();
                    switch (firstChar) {
                    case '>':
                        eDirection = CommCommandDirection::SEND_RECV;
                        return true;
                    case '<':
                        eDirection = CommCommandDirection::RECV_SEND;
                        return true;
                    case '!':
                        eDirection = CommCommandDirection::DELAY;
                        return true;
                    case '@':
                        eDirection = CommCommandDirection::PRINT;
                        return true;
                    default:
                        return false;
                    }
                }

                /**
                 * @brief Locate the two pipe-separated fields and the optional
                 *        '~ xtra_params' suffix in a single quote-aware pass
                 * @param command       Command string to split (direction char already removed)
                 * @param field1        Output view of the first field (trimmed, quotes preserved)
                 * @param field2        Output view of the second field (trimmed, quotes preserved)
                 * @param xtraRaw       Output view of the raw text after '~' (trimmed), empty when absent
                 * @param separatorFound Output: whether an unquoted '|' was found
                 * @param hasXtra       Output: whether an unquoted '~' was found
                 * @return false if more than one unquoted '|' is present, true otherwise
                 *
                 * This replaces the previous two-pass implementation (a
                 * splitXtraParams() copy into an intermediate `std::string`
                 * body, followed by a splitFields() char-by-char copy into
                 * `field1`/`field2`). Both of those allocated and copied the
                 * command text end-to-end before a single byte had even been
                 * classified. Here the command line is scanned exactly once
                 * and every output is a `string_view` aliasing `command` -
                 * zero allocations. The caller decides if/when a field needs
                 * to become an owned `std::string` (i.e. exactly once, when
                 * it is finally committed to CommCommand storage).
                 *
                 * The tilde is only a separator outside quotes - a quoted '~'
                 * inside H"..." / "..." / etc. is treated as data, matching
                 * the previous behaviour. Everything from the first unquoted
                 * '~' onward belongs to xtra_params, so pipe-scanning stops there
                 * — which is also why parseXtraParamsSuffix() below can safely
                 * reuse '|' as its own per-operation separator: this loop
                 * never scans for '|' past an unquoted '~', so the field '|'
                 * (before '~') and the xtra_params '|' (after '~') always sit
                 * in disjoint, non-overlapping regions of the line.
                 */
                bool splitCommandBody(std::string_view command,
                                      std::string_view &field1, std::string_view &field2,
                                      std::string_view &xtraRaw,
                                      bool &bSeparatorFound, bool &bHasXtra) const
                {
                    bool insideQuote     = false;
                    std::size_t pipePos  = std::string_view::npos;
                    std::size_t tildePos = std::string_view::npos;

                    for (std::size_t i = 0; i < command.size(); ++i) {
                        char ch = command[i];
                        if (ch == '"') {
                            insideQuote = !insideQuote;
                        } else if (!insideQuote && ch == '~') {
                            tildePos = i;
                            break; /* everything from here on is xtra_params, not body */
                        } else if (!insideQuote && ch == '|') {
                            if (pipePos != std::string_view::npos) {
                                return false; /* multiple separators outside quotes */
                            }
                            pipePos = i;
                        }
                    }

                    std::string_view body = (tildePos == std::string_view::npos)
                                                ? command
                                                : command.substr(0, tildePos);

                    bSeparatorFound        = (pipePos != std::string_view::npos);
                    field1                = ustring::trim_view(bSeparatorFound ? body.substr(0, pipePos) : body);
                    field2                = ustring::trim_view(bSeparatorFound ? body.substr(pipePos + 1) : std::string_view{});

                    bHasXtra               = (tildePos != std::string_view::npos);
                    xtraRaw               = bHasXtra ? ustring::trim_view(command.substr(tildePos + 1)) : std::string_view{};

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
                 *  - '~ param1 | param2'→ param1 to first operation, param2 to second
                 *  - '~ param1 | param2' when dualOp == false → rejected ('|' not allowed for
                 *                         single-operation commands)
                 *
                 * Either side of the '|' may legitimately be empty (e.g. '~ | 0x22' or
                 * '~ 0x21 |'), matching the fact that an empty xtra_param is itself a
                 * meaningful value downstream — every driver treats it as "no override,
                 * fall back to the connection's configured default" (see e.g. the KVCAN
                 * plugin's tout_write()/tout_read() docs) rather than as an error. So
                 * '~ | 0x22' ("use the default for the send, but 0x22 for the receive")
                 * is exactly as valid as '~ 0x21 |' ("use 0x21 for the send, default for
                 * the receive") — there is no reason to privilege one side over the other.
                 * '~ |' alone (both sides empty) is likewise accepted; it is simply a
                 * verbose way of saying "no override for either operation", equivalent to
                 * omitting the '~' suffix entirely.
                 *
                 * Note: this '|' is scoped entirely to the xtraRaw text (i.e.
                 * everything after an unquoted '~'). It shares its character
                 * with the field separator '|' used by splitCommandBody()
                 * above, but never the same text: splitCommandBody() stops
                 * looking for '|' as soon as it hits an unquoted '~', so the
                 * two never compete over the same character position.
                 */
                bool parseXtraParamsSuffix(std::string_view xtraRaw,
                                           bool bDualOp,
                                           std::pair<std::string, std::string> &xtraParams) const
                {
                    if (xtraRaw.empty()) {
                        return false; /* '~' without any text is invalid */
                    }

                    /* Look for '|' separator in the xtra_params portion */
                    auto pipePos = xtraRaw.find('|');

                    if (pipePos == std::string_view::npos) {
                        /* '~ param' — apply to both operations. Two owning copies are
                         * unavoidable (each pair member must independently own its
                         * data), but this is exactly one allocation per copy - no
                         * scratch/intermediate buffers involved. */
                        xtraParams.first.assign(xtraRaw);
                        xtraParams.second = xtraParams.first;
                        return true;
                    }

                    /* '|' present — only valid when the command has two operations */
                    if (!bDualOp) {
                        LOG_PRINT(LOG_ERROR, LOG_HDR;
                                  LOG_STRING("xtra_params '|' separator not allowed for single-operation commands"));
                        return false;
                    }

                    std::string_view left  = ustring::trim_view(xtraRaw.substr(0, pipePos));
                    std::string_view right = ustring::trim_view(xtraRaw.substr(pipePos + 1));

                    /* Both sides may legitimately be empty — see the doc comment above */
                    xtraParams.first       = std::string(left);
                    xtraParams.second      = std::string(right);
                    return true;
                }

                /**
                 * @brief Classify a field and write its final content directly into `output`
                 * @param input  Raw field text (quotes/decorators still present) - a view,
                 *               not yet owned by the caller
                 * @param output Receives the classified value - must be a *different*
                 *               object than whatever backs `input` (e.g. a freshly
                 *               default-constructed CommCommand::values member).
                 *               Overwritten unconditionally.
                 * @return Detected token type with validation
                 *
                 * This is the single allocation point for a field. Previously, a field
                 * went: split into an owned std::string (1 alloc) -> handed to a
                 * mutate-in-place getTokenType() which allocated an extraction buffer
                 * internally for decorated fields (2nd alloc) -> moved over the original
                 * (free, but the 1st alloc's content was entirely thrown away). Here,
                 * for a decorated field (H"..", R"..", etc.) undecorate() writes the
                 * extracted content straight into `output` - nothing is allocated and
                 * discarded first. For a plain/raw field, `output` is constructed
                 * directly from `input` - one allocation, not two.
                 *
                 * Validates:
                 * - File existence and non-empty for FILENAME type
                 * - Valid hex string for HEXSTREAM type
                 * - Valid numeric for SIZE type
                 * - Valid regex pattern for REGEX type
                 */
                CommCommandTokenType classify(std::string_view input, std::string &strOutput) const
                {
                    /* Empty expression */
                    if (input.empty()) {
                        strOutput.clear();
                        return CommCommandTokenType::EMPTY;
                    }

                    /* Delimited string: "content" or "" */
                    if (ustring::undecorate(input, DECORATOR_STRING_START, DECORATOR_ANY_END, strOutput)) {
                        return !strOutput.empty() ? CommCommandTokenType::STRING_DELIMITED : CommCommandTokenType::STRING_DELIMITED_EMPTY;
                    }

                    /* Regex pattern: R"pattern" - validate that pattern is non-empty */
                    if (ustring::undecorate(input, DECORATOR_REGEX_START, DECORATOR_ANY_END, strOutput)) {
                        return !strOutput.empty() ? CommCommandTokenType::REGEX : CommCommandTokenType::INVALID;
                    }

                    /* Token String: T"value" - validate that value is non-empty */
                    if (ustring::undecorate(input, DECORATOR_TOKEN_STRING_START, DECORATOR_ANY_END, strOutput)) {
                        return !strOutput.empty() ? CommCommandTokenType::TOKEN_STRING : CommCommandTokenType::INVALID;
                    }

                    /* Token String: X"value" - validate that value is non-empty */
                    if (ustring::undecorate(input, DECORATOR_TOKEN_HEXSTREAM_START, DECORATOR_ANY_END, strOutput)) {
                        ustring::removeWhitespace(strOutput);
                        return !strOutput.empty() ? CommCommandTokenType::TOKEN_HEXSTREAM : CommCommandTokenType::INVALID;
                    }

                    /* Line: L"content" - validate that content is non-empty */
                    if (ustring::undecorate(input, DECORATOR_LINE_START, DECORATOR_ANY_END, strOutput)) {
                        return !strOutput.empty() ? CommCommandTokenType::LINE : CommCommandTokenType::INVALID;
                    }

                    /* Size: S"number" - validate numeric and non-empty */
                    if (ustring::undecorate(input, DECORATOR_SIZE_START, DECORATOR_ANY_END, strOutput)) {
                        size_t szSize = 0;
                        return (!strOutput.empty() && numeric::str2sizet(strOutput, szSize)) ? CommCommandTokenType::SIZEOF : CommCommandTokenType::INVALID;
                    }

                    /* Hex stream: H"hexstring" - validate hex format */
                    if (ustring::undecorate(input, DECORATOR_HEXLIFY_START, DECORATOR_ANY_END, strOutput)) {
                        ustring::removeWhitespace(strOutput);
                        return (!strOutput.empty() && hexutils::isHexlified(strOutput)) ? CommCommandTokenType::HEXSTREAM : CommCommandTokenType::INVALID;
                    }

                    /* File: F"filename.bin" or F"filename.bin,options" - validate file exists and is non-empty */
                    if (ustring::undecorate(input, DECORATOR_FILENAME_START, DECORATOR_ANY_END, strOutput)) {
                        /* Extract filename part (before optional comma-separated options).
                         * ufile::fileExistsAndNotEmpty(string_view) avoids an extra
                         * std::string allocation here. */
                        std::string_view filename = ustring::substringUntil(strOutput, CHAR_SEPARATOR_COMMA);
                        return (!strOutput.empty() && ufile::fileExistsAndNotEmpty(filename)) ? CommCommandTokenType::FILENAME : CommCommandTokenType::INVALID;
                    }

                    /* Validate raw string format */
                    if (!ustring::isValidTaggedOrPlainString(input)) {
                        /* Matches the previous behaviour: an invalid raw/tagged string
                         * clears the field rather than preserving the offending text. */
                        strOutput.clear();
                        return CommCommandTokenType::INVALID;
                    }

                    /* Raw undecorated string: nothing to extract, `input` verbatim
                     * becomes the value - exactly one allocation. */
                    strOutput.assign(input);
                    return CommCommandTokenType::STRING_RAW;
                }

                /**
                 * @brief Owned-string compatibility wrapper around classify()
                 * @param strItem Input/output parameter - expression string (modified to contain extracted value)
                 * @return Detected token type with validation
                 *
                 * Used only by the DELAY path in parse(), where the value is already
                 * an owned std::string (produced by ustring::splitValueUnit()) and
                 * always trivially short (a numeric delay + unit), so the extra
                 * move here costs nothing measurable. Every other caller uses
                 * classify() directly to avoid this round trip.
                 */
                CommCommandTokenType getTokenType(std::string &strItem) const
                {
                    std::string extracted;
                    CommCommandTokenType outToken = classify(strItem, extracted);
                    strItem                       = std::move(extracted);
                    return outToken;
                }

                /**
                 * @brief Evaluate and validate command semantics
                 * @param command Command to validate
                 * @param firstToken Token type already classified for command.values.first
                 * @param secondToken Token type already classified for command.values.second
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
                bool evaluateAndValidate(CommCommand &sCommand, CommCommandTokenType eFirstToken,
                                         CommCommandTokenType eSecondToken, bool bSeparatorFound = false)
                {
                    CommCommandDirection direction = sCommand.direction;

                    /* If pipe was present but recv side is empty → mark as hexdump recv */
                    if (bSeparatorFound && direction == CommCommandDirection::SEND_RECV && eSecondToken == CommCommandTokenType::EMPTY) {
                        eSecondToken = CommCommandTokenType::ANYTHING;
                    }

                    /* If direction is RECV_SEND and both tokens are empty the just read anything in buffer */
                    if (!bSeparatorFound && direction == CommCommandDirection::RECV_SEND && eFirstToken == CommCommandTokenType::EMPTY && eSecondToken == CommCommandTokenType::EMPTY) {
                        eFirstToken = CommCommandTokenType::ANYTHING;
                    }

                    sCommand.tokens = std::make_pair(eFirstToken, eSecondToken);

                    if (eFirstToken == CommCommandTokenType::INVALID ||
                        eSecondToken == CommCommandTokenType::INVALID) {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid token type detected"));
                        return false;
                    }

                    if (direction == CommCommandDirection::SEND_RECV) {
                        if (eFirstToken == CommCommandTokenType::TOKEN_STRING ||
                            eFirstToken == CommCommandTokenType::TOKEN_HEXSTREAM ||
                            eFirstToken == CommCommandTokenType::SIZEOF ||
                            eFirstToken == CommCommandTokenType::REGEX ||
                            eFirstToken == CommCommandTokenType::EMPTY) {
                            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Cannot send TOKEN_*, SIZE, REGEX, or EMPTY"));
                            return false;
                        }
                        /* ANYTHING on the recv side is always valid — no further checks needed */
                    } else if (direction == CommCommandDirection::RECV_SEND) {
                        if (eFirstToken == CommCommandTokenType::STRING_DELIMITED_EMPTY ||
                            eFirstToken == CommCommandTokenType::EMPTY) {
                            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Cannot receive EMPTY or STRING_DELIMITED_EMPTY"));
                            return false;
                        }
                    } else if (direction == CommCommandDirection::DELAY) {
                        size_t szDelay = 0;
                        if (!(eFirstToken == CommCommandTokenType::STRING_RAW) && !numeric::str2sizet(sCommand.values.first, szDelay)) {
                            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid value for delay"));
                            return false;
                        }
                    }

                    if ((eFirstToken == CommCommandTokenType::EMPTY && eSecondToken == CommCommandTokenType::EMPTY) ||
                        (eFirstToken == CommCommandTokenType::STRING_DELIMITED_EMPTY && eSecondToken == CommCommandTokenType::STRING_DELIMITED_EMPTY)) {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Both fields cannot be empty for sending"));
                        return false;
                    }

                    return true;
                }

        }; /* class ItemParser */

}; /* class CommScriptCommandValidator */

#endif // COMMSCRIPTCOMMANDVALIDATOR_HPP
