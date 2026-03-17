#ifndef U_SCRIPT_COMMAND_VALIDATOR_HPP
#define U_SCRIPT_COMMAND_VALIDATOR_HPP

#include "uSharedConfig.hpp"
#include "IScriptCommandValidator.hpp"
#include "uScriptDataTypes.hpp"
#include "uLogger.hpp"


#include <string>
#include <regex>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "CORE_CMD_V  |"
#define LOG_HDR    LOG_STRING(LT_HDR)


/////////////////////////////////////////////////////////////////////////////////
//                            CLASS IMPLEMENTATION                             //
/////////////////////////////////////////////////////////////////////////////////

class ScriptCommandValidator : public IScriptCommandValidator<Token>
{
public:

    bool validateCommand(const std::string& command, Token& token ) noexcept override
    {
        bool bRetVal = true;

        do {

            if (true == m_isLoadPlugin(command) ) {
                token = Token::LOAD_PLUGIN;
                break;
            }

            if (true == m_isConstantMacro(command) ) {
                token = Token::CONSTANT_MACRO;
                break;
            }

            if (true == m_isArrayMacro(command) ) {
                token = Token::ARRAY_MACRO;
                break;
            }

            if (true == m_isVariableMacro(command) ) {
                token = Token::VARIABLE_MACRO;
                break;
            }

            // REPEAT must be checked before VAR_MACRO_INIT because the index-capture
            // form  "varname ?= REPEAT label N"  starts with "identifier ?=" and would
            // otherwise be swallowed by the more general VAR_MACRO_INIT pattern.
            if (true == m_isRepeat(command) ) {
                token = Token::REPEAT;
                break;
            }

            // FORMAT must be checked AFTER VARIABLE_MACRO (plugin RHS wins) and
            // AFTER REPEAT (index-capture form wins), but BEFORE the catch-all
            // VAR_MACRO_INIT so that  "name ?= FORMAT input | pattern"  is not
            // silently treated as a plain string initialisation.
            if (true == m_isFormatStmt(command) ) {
                token = Token::FORMAT_STMT;
                break;
            }

            // Must be checked AFTER VARIABLE_MACRO (rhs PLUGIN.COMMAND wins) and
            // AFTER REPEAT (index-capture form wins).  Anything else of the form
            // "identifier ?= <value>" is a direct string initialisation.
            if (true == m_isVarMacroInit(command) ) {
                token = Token::VAR_MACRO_INIT;
                break;
            }

            if (true == m_isCommand(command) ) {
                token = Token::COMMAND;
                break;
            }

            if (true == m_isIfGoToCondition(command) ) {
                token = Token::IF_GOTO_LABEL;
                break;
            }

            if (true == m_isLabel(command) ) {
                token = Token::LABEL;
                break;
            }

            if (true == m_isEndRepeat(command) ) {
                token = Token::END_REPEAT;
                break;
            }

            if (true == m_isBreak(command) ) {
                token = Token::BREAK_LOOP;
                break;
            }

            if (true == m_isContinue(command) ) {
                token = Token::CONTINUE_LOOP;
                break;
            }

            if (true == m_isPrint(command) ) {
                token = Token::PRINT_STMT;
                break;
            }

            if (true == m_isDelay(command) ) {
                token = Token::DELAY_STMT;
                break;
            }

            token = Token::INVALID;
            bRetVal = false;

        } while(false);

        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(command); LOG_STRING("->"); LOG_STRING(getTokenTypeName(token)));

        return bRetVal;

    }

private:

    // validate a load plugin expression
    bool m_isLoadPlugin(const std::string& expression )
    {
        static const std::regex pattern(R"(^LOAD_PLUGIN\s+[A-Za-z][A-Za-z0-9_]*(\s+(<=|<|>=|>|==)\s+v\d+\.\d+\.\d+\.\d+)?$)");
        return std::regex_match(expression, pattern);
    }

    // validate a constant macro expression
    bool m_isConstantMacro(const std::string& expression )
    {
        static const std::regex pattern(R"(^[A-Za-z_][A-Za-z0-9_]*\s*:=\s*\S.*$)");
        return std::regex_match(expression, pattern);
    }

    // validate an array macro expression:  NAME [= elem1, elem2, ...
    // At least one element (non-empty content after [=) is required.
    bool m_isArrayMacro(const std::string& expression)
    {
        static const std::regex pattern(R"(^[A-Za-z_][A-Za-z0-9_]*\s*\[=\s*\S.*$)");
        return std::regex_match(expression, pattern);
    }

    // validate a variable macro expression
    bool m_isVariableMacro(const std::string& expression )
    {
        static const std::regex pattern(R"(^[A-Za-z_][A-Za-z0-9_]*\s*\?=\s*[A-Z][A-Z0-9_]*\.[A-Z][A-Z0-9_]*.*$)");
        return std::regex_match(expression, pattern);
    }

    // validate a direct variable macro initialisation:  name ?= <string value>
    // This form is recognised only when VARIABLE_MACRO does NOT match — i.e. the
    // right-hand side is not a PLUGIN.COMMAND pattern.  The value may be empty
    // (bare "name ?=") which initialises the macro to an empty string.
    bool m_isVarMacroInit(const std::string& expression)
    {
        static const std::regex pattern(R"(^[A-Za-z_][A-Za-z0-9_]*\s*\?=(\s.*)?$)");
        return std::regex_match(expression, pattern);
    }

    // validate a FORMAT statement:  name ?= FORMAT input | format_pattern
    //
    // Syntax:
    //   <identifier> ?= FORMAT <input_text> | <format_template>
    //
    // Both <input_text> and <format_template> may contain $macros (expanded at
    // runtime).  The pipe character '|' is mandatory and separates the two
    // operands.  At least one non-whitespace character must follow FORMAT.
    //
    // Examples (after $macro expansion):
    //   out ?= FORMAT Hello world from Paris | I salute from %3 to the %1 with %0
    //   out ?= FORMAT $words | %2 %1 %0
    bool m_isFormatStmt(const std::string& expression)
    {
        // name ?= FORMAT <something> | <something>
        // Both sides of | must have at least one non-ws character.
        static const std::regex pattern(
            R"(^[A-Za-z_][A-Za-z0-9_]*\s*\?=\s*FORMAT\s+\S[^|]*\|\s*\S.*$)");
        return std::regex_match(expression, pattern);
    }

    // validate simple command
    bool m_isCommand(const std::string& expression )
    {
        static const std::regex pattern(R"(^[A-Z][A-Z0-9_]*\.[A-Z][A-Z0-9_]*\s*.*$)");
        return std::regex_match(expression, pattern);
    }

    // validate "IF .. GOTO .." or "GOTO .." conditions
    bool m_isIfGoToCondition(const std::string& expression)
    {
        static const std::regex pattern(R"(^(?:IF\s+\S(?:.*\S)?\s+)?GOTO\s+[A-Za-z_][A-Za-z0-9_]*$)");
        return std::regex_match(expression, pattern);
    }

    // validate LABEL
    bool m_isLabel(const std::string& expression )
    {
        static const std::regex pattern(R"(^LABEL\s+[A-Za-z_][A-Za-z0-9_]*$)");
        return std::regex_match(expression, pattern);
    }

    // validate REPEAT <label> <count>  or  REPEAT <label> UNTIL <condition>
    // and their index-capture forms:  varname ?= REPEAT <label> <count / UNTIL cond>
    // Both forms share the same token; the handler in the validator distinguishes them.
    bool m_isRepeat(const std::string& expression)
    {
        // Optional capture prefix:  [varname ?=]
        // Counted form:     [varname ?=] REPEAT label N
        static const std::regex counted(R"(^(?:[A-Za-z_][A-Za-z0-9_]*\s*\?=\s*)?REPEAT\s+[A-Za-z_][A-Za-z0-9_]*\s+[1-9][0-9]*$)");
        // Conditional form: [varname ?=] REPEAT label UNTIL cond
        static const std::regex until  (R"(^(?:[A-Za-z_][A-Za-z0-9_]*\s*\?=\s*)?REPEAT\s+[A-Za-z_][A-Za-z0-9_]*\s+UNTIL\s+\S.*$)");
        return std::regex_match(expression, counted) || std::regex_match(expression, until);
    }

    // validate END_REPEAT <label>
    bool m_isEndRepeat(const std::string& expression)
    {
        static const std::regex pattern(R"(^END_REPEAT\s+[A-Za-z_][A-Za-z0-9_]*$)");
        return std::regex_match(expression, pattern);
    }

    // validate BREAK <loop-label>
    bool m_isBreak(const std::string& expression)
    {
        static const std::regex pattern(R"(^BREAK\s+[A-Za-z_][A-Za-z0-9_]*$)");
        return std::regex_match(expression, pattern);
    }

    // validate CONTINUE <loop-label>
    bool m_isContinue(const std::string& expression)
    {
        static const std::regex pattern(R"(^CONTINUE\s+[A-Za-z_][A-Za-z0-9_]*$)");
        return std::regex_match(expression, pattern);
    }

    // validate PRINT [text]
    bool m_isPrint(const std::string& expression)
    {
        static const std::regex pattern(R"(^PRINT(\s.*)?$)");
        return std::regex_match(expression, pattern);
    }

    // validate DELAY <value> <unit>
    // <value> : positive integer (>= 1)
    // <unit>  : us | ms | sec   (case-sensitive)
    bool m_isDelay(const std::string& expression)
    {
        static const std::regex pattern(R"(^DELAY\s+[1-9][0-9]*\s+(us|ms|sec)$)");
        return std::regex_match(expression, pattern);
    }

};

#endif // U_SCRIPT_COMMAND_VALIDATOR_HPP
