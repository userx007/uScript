#ifndef PLUGINSCRIPTCLIENT_HPP
#define PLUGINSCRIPTCLIENT_HPP

#include "ScriptReader.hpp"            // reuse the same script reader
#include "ScriptRunner.hpp"            // reuse the same script runner
#include "PluginScriptItemValidator.hpp"
#include "PluginScriptValidator.hpp"
#include "PluginScriptInterpreter.hpp"

#include "uTimer.hpp"

#include <string>
#include <memory>


class PluginScriptClient
{
    public:

        explicit PluginScriptClient(const std::string& strScriptPathName, const std::string& strIniPathName = "")
                                    : m_shpPluginScriptRunner (std::make_shared<ScriptRunner>
                                        (
                                            std::make_shared<ScriptReader>(strScriptPathName),
                                            std::make_shared<PluginScriptValidator>(std::make_shared<PluginScriptItemValidator>()),
                                            std::make_shared<PluginScriptInterpreter>(strIniPathName)
                                        )
                                      )
        {}

        bool execute()
        {
            Timer timer("PLUGIN_SCRIPT");
            return m_shpPluginScriptRunner->runScript();
        }

    private:

        std::shared_ptr<ScriptRunner> m_shpPluginScriptRunner;

};


#endif // PLUGINSCRIPTCLIENT_HPP
