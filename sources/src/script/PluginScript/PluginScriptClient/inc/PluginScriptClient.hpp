#ifndef PLUGINSCRIPTCLIENT_HPP
#define PLUGINSCRIPTCLIENT_HPP

#include "ScriptReader.hpp"            // reuse the same script reader
#include "ScriptRunner.hpp"            // reuse the same script runner
#include "PluginItemValidator.hpp"
#include "PluginScriptValidator.hpp"
#include "UartPluginScriptInterpreter.hpp"


#include "uTimer.hpp"

#include <string>
#include <memory>


class UartPluginScriptClient
{
    public:

        UartPluginScriptClient(const std::string& strScriptPathName, const std::string& strIniPathName = "")
            : m_shpPluginScriptRunner (std::make_shared<ScriptRunner> (
                                            std::make_shared<ScriptReader>(strScriptPathName),
                                            std::make_shared<PluginScriptValidator>(std::make_shared<PluginItemValidator>()),
                                            std::make_shared<UartPluginScriptInterpreter>(strIniPathName)
                                        )
                                      )
        {}

        bool execute()
        {
            Timer timer("UART_PLUGIN_SCRIPT");
            return m_shpPluginScriptRunner->runScript();
        }

    private:

        std::shared_ptr<ScriptRunner> m_shpPluginScriptRunner;

};


#endif // PLUGINSCRIPTCLIENT_HPP
