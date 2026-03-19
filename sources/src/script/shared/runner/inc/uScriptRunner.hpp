#ifndef U_SCRIPT_RUNNER_HPP
#define U_SCRIPT_RUNNER_HPP

#include "IScriptRunner.hpp"
#include "IScriptReader.hpp"
#include "IScriptValidator.hpp"
#include "IScriptInterpreter.hpp"
#include "uScriptDataTypes.hpp"

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

#define LT_HDR     "SCR_RUN     |"
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

    bool runScript(const char *pstrCallCtx, bool bRealExec) override
    {
        bool bRetVal = false;

        do {

            std::vector<ScriptRawLine> vRawScriptLines;
            TScriptEntries sScriptEntries;

            LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Reading"); LOG_STRING(pstrCallCtx));
            if (false == m_shpScriptReader->readScript(vRawScriptLines)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Script reading failed"));
                break;
            }

            LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Validating"); LOG_STRING(pstrCallCtx));
            if (false == m_shpScriptValidator->validateScript(vRawScriptLines, sScriptEntries)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Script validation failed"));
                break;
            }

            if (true == bRealExec) {
                LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Interpreting"); LOG_STRING(pstrCallCtx));
                if (false == m_shpScriptInterpreter->interpretScript(sScriptEntries)) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Script interpretation failed"));
                    break;
                }
            }

            bRetVal = true;

        } while(false);

        LOG_PRINT(((true == bRetVal) ? LOG_INFO : LOG_ERROR), LOG_HDR; LOG_STRING(pstrCallCtx); LOG_STRING(bRealExec ? "execution" : "validation"); LOG_STRING((true == bRetVal) ? "ok" : "failed"));

        return bRetVal;

    }

protected:

    std::shared_ptr<IScriptReader> m_shpScriptReader;
    std::shared_ptr<IScriptValidator<TScriptEntries>> m_shpScriptValidator;
    std::shared_ptr<IScriptInterpreter<TScriptEntries>> m_shpScriptInterpreter;
};

#endif // U_SCRIPT_RUNNER_HPP
