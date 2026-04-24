#ifndef U_COMM_SCRIPT_CLIENT_HPP
#define U_COMM_SCRIPT_CLIENT_HPP

#include "uSharedConfig.hpp"
#include "ICommDriver.hpp"

#include "uScriptReader.hpp"            // reuse the same script reader
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
                std::make_shared<CommScriptInterpreter<TDriver>>(shpDriver, szMaxRecvSize, u32DefaultTimeout, szDelay)
              ))
            , m_strScriptPathName(strScriptPathName)   // stored for LOAD_COMM notification			
        {}

        bool execute(bool bRealExec)
        {
            static const char *pstrCtx = "COMM script";
            utime::Timer timer(pstrCtx);

            // Tell the GUI front-end to load the comm script file into w2 and
            // start tracking its execution.  Skipped on dry-run so the marker
            // only appears when lines actually execute.
            if (bRealExec) {
				gui_notify_load_comm(m_strScriptPathName);
			}

            bool bResult = m_shpCommScriptRunner->runScript(pstrCtx, bRealExec, false /*bUseDryRun*/);

            // Comm script done — tell the GUI to clear w2.
            if (bRealExec) {
				gui_notify_clear_comm();
			}

            return bResult;
        }

    private:
	
        std::shared_ptr<CommScriptRunner<CommCommandsType, TDriver>> m_shpCommScriptRunner;
        std::string m_strScriptPathName;    // kept for gui_notify_load_comm
};


#endif // U_COMM_SCRIPT_CLIENT_HPP
