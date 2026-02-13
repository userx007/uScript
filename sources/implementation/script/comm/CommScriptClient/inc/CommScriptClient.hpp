#ifndef COMMSCRIPTCLIENT_HPP
#define COMMSCRIPTCLIENT_HPP

#include "SharedSettings.hpp"
#include "ICommDriver.hpp"

#include "ScriptReader.hpp"            // reuse the same script reader
#include "ScriptRunnerComm.hpp"            

#include "CommScriptDataTypes.hpp"
#include "CommScriptCommandValidator.hpp"
#include "CommScriptValidator.hpp"
#include "CommScriptInterpreter.hpp"

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

        using SendFunc = ICommDriver::SendFunc<TDriver>;
        using RecvFunc = ICommDriver::RecvFunc<TDriver>;

        explicit CommScriptClient (   const std::string& strScriptPathName,
                                        std::shared_ptr<const TDriver> shpDriver,
                                        SendFunc pfsend = SendFunc{},
                                        RecvFunc pfrecv = RecvFunc{},
                                        size_t szDelay = PLUGIN_SCRIPT_DEFAULT_CMDS_DELAY,
                                        size_t szMaxRecvSize = PLUGIN_DEFAULT_RECEIVE_SIZE
                                    )
            : m_shpCommScriptRunner ( std::make_shared<ScriptRunnerComm<CommScriptEntriesType, TDriver>>
                                        (
                                            std::make_shared<ScriptReader>(strScriptPathName),
                                            std::make_shared<CommScriptValidator>(std::make_shared<CommScriptCommandValidator>()),
                                            std::make_shared<CommScriptInterpreter<const TDriver>>(shpDriver, pfsend, pfrecv, szDelay, szMaxRecvSize)
                                        )
                                      )
        {}

        bool execute()
        {
            utime::Timer timer("PLUGIN_SCRIPT");
            return m_shpCommScriptRunner->runScript();
        }

    private:

        std::shared_ptr<ScriptRunnerComm<CommScriptEntriesType, TDriver>> m_shpCommScriptRunner;

};


#endif // COMMSCRIPTCLIENT_HPP
