#ifndef SCRIPTINTERPRETER_HPP
#define SCRIPTINTERPRETER_HPP

#include "CommonSettings.hpp"
#include "IScriptInterpreter.hpp"
#include "ScriptDataTypes.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"

#include "uPluginLoader.hpp"
#include "uIniParserEx.hpp"
#include "uNumeric.hpp"

#include <string>

class ScriptInterpreter : public IScriptInterpreter<ScriptEntriesType>
{

public:

    explicit ScriptInterpreter(const std::string& strIniPathName)
                : m_strIniCfgPathName(strIniPathName)
                , m_PluginLoader(PluginPathGenerator(SCRIPT_PLUGINS_PATH, PLUGIN_PREFIX, SCRIPT_PLUGIN_EXTENSION),
                                 PluginEntryPointResolver(SCRIPT_PLUGIN_ENTRY_POINT_NAME, SCRIPT_PLUGIN_EXIT_POINT_NAME))
    {
        m_mapSettings[SCRIPT_INI_CMD_EXEC_DELAY] = "0";
    }

    bool interpretScript(ScriptEntriesType& sScriptEntries) override;

    //--------------------------------------------------------------------
    // additional interfaces used to handle script elements from the shell
    //--------------------------------------------------------------------
    bool listItems() override;
    bool listCommands() override;
    bool loadPlugin(const std::string& strPluginName) override;
    bool executeCmd(const std::string& strCommand) override;

private:

    bool m_loadPlugin(PluginDataType& item) noexcept;
    bool m_loadPlugins () noexcept;
    bool m_crossCheckCommands() noexcept;
    bool m_initPlugins() noexcept;
    void m_enablePlugins() noexcept;
    void m_replaceVariableMacros(std::string& input);
    bool m_retrieveScriptSettings() noexcept;
    bool m_executeScript() noexcept;
    bool m_executeCommand(ScriptCommandType& data, bool bRealExec) noexcept;
    bool m_executeCommands(bool bRealExec) noexcept;

    // members (ini config related)
    bool m_bIniConfigAvailable = true;

    // delay between commands execution
    size_t m_szDelay = 0;

    // members (internals)
    ScriptEntriesType *m_sScriptEntries = nullptr;
    std::string m_strSkipUntilLabel;
    IniParserEx m_IniParser;
    PluginLoaderFunctor<PluginInterface> m_PluginLoader;
    std::string m_strIniCfgPathName;

    // map with the script related keys expected to be present in the ini file
    std::unordered_map<std::string, std::string> m_mapSettings;

    // additional map with variable macros added by the shell
    std::unordered_map<std::string, std::string> m_ShellVarMacros;

};

#endif // SCRIPTINTERPRETER_HPP
