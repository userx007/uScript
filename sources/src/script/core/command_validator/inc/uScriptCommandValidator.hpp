#ifndef U_SCRIPT_COMMAND_VALIDATOR_HPP
#define U_SCRIPT_COMMAND_VALIDATOR_HPP

#include "uSharedConfig.hpp"
#include "IScriptCommandValidator.hpp"
#include "uScriptDataTypes.hpp"
#include "uScriptSyntax.hpp"
#include "uString.hpp"
#include "uLogger.hpp"

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

#define LT_HDR     "CORE_CMD_V  |"
#define LOG_HDR    LOG_STRING(LT_HDR)


/////////////////////////////////////////////////////////////////////////////////
//                            CLASS IMPLEMENTATION                             //
/////////////////////////////////////////////////////////////////////////////////

class ScriptCommandValidator : public IScriptCommandValidator<Token>
{
public:

    bool validateCommand(int iLineNumber, const std::string& command, Token& token ) noexcept override
    {
        bool bRetVal = true;

        do {

            if (true == usyntax::m_isLoadPlugin(command) ) {
                token = Token::LOAD_PLUGIN;
                break;
            }

            if (true == usyntax::m_isConstantMacro(command) ) {
                token = Token::CONSTANT_MACRO;
                break;
            }

            if (true == usyntax::m_isArrayMacro(command) ) {
                token = Token::ARRAY_MACRO;
                break;
            }

            if (true == usyntax::m_isVariableMacro(command) ) {
                token = Token::VARIABLE_MACRO;
                break;
            }

            // REPEAT must be checked before VAR_MACRO_INIT because the index-capture
            // form  "varname ?= REPEAT label N"  starts with "identifier ?=" and would
            // otherwise be swallowed by the more general VAR_MACRO_INIT pattern.
            if (true == usyntax::m_isRepeat(command) ) {
                token = Token::REPEAT;
                break;
            }

            // FORMAT must be checked AFTER VARIABLE_MACRO (plugin RHS wins) and
            // AFTER REPEAT (index-capture form wins), but BEFORE the catch-all
            // VAR_MACRO_INIT so that  "name ?= FORMAT input | pattern"  is not
            // silently treated as a plain string initialisation.
            if (true == usyntax::m_isFormatStmt(command) ) {
                token = Token::FORMAT_STMT;
                break;
            }

            // MATH must be checked AFTER VARIABLE_MACRO and REPEAT for the same
            // reason, and AFTER FORMAT so the two keyword-RHS forms are distinct.
            if (true == usyntax::m_isMathStmt(command) ) {
                token = Token::MATH_STMT;
                break;
            }

            // Must be checked AFTER VARIABLE_MACRO (rhs PLUGIN.COMMAND wins) and
            // AFTER REPEAT (index-capture form wins).  Anything else of the form
            // "identifier ?= <value>" is a direct string initialisation.
            if (true == usyntax::m_isVarMacroInit(command) ) {
                token = Token::VAR_MACRO_INIT;
                break;
            }

            if (true == usyntax::m_isCommand(command) ) {
                token = Token::COMMAND;
                break;
            }

            if (true == usyntax::m_isIfGoToCondition(command) ) {
                token = Token::IF_GOTO_LABEL;
                break;
            }

            if (true == usyntax::m_isLabel(command) ) {
                token = Token::LABEL;
                break;
            }

            if (true == usyntax::m_isEndRepeat(command) ) {
                token = Token::END_REPEAT;
                break;
            }

            if (true == usyntax::m_isBreak(command) ) {
                token = Token::BREAK_LOOP;
                break;
            }

            if (true == usyntax::m_isContinue(command) ) {
                token = Token::CONTINUE_LOOP;
                break;
            }

            if (true == usyntax::m_isPrint(command) ) {
                token = Token::PRINT_STMT;
                break;
            }

            if (true == usyntax::m_isDelay(command) ) {
                token = Token::DELAY_STMT;
                break;
            }

            if (true == usyntax::m_isBreakpoint(command) ) {
                token = Token::BREAKPOINT_STMT;
                break;
            }

            token = Token::INVALID;
            bRetVal = false;

        } while(false);

        auto lineNr = ustring::fmtLineNr(iLineNumber);
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); 
                  LOG_STRING(command); 
                  LOG_STRING("->"); 
                  LOG_STRING(getTokenTypeName(token)));

        return bRetVal;

    }
};

#endif // U_SCRIPT_COMMAND_VALIDATOR_HPP
