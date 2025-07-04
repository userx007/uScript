#ifndef ITEMVALIDATOR_HPP
#define ITEMVALIDATOR_HPP

#include "IItemValidator.hpp"
#include "CommonSettings.hpp"
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
#define LT_HDR     "ITEMVALID  :"
#define LOG_HDR    LOG_STRING(LT_HDR)


/* Tokens type */
enum class Token {
    LOAD_PLUGIN,
    CONSTANT_MACRO,
    VARIABLE_MACRO,
    COMMAND,
    IF_GOTO_LABEL,
    LABEL,
    INVALID
};


/////////////////////////////////////////////////////////////////////////////////
//                            CLASS IMPLEMENTATION                             //
/////////////////////////////////////////////////////////////////////////////////

class ScriptItemValidator : public IItemValidator<Token>
{
public:

    bool validateItem ( const std::string& item, Token& token ) noexcept override
    {
        bool bRetVal = true;

        do {

            if (true == m_isLoadPlugin(item) ) {
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(item); LOG_STRING(" -> LOAD"));
                token = Token::LOAD_PLUGIN;
                break;
            }

            if (true == m_isConstantMacro(item) ) {
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(item); LOG_STRING(" -> CMACRO"));
                token = Token::CONSTANT_MACRO;
                break;
            }

            if (true == m_isVariableMacro(item) ) {
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(item); LOG_STRING(" -> VMACRO"));
                token = Token::VARIABLE_MACRO;
                break;
            }

            if (true == m_isCommand(item) ) {
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(item); LOG_STRING(" -> COMMAND"));
                token = Token::COMMAND;
                break;
            }

            if (true == m_isIfGoToCondition(item) ) {
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(item); LOG_STRING(" -> [IF]GOTO"));
                token = Token::IF_GOTO_LABEL;
                break;
            }

            if (true == m_isLabel(item) ) {
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(item); LOG_STRING(" -> LABEL"));
                token = Token::LABEL;
                break;
            }

            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(item); LOG_STRING(" -> UNKNOWN"));
            token = Token::INVALID;
            bRetVal = false;

        } while(false);

        return bRetVal;

    }

private:

    // validate a load plugin expression
    bool m_isLoadPlugin( const std::string& expression )
    {
        static const std::regex pattern(R"(^LOAD_PLUGIN\s+[A-Za-z_]+(_[A-Za-z_]+)?(\s+(<=|<|>=|>|==)\s+v\d+\.\d+\.\d+\.\d+)?$)");
        return std::regex_match(expression, pattern);
    }

    // validate a constant macro expression
    bool m_isConstantMacro ( const std::string& expression )
    {
        static const std::regex pattern(R"(^[A-Za-z_][A-Za-z0-9_]*\s*:=\s*\S.*$)");
        return std::regex_match(expression, pattern);
    }

    // validate a variable macro expression
    bool m_isVariableMacro ( const std::string& expression )
    {
        static const std::regex pattern(R"(^[A-Z]+[A-Z_]*[A-Z]+\s*\?=\s*[A-Z]+[A-Z_]*[A-Z]+\.[A-Z]+[A-Z_]*[A-Z]+.*$)");
        return std::regex_match(expression, pattern);
    }

    // validate simple item
    bool m_isCommand ( const std::string& expression )
    {
        static const std::regex pattern(R"(^[A-Z]+[A-Z_]*[A-Z]+\.[A-Z]+[A-Z_]*[A-Z]+\s*.*$)");
        return std::regex_match(expression, pattern);
    }

    // validate "IF .. GOTO .." or "GOTO .." conditions
    bool m_isIfGoToCondition(const std::string& expression)
    {
        static const std::regex pattern(R"(^(?:IF\s+\S(?:.*\S)?\s+)?GOTO\s+[A-Za-z_][A-Za-z0-9_]*$)");
        return std::regex_match(expression, pattern);
    }

    // validate LABEL
    bool m_isLabel ( const std::string& expression )
    {
        static const std::regex pattern(R"(^LABEL\s+[A-Za-z_][A-Za-z0-9_]*$)");
        return std::regex_match(expression, pattern);
    }

};

#endif // ITEMVALIDATOR_HPP

