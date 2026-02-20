#ifndef U_SCRIPT_RUNNER_HPP
#define U_SCRIPT_RUNNER_HPP

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

/**
 * @brief Basic script runner without communication driver dependency
 * 
 * Coordinates script reading, validation, and interpretation for scripts
 * that don't require device communication.
 * 
 * @tparam TScriptEntries Type representing script entries/commands
 */
template<typename TScriptEntries>
class ScriptRunner : public IScriptRunner
{
public:

    /**
     * @brief Construct a basic script runner
     * @param shpScriptReader Script reader component
     * @param shvScriptValidator Script validator component
     * @param shvScriptInterpreter Script interpreter component (Level 1)
     */
    explicit ScriptRunner( std::shared_ptr<IScriptReader> shpScriptReader,
                           std::shared_ptr<IScriptValidator<TScriptEntries>> shvScriptValidator,
                           std::shared_ptr<IScriptInterpreter<TScriptEntries>> shvScriptInterpreter )
        : m_shpScriptReader(std::move(shpScriptReader))
        , m_shpScriptValidator(std::move(shvScriptValidator))
        , m_shpScriptInterpreter(std::move(shvScriptInterpreter))
    {}

    bool runScript(bool bValidateOnly) override
    {
        bool bRetVal = false;

        do {

            std::vector<std::string> vstrScriptLines;
            TScriptEntries sScriptEntries;

            if (false == m_shpScriptReader->readScript(vstrScriptLines)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to read script"));
                break;
            }

            if (false == m_shpScriptValidator->validateScript(vstrScriptLines, sScriptEntries)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to validate script"));
                break;
            }

            if (false == bValidateOnly) {
                if (false == m_shpScriptInterpreter->interpretScript(sScriptEntries)) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to interpret script"));
                    break;
                }
            }

            bRetVal = true;

        } while(false);

        LOG_PRINT(((true == bRetVal) ? LOG_VERBOSE : LOG_ERROR), LOG_HDR; LOG_STRING(__FUNCTION__); LOG_STRING("->"); LOG_STRING((true == bRetVal) ? "OK" : "FAILED"));

        return bRetVal;

    }

protected:

    std::shared_ptr<IScriptReader> m_shpScriptReader;
    std::shared_ptr<IScriptValidator<TScriptEntries>> m_shpScriptValidator;
    std::shared_ptr<IScriptInterpreter<TScriptEntries>> m_shpScriptInterpreter;
};

#endif // U_SCRIPT_RUNNER_HPP
