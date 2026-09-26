#ifndef U_SCRIPT_COMMAND_VALIDATOR_HPP
#define U_SCRIPT_COMMAND_VALIDATOR_HPP

#include "IScriptCommandValidator.hpp"
#include "uLogger.hpp"
#include "uScriptDataTypes.hpp"
#include "uScriptSyntax.hpp"
#include "uSharedConfig.hpp"
#include "uString.hpp"

#include <string>

/////////////////////////////////////////////////////////////////////////////////
//                            LOG DEFINITIONS                                  //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
#undef LT_HDR
#endif
#ifdef LOG_HDR
#undef LOG_HDR
#endif

#define LT_HDR  "CORE_CMD_V  |"
#define LOG_HDR LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                            CLASS IMPLEMENTATION                             //
/////////////////////////////////////////////////////////////////////////////////

class ScriptCommandValidator : public IScriptCommandValidator<Token> {
    public:
        bool validateCommand(int iLineNumber, const std::string &strCommand, Token &eToken) noexcept override
        {
            bool bRetVal = true;

            do {

                if (true == usyntax::m_isLoadPlugin(strCommand)) {
                    eToken = Token::LOAD_PLUGIN;
                    break;
                }

                if (true == usyntax::m_isConstantMacro(strCommand)) {
                    eToken = Token::CONSTANT_MACRO;
                    break;
                }

                // The array-form BITSTREAMVAL/BYTESTREAMVAL statements ("name [=
                // source | KEYWORD field...") must be checked BEFORE the generic
                // ARRAY_MACRO pattern ("name [= elem1, elem2, ..."), because
                // ARRAY_MACRO's own pattern is permissive enough (any non-empty
                // content after "[=") to also match them.
                if (true == usyntax::m_isBitstreamValArrayStmt(strCommand)) {
                    eToken = Token::BITSTREAMVAL_ARRAY_STMT;
                    break;
                }

                if (true == usyntax::m_isBytestreamValArrayStmt(strCommand)) {
                    eToken = Token::BYTESTREAMVAL_ARRAY_STMT;
                    break;
                }

                if (true == usyntax::m_isArrayMacro(strCommand)) {
                    eToken = Token::ARRAY_MACRO;
                    break;
                }

                if (true == usyntax::m_isVariableMacro(strCommand)) {
                    eToken = Token::VARIABLE_MACRO;
                    break;
                }

                // REPEAT must be checked before VAR_MACRO_INIT because the index-capture
                // form  "varname ?= REPEAT label N"  starts with "identifier ?=" and would
                // otherwise be swallowed by the more general VAR_MACRO_INIT pattern.
                if (true == usyntax::m_isRepeat(strCommand)) {
                    eToken = Token::REPEAT;
                    break;
                }

                // FORMAT must be checked AFTER VARIABLE_MACRO (plugin RHS wins) and
                // AFTER REPEAT (index-capture form wins), but BEFORE the catch-all
                // VAR_MACRO_INIT so that  "name ?= FORMAT input | pattern"  is not
                // silently treated as a plain string initialisation.
                if (true == usyntax::m_isFormatStmt(strCommand)) {
                    eToken = Token::FORMAT_STMT;
                    break;
                }

                // MATH must be checked AFTER VARIABLE_MACRO and REPEAT for the same
                // reason, and AFTER FORMAT so the two keyword-RHS forms are distinct.
                if (true == usyntax::m_isMathStmt(strCommand)) {
                    eToken = Token::MATH_STMT;
                    break;
                }

                // Same ordering rationale as FORMAT/MATH above: after VARIABLE_MACRO
                // and REPEAT (so their own forms of "identifier ?=..." win first),
                // and distinct from FORMAT/MATH's own keyword-RHS shapes.
                if (true == usyntax::m_isGeneratorStmt(strCommand)) {
                    eToken = Token::GENERATOR_STMT;
                    break;
                }

                // Same ordering rationale as FORMAT/MATH above: after VARIABLE_MACRO
                // and REPEAT (so their own forms of "identifier ?=..." win first),
                // and distinct from FORMAT/MATH's own keyword-RHS shapes.
                if (true == usyntax::m_isBitstreamStmt(strCommand)) {
                    eToken = Token::BITSTREAM_STMT;
                    break;
                }

                if (true == usyntax::m_isBytestreamStmt(strCommand)) {
                    eToken = Token::BYTESTREAM_STMT;
                    break;
                }

                // Same ordering rationale as BITSTREAM/BYTESTREAM just above:
                // after VARIABLE_MACRO and REPEAT, and before the VAR_MACRO_INIT
                // catch-all so "name ?= source | BITSTREAMVAL ..." is not
                // swallowed as a plain string initialisation.
                if (true == usyntax::m_isBitstreamValStmt(strCommand)) {
                    eToken = Token::BITSTREAMVAL_STMT;
                    break;
                }

                if (true == usyntax::m_isBytestreamValStmt(strCommand)) {
                    eToken = Token::BYTESTREAMVAL_STMT;
                    break;
                }

                // Must be checked AFTER VARIABLE_MACRO (rhs PLUGIN.COMMAND wins) and
                // AFTER REPEAT (index-capture form wins).  Anything else of the form
                // "identifier ?= <value>" is a direct string initialisation.
                if (true == usyntax::m_isVarMacroInit(strCommand)) {
                    eToken = Token::VAR_MACRO_INIT;
                    break;
                }

                if (true == usyntax::m_isCommand(strCommand)) {
                    eToken = Token::COMMAND;
                    break;
                }

                if (true == usyntax::m_isIfGoToCondition(strCommand)) {
                    eToken = Token::IF_GOTO_LABEL;
                    break;
                }

                if (true == usyntax::m_isLabel(strCommand)) {
                    eToken = Token::LABEL;
                    break;
                }

                if (true == usyntax::m_isEndRepeat(strCommand)) {
                    eToken = Token::END_REPEAT;
                    break;
                }

                if (true == usyntax::m_isBreak(strCommand)) {
                    eToken = Token::BREAK_LOOP;
                    break;
                }

                if (true == usyntax::m_isContinue(strCommand)) {
                    eToken = Token::CONTINUE_LOOP;
                    break;
                }

                if (true == usyntax::m_isPrint(strCommand)) {
                    eToken = Token::PRINT_STMT;
                    break;
                }

                if (true == usyntax::m_isDelay(strCommand)) {
                    eToken = Token::DELAY_STMT;
                    break;
                }

                if (true == usyntax::m_isBreakpoint(strCommand)) {
                    eToken = Token::BREAKPOINT_STMT;
                    break;
                }

                if (true == usyntax::m_isGeneratorStopAll(strCommand)) {
                    eToken = Token::GENERATOR_STOP_ALL_STMT;
                    break;
                }

                eToken   = Token::INVALID;
                bRetVal = false;

            } while (false);

            auto lineNr = ustring::fmtLineNr(iLineNumber);
            LOG_PRINT(LOG_WERBOSE, LOG_HDR; LOG_STRING(lineNr.data());
                      LOG_STRING(strCommand);
                      LOG_STRING("->");
                      LOG_STRING(getTokenTypeName(eToken)));

            return bRetVal;
        }
};

#endif // U_SCRIPT_COMMAND_VALIDATOR_HPP
