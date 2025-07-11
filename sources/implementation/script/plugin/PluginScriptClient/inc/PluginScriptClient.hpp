#ifndef PLUGINSCRIPTCLIENT_HPP
#define PLUGINSCRIPTCLIENT_HPP

#include "CommonSettings.hpp"
#include "ScriptReader.hpp"            // reuse the same script reader
#include "ScriptRunner.hpp"            // reuse the same script runner

#include "PluginScriptDataTypes.hpp"
#include "PluginScriptItemValidator.hpp"
#include "PluginScriptValidator.hpp"
#include "PluginScriptInterpreter.hpp"

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

class PluginScriptClient
{
    public:

        explicit PluginScriptClient(const std::string& strScriptPathName, PFSEND pfsend = PFSEND{}, PFRECV pfrecv = PFRECV{}, size_t szDelay = PLUGIN_SCRIPT_DEFAULT_CMDS_DELAY, size_t szMaxRecvSize = PLUGIN_DEFAULT_RECEIVE_SIZE)
            : m_shpPluginScriptRunner (std::make_shared<ScriptRunner<PluginScriptEntriesType>>
                                        (
                                            std::make_shared<ScriptReader>(strScriptPathName),
                                            std::make_shared<PluginScriptValidator>(std::make_shared<PluginScriptItemValidator>()),
                                            std::make_shared<PluginScriptInterpreter>(pfsend, pfrecv, szDelay, szMaxRecvSize)
                                        )
                                      )
        {}

        bool execute()
        {
            utime::Timer timer("PLUGIN_SCRIPT");
            return m_shpPluginScriptRunner->runScript();
        }

    private:

        std::shared_ptr<ScriptRunner<PluginScriptEntriesType>> m_shpPluginScriptRunner;

};


#endif // PLUGINSCRIPTCLIENT_HPP
