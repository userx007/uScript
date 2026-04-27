#ifndef U_COMM_SCRIPT_CLIENT_HPP
#define U_COMM_SCRIPT_CLIENT_HPP

#include "uSharedConfig.hpp"
#include "ICommDriver.hpp"

#include "uScriptReader.hpp"
#include "uCommScriptRunner.hpp"

#include "uCommScriptDataTypes.hpp"
#include "uCommScriptCommandValidator.hpp"
#include "uCommScriptValidator.hpp"
#include "uCommScriptInterpreter.hpp"

#include "uTimer.hpp"
#include "uLogger.hpp"
#include "uGuiNotify.hpp"

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

#define LT_HDR     "COMMSCRIPT_C|"
#define LOG_HDR    LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                    CLASS DECLARATION / DEFINITION                           //
/////////////////////////////////////////////////////////////////////////////////

template <typename TDriver>
class CommScriptClient
{
    public:

        explicit CommScriptClient(
            const std::string& strScriptPathName,
            std::shared_ptr<const TDriver> shpDriver,
            size_t szMaxRecvSize = PLUGIN_DEFAULT_RECEIVE_SIZE,
            uint32_t u32DefaultTimeout = 5000,
            size_t szDelay = PLUGIN_SCRIPT_DEFAULT_CMDS_DELAY
        )
            : m_shpCommScriptRunner(std::make_shared<CommScriptRunner<CommCommandsType, TDriver>>(
                std::make_shared<ScriptReader>(strScriptPathName),
                std::make_shared<CommScriptValidator>(std::make_shared<CommScriptCommandValidator>()),
                std::make_shared<CommScriptInterpreter<TDriver>>(shpDriver, szMaxRecvSize, u32DefaultTimeout, szDelay, strScriptPathName)
              ))
            , m_strScriptPathName(strScriptPathName)
        {}

        bool execute(bool bRealExec)
        {
            static const char *pstrCtx = "COMM script";
            utime::Timer timer(pstrCtx);

            if (!bRealExec) {
                /* Core dry-run pass: read, validate, and populate the per-path
                 * snapshot cache in CommScriptInterpreter. No device I/O occurs. */
                return m_shpCommScriptRunner->runScript(pstrCtx, false, false);
            }

            /* Real-execution pass (called by the plugin on every REPEAT iteration).
             *
             * The snapshot cache in CommScriptInterpreter is keyed by script path
             * and was populated during the core dry-run pass via execute(false).
             * If it already has an entry for this path, skip straight to execution.
             * Only fall back to a local dry-run if somehow the cache is cold
             * (e.g. execute(true) called without a prior execute(false)). */
            if (!CommScriptInterpreter<TDriver>::isCached(m_strScriptPathName)) {
                LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                          LOG_STRING("Cache miss — preparing snapshot for:");
                          LOG_STRING(m_strScriptPathName));
                if (false == m_shpCommScriptRunner->runScript(pstrCtx, false, false)) {
                    return false;
                }
            }

            gui_notify_load_comm(m_strScriptPathName);
            bool bResult = m_shpCommScriptRunner->runScript(pstrCtx, true, false);
            gui_notify_clear_comm();

            return bResult;
        }

    private:

        std::shared_ptr<CommScriptRunner<CommCommandsType, TDriver>> m_shpCommScriptRunner;
        std::string m_strScriptPathName;
};


#endif // U_COMM_SCRIPT_CLIENT_HPP
