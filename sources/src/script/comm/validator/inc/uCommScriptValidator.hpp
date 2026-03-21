#ifndef U_COMM_SCRIPT_VALIDATOR_HPP
#define U_COMM_SCRIPT_VALIDATOR_HPP

#include "IScriptValidator.hpp"
#include "IScriptCommandValidator.hpp"
#include "uScriptSyntax.hpp"
#include "uCommScriptDataTypes.hpp"
#include "uCommScriptCommandValidator.hpp"
#include "uLogger.hpp"

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "COMM_SCR_V  |"
#define LOG_HDR    LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                            CLASS DEFINITION                                 //
/////////////////////////////////////////////////////////////////////////////////

class CommScriptValidator : public IScriptValidator<CommCommandsType>
{
    public:

        explicit CommScriptValidator (std::shared_ptr<CommScriptCommandValidator> shpCommandValidator)
            : m_shpCommandValidator(std::move(shpCommandValidator))
        {}

        bool validateScript (std::vector<ScriptRawLine>& vRawLines, CommCommandsType& sScriptEntries) override
        {
            CommCommand token;
            m_sScriptEntries = &sScriptEntries;

            bool bRetVal = std::all_of(vRawLines.begin(), vRawLines.end(),
                [&](ScriptRawLine& rawLine) {

                    std::string& command = rawLine.strContent;

                    // replace the macros declared so far
                    ustring::replaceMacros(command, m_sScriptEntries->mapMacros, SCRIPT_MACRO_MARKER);

                    // validate as macro
                    if (true == usyntax::m_isConstantMacro(command)) {
                        std::vector<std::string> vstrTokens;
                        ustring::tokenize(command, SCRIPT_CONSTANT_MACRO_SEPARATOR, vstrTokens);
                        m_sScriptEntries->mapMacros.emplace(vstrTokens[0], vstrTokens[1]);
                        return true;
                    }

                    // validate as command
                    if (true == m_shpCommandValidator->validateCommand(rawLine.iLineNumber, command, token)) {
                        m_sScriptEntries->vCommands.emplace_back(token);
                        return true;
                    }

                    // none of expected
                    auto lineNr = ustring::fmtLineNr(rawLine.iLineNumber);
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                              LOG_STRING("Failed to validate ["); 
                              LOG_STRING(command); 
                              LOG_STRING("]"));
                    return false;

                });

            LOG_PRINT(((true == bRetVal) ? LOG_VERBOSE : LOG_ERROR), LOG_HDR; LOG_STRING("Comm script validation"); LOG_STRING((true == bRetVal) ? "ok" : "failed"));
            return bRetVal;
        }

    private:


        std::shared_ptr<IScriptCommandValidator<CommCommand>> m_shpCommandValidator;
        CommCommandsType *m_sScriptEntries = nullptr;

};

#endif //U_COMM_SCRIPT_VALIDATOR_HPP