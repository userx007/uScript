#ifndef ITEMVALIDATOR_HPP
#define ITEMVALIDATOR_HPP

#include "CommonSettings.hpp"
#include "IScriptItemValidator.hpp"
#include "ScriptDataTypes.hpp"
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


/////////////////////////////////////////////////////////////////////////////////
//                            CLASS IMPLEMENTATION                             //
/////////////////////////////////////////////////////////////////////////////////

class ScriptItemValidator : public IScriptItemValidator<Token>
{
public:

    bool validateItem(const std::string& item, Token& token ) noexcept override
    {
        bool bRetVal = true;

        do {

            if (true == m_isLoadPlugin(item) ) {
                token = Token::LOAD_PLUGIN;
                break;
            }

            if (true == m_isConstantMacro(item) ) {
                token = Token::CONSTANT_MACRO;
                break;
            }

            if (true == m_isVariableMacro(item) ) {
                token = Token::VARIABLE_MACRO;
                break;
            }

            if (true == m_isCommand(item) ) {
                token = Token::COMMAND;
                break;
            }

            if (true == m_isIfGoToCondition(item) ) {
                token = Token::IF_GOTO_LABEL;
                break;
            }

            if (true == m_isLabel(item) ) {
                token = Token::LABEL;
                break;
            }

            token = Token::INVALID;
            bRetVal = false;

        } while(false);

        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(item); LOG_STRING("->"); LOG_STRING(getTokenName(token)));

        return bRetVal;

    }

private:

    // validate a load plugin expression
    bool m_isLoadPlugin(const std::string& expression )
    {
        static const std::regex pattern(R"(^LOAD_PLUGIN\s+[A-Za-z_]+(_[A-Za-z_]+)?(\s+(<=|<|>=|>|==)\s+v\d+\.\d+\.\d+\.\d+)?$)");
        return std::regex_match(expression, pattern);
    }

    // validate a constant macro expression
    bool m_isConstantMacro(const std::string& expression )
    {
        static const std::regex pattern(R"(^[A-Za-z_][A-Za-z0-9_]*\s*:=\s*\S.*$)");
        return std::regex_match(expression, pattern);
    }

    // validate a variable macro expression
    bool m_isVariableMacro(const std::string& expression )
    {
        static const std::regex pattern(R"(^^[A-Za-z_][A-Za-z0-9_]*\s*\?=\s*[A-Z]+[A-Z_]*[A-Z]+\.[A-Z]+[A-Z_]*[A-Z]+.*$)");
        return std::regex_match(expression, pattern);
    }

    // validate simple item
    bool m_isCommand(const std::string& expression )
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
    bool m_isLabel(const std::string& expression )
    {
        static const std::regex pattern(R"(^LABEL\s+[A-Za-z_][A-Za-z0-9_]*$)");
        return std::regex_match(expression, pattern);
    }
};

#endif // ITEMVALIDATOR_HPP

