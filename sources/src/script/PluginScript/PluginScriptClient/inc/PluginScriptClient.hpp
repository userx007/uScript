#ifndef PLUGINSCRIPTCLIENT_HPP
#define PLUGINSCRIPTCLIENT_HPP

#include "ScriptReader.hpp"            // reuse the same script reader
#include "ScriptRunner.hpp"            // reuse the same script runner
#include "PluginScriptItemValidator.hpp"
#include "PluginScriptValidator.hpp"
#include "PluginScriptInterpreter.hpp"
#include "IPluginScriptDataTypes.hpp"

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

        explicit PluginScriptClient(const std::string& strScriptPathName)
            : m_shpPluginScriptRunner (std::make_shared<ScriptRunner<PluginScriptEntriesType>>
                                        (
                                            std::make_shared<ScriptReader>(strScriptPathName),
                                            std::make_shared<PluginScriptValidator>(std::make_shared<PluginScriptItemValidator>()),
                                            std::make_shared<PluginScriptInterpreter>()
                                        )
                                      )
        {}

        bool execute()
        {
            Timer timer("LOCAL_SCRIPT");
            return m_shpPluginScriptRunner->runScript();
        }

    private:

        std::shared_ptr<ScriptRunner<PluginScriptEntriesType>> m_shpPluginScriptRunner;

};


#endif // PLUGINSCRIPTCLIENT_HPP
