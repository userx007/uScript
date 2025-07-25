#ifndef SCRIPTRUNNER_HPP
#define SCRIPTRUNNER_HPP

#include "IScriptRunner.hpp"
#include "IScriptReader.hpp"
#include "IScriptValidator.hpp"
#include "IScriptInterpreter.hpp"

#include "uLogger.hpp"

#include <vector>
#include <string>
#include <memory>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "S_RUNNER   :"
#define LOG_HDR    LOG_STRING(LT_HDR)


/////////////////////////////////////////////////////////////////////////////////
//                    CLASS DECLARATION / DEFINITION                           //
/////////////////////////////////////////////////////////////////////////////////


template<typename TScriptEntries, typename TDriver = void>
class ScriptRunner : public IScriptRunner
{
public:

    explicit ScriptRunner( std::shared_ptr<IScriptReader> shpScriptReader,
                           std::shared_ptr<IScriptValidator<TScriptEntries>> shvScriptValidator,
                           std::shared_ptr<IScriptInterpreter<TScriptEntries, TDriver>> shvScriptInterpreter )
        : m_shpScriptReader(std::move(shpScriptReader))
        , m_shvScriptValidator(std::move(shvScriptValidator))
        , m_shvScriptInterpreter(std::move(shvScriptInterpreter))
    {}

    bool runScript() override
    {
        bool bRetVal = false;

        do {

            std::vector<std::string> vstrScriptLines;
            TScriptEntries sScriptEntries;

            if (false == m_shpScriptReader->readScript(vstrScriptLines)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to read script"));
                break;
            }

            if (false == m_shvScriptValidator->validateScript(vstrScriptLines, sScriptEntries)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to validate script"));
                break;
            }

            if (false == m_shvScriptInterpreter->interpretScript(sScriptEntries)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to interpret script"));
                break;
            }

            bRetVal = true;

        } while(false);

        LOG_PRINT(((true == bRetVal) ? LOG_VERBOSE : LOG_ERROR), LOG_HDR; LOG_STRING(__FUNCTION__); LOG_STRING("->"); LOG_STRING((true == bRetVal) ? "OK" : "FAILED"));

        return bRetVal;

    }

private:

    std::shared_ptr<IScriptReader> m_shpScriptReader;
    std::shared_ptr<IScriptValidator<TScriptEntries>> m_shvScriptValidator;
    std::shared_ptr<IScriptInterpreter<TScriptEntries, TDriver>> m_shvScriptInterpreter;
};

#endif // SCRIPTRUNNER_HPP
