#ifndef U_SCRIPT_INTERPRETER_HPP
#define U_SCRIPT_INTERPRETER_HPP

#include "uSharedConfig.hpp"
#include "uScriptDataTypes.hpp"

#include "IScriptInterpreterShell.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"

#include "uBoolExprEvaluator.hpp"
#include "uIniCfgLoader.hpp"
#include "uPluginLoader.hpp"
#include "uNumeric.hpp"

#include <string>
#include <string_view>

class ScriptInterpreter : public IScriptInterpreterShell<ScriptEntriesType>
{

public:

    explicit ScriptInterpreter(IniCfgLoader&& loader)
                : m_IniCfgLoader(std::move(loader))
                , m_PluginLoader(PluginPathGenerator(SCRIPT_PLUGINS_PATH, PLUGIN_PREFIX, SCRIPT_PLUGIN_EXTENSION),
                                 PluginEntryPointResolver(SCRIPT_PLUGIN_ENTRY_POINT_NAME, SCRIPT_PLUGIN_EXIT_POINT_NAME))
    {
        if (m_IniCfgLoader.loadSection(SCRIPT_INI_SECTION_NAME)) {
            m_IniCfgLoader.getNumFromIni (SCRIPT_INI_CMD_EXEC_DELAY,m_szDelay);
        }
    }

    bool interpretScript(ScriptEntriesType& sScriptEntries);
	
    bool listMacrosPlugins();
    bool listCommands();
    bool loadPlugin(const std::string& strPluginName, bool bInitEnable);
    bool executeCmd(const std::string& strCommand);

private:

    bool m_loadPlugin(PluginDataType& command, bool bInitEnable) noexcept;
    bool m_loadPlugins () noexcept;
    bool m_crossCheckCommands() noexcept;
    bool m_initPlugins() noexcept;
    void m_enablePlugins() noexcept;
    void m_replaceVariableMacros(std::string& input);
    bool m_retrieveScriptSettings() noexcept;
    bool m_executeScript() noexcept;
    bool m_executeCommand(ScriptCommandType& data, bool bRealExec) noexcept;
    bool m_executeCommands(bool bRealExec) noexcept;
    bool m_pluginIsLoaded(const std::string& strPluginName) noexcept;

    // members initialized in the initialization list
    IniCfgLoader m_IniCfgLoader;
    BoolExprEvaluator m_beEvaluator;
    PluginLoaderFunctor<PluginInterface> m_PluginLoader;

    // members (internals)
    bool m_bIniConfigAvailable = true;
    size_t m_szDelay = 0U;
    ScriptEntriesType *m_sScriptEntries = nullptr;
    std::string m_strSkipUntilLabel;

    // additional map with variable macros added by the shell
    std::unordered_map<std::string, std::string> m_ShellVarMacros;
};

#endif // U_SCRIPT_INTERPRETER_HPP
