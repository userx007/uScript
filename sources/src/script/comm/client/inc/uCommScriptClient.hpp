#ifndef U_COMM_SCRIPT_CLIENT_HPP
#define U_COMM_SCRIPT_CLIENT_HPP

#include "uSharedConfig.hpp"
#include "ICommDriver.hpp"

#include "ScriptReader.hpp"            // reuse the same script reader
#include "ScriptRunnerComm.hpp"            

#include "uCommScriptDataTypes.hpp"
#include "uCommScriptCommandValidator.hpp"
#include "CommScriptValidator.hpp"
#include "uCommScriptInterpreter.hpp"

#include "uTimer.hpp"
#include "uLogger.hpp"

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

#define LT_HDR     "PSCLIENT   :"
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
            : m_shpCommScriptRunner(std::make_shared<ScriptRunnerComm<CommScriptEntriesType, TDriver>>(
                std::make_shared<ScriptReader>(strScriptPathName),
                std::make_shared<CommScriptValidator>(std::make_shared<CommScriptCommandValidator>()),
                std::make_shared<CommScriptInterpreter<TDriver>>(shpDriver, szMaxRecvSize, u32DefaultTimeout, szDelay)
            ))
        {}

        bool execute()
        {
            utime::Timer timer("PLUGIN_SCRIPT");
            return m_shpCommScriptRunner->runScript();
        }

    private:

        std::shared_ptr<ScriptRunnerComm<CommScriptEntriesType, TDriver>> m_shpCommScriptRunner;

};


#endif // U_COMM_SCRIPT_CLIENT_HPP
