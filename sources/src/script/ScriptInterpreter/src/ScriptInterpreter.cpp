#include "CommonSettings.hpp"
#include "IPlugin.hpp"
#include "ScriptInterpreter.hpp"
#include "uPluginLoader.hpp"

#include "uBoolExprParser.hpp"
#include "uString.hpp"
#include "uTimer.hpp"
#include "uLogger.hpp"

#include <regex>
#include <sstream>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////


#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "S_INTERPRET:"
#define LOG_HDR    LOG_STRING(LT_HDR)


/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::interpretScript(ScriptEntriesType& sScriptEntries)
{
    bool bRetVal = false;

    do {

        m_sScriptEntries = &sScriptEntries;
        if (false == m_IniParser.load(SCRIPT_INI_CONFIG)) {
            m_bIniConfigAvailable = false;
        } else {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Loaded settings from:"); LOG_STRING(SCRIPT_INI_CONFIG));
            if (false == m_retrieveSettings()) {
                break;
            }
        }

        if (false == m_loadPlugins()) {
            break;
        }

        if (false == m_crossCheckCommands()) {
            break;
        }

        if (false == m_initPlugins()) {
            break;
        }

        // pre-execute the commands with plugins not enabled
        if (false == m_executeCommands()) {
            break;
        }

        m_enablePlugins();

        // real-execute the commands with plugins enabled
        if (false == m_executeCommands()) {
            break;
        }

        bRetVal = true;

    } while(false);

    LOG_PRINT((bRetVal ? LOG_VERBOSE : LOG_ERROR), LOG_HDR; LOG_STRING("Script execution"); LOG_STRING(bRetVal ? "passed" : "failed"));

    return bRetVal;

}


/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::listScriptItems()
{
    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("----- cmacros -----"));
    std::for_each(m_sScriptEntries->mapMacros.begin(), m_sScriptEntries->mapMacros.end(),
        [&](auto& cmacro) {
            LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING(cmacro.first); LOG_STRING(":"); LOG_STRING(cmacro.second));
        });

    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("----- vmacros -----"));
    std::for_each(m_sScriptEntries->vCommands.begin(), m_sScriptEntries->vCommands.end(),
        [&](const auto& data) {
            std::visit([](const auto& item) {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<T, MacroCommand>) {
                    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING(item.strVarMacroName); LOG_STRING(":"); LOG_STRING(item.strVarMacroValue));
                }
            }, data);
        });

    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("----- commands ----"));
    std::for_each(m_sScriptEntries->vPlugins.begin(), m_sScriptEntries->vPlugins.end(),
        [&](auto& plugin) {
            LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING(plugin.strPluginName); LOG_STRING("|"); LOG_STRING(plugin.sGetParams.strPluginVersion); LOG_STRING("|"); LOG_STRING(ustring::joinStrings(plugin.sGetParams.vstrPluginCommands)));
        });

    return true;
}


/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_retrieveSettings() noexcept
{
    return true;
}


/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_loadPlugins() noexcept
{
    bool bRetVal = true;

    PluginLoaderFunctor<PluginInterface> loader(PluginPathGenerator(SCRIPT_PLUGINS_PATH, PLUGIN_PREFIX, SCRIPT_PLUGIN_EXTENSION),
                                                PluginEntryPointResolver(SCRIPT_PLUGIN_ENTRY_POINT_NAME, SCRIPT_PLUGIN_EXIT_POINT_NAME));

    for (auto& item : m_sScriptEntries->vPlugins) {
        auto handle = loader(item.strPluginName);
        if (handle.first && handle.second) {
            // Transfer the pointers to the internal storage
            item.hLibHandle = std::move(handle.first);
            item.shptrPluginEntryPoint = std::move(handle.second);

            // Retrieve data from plugin
            item.shptrPluginEntryPoint->getParams(&item.sGetParams);

            // get data to be set as params to plugin
            if (m_bIniConfigAvailable) {
                if (m_IniParser.sectionExists(item.strPluginName)) {
                    if (!m_IniParser.getResolvedSection(item.strPluginName, item.sSetParams.mapSettings)) {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(item.strPluginName); LOG_STRING(": failed to load settings from .ini file"));
                        bRetVal = false;
                        break; // Exit early on failure
                    }
                } else {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(item.strPluginName); LOG_STRING(": no settings in .ini file"));
                }
            }
            item.sSetParams.shpLogger = getLogger();
            // set parameters to plugin
            if (false == item.shptrPluginEntryPoint->setParams(&item.sSetParams)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(item.strPluginName); LOG_STRING(": failed to set params loaded from .ini file"));
                bRetVal = false;
                break; // Exit early on failure
            }

            // Lambda to print plugin info
            auto printPluginInfo =  [](const std::string& name, const std::string& version, const std::vector<std::string>& vs) {
                std::ostringstream oss;
                oss << name << " v" << version << " ";
                for (const auto& cmd : vs) {
                    oss << cmd << " ";
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(oss.str()); LOG_STRING("-> loaded ok"));
            };
            printPluginInfo(item.strPluginName, item.sGetParams.strPluginVersion, item.sGetParams.vstrPluginCommands);
        } else {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(item.strPluginName); LOG_STRING("-> loading failed"));
            bRetVal = false;
            break; // Exit early on failure
        }
    }

    LOG_PRINT((bRetVal ? LOG_VERBOSE : LOG_ERROR), LOG_HDR; LOG_STRING("Plugin loading"); LOG_STRING(bRetVal ? "passed" : "failed"));

    return bRetVal;

} // m_loadPlugins()



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_crossCheckCommands () noexcept
{
    bool bRetVal = true;

    for (const auto& data : m_sScriptEntries->vCommands) {
        std::visit([this, &bRetVal](const auto & item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, MacroCommand> || std::is_same_v<T, Command>) {
                for (auto& plugin : m_sScriptEntries->vPlugins) {
                    if (item.strPlugin == plugin.strPluginName) {
                        auto& commands = plugin.sGetParams.vstrPluginCommands;
                        if (std::find(commands.begin(), commands.end(), item.strCommand) == commands.end()) {
                            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Command") LOG_STRING(item.strCommand); LOG_STRING("unsupported by plugin"); LOG_STRING(plugin.strPluginName));
                            bRetVal = false;
                            break;
                        }
                    }
                }
            }
        }, data);
    }

    LOG_PRINT((bRetVal ? LOG_VERBOSE : LOG_ERROR), LOG_HDR; LOG_STRING("Commands check"); LOG_STRING(bRetVal ? "passed" : "failed"));

    return bRetVal;

} // m_crossCheckCommands()



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_initPlugins () noexcept
{
    bool bRetVal = true;

    for (const auto& plugin : m_sScriptEntries->vPlugins) {
        if (false == plugin.shptrPluginEntryPoint->doInit( (true == plugin.shptrPluginEntryPoint->isPrivileged()) ? this : nullptr)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to initialize plugin:"); LOG_STRING(plugin.strPluginName));
            bRetVal = false;
            break;
        }
    }

    LOG_PRINT((bRetVal ? LOG_VERBOSE : LOG_ERROR), LOG_HDR; LOG_STRING("Plugins initialization"); LOG_STRING(bRetVal ? "passed" : "failed"));

    return bRetVal;

} // m_initPlugins()



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

void ScriptInterpreter::m_enablePlugins () noexcept
{
    std::for_each(m_sScriptEntries->vPlugins.begin(), m_sScriptEntries->vPlugins.end(),
        [&](auto & plugin) {
            plugin.shptrPluginEntryPoint->doEnable();
        });

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Plugins enabling passed"));

} // m_enablePlugins()



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

void ScriptInterpreter::m_replaceVariableMacros(std::string& input)
{
    std::regex pattern(R"(\$([A-Za-z_][A-Za-z0-9_]*))");
    std::smatch match;
    std::string temp = input;

    while (std::regex_search(temp, match, pattern)) {
        std::string macroName = match[1];  // Extract macro name without "$"

        // Search for the corresponding value in vCommands
        for (const auto& command : m_sScriptEntries->vCommands) {
            if (std::holds_alternative<MacroCommand>(command)) {
                const auto& macroCommand = std::get<MacroCommand>(command);
                if (macroCommand.strVarMacroName == macroName) {
                    std::string macroPattern = "$" + macroName;
                    std::string macroValue = macroCommand.strVarMacroValue;

                    size_t pos = 0;
                    while ((pos = input.find(macroPattern, pos)) != std::string::npos) {
                        input.replace(pos, macroPattern.length(), macroValue);
                        pos += macroValue.length();
                    }
                    break;  // Stop searching once found
                }
            }
        }

        temp = match.suffix().str();  // Move forward in the string
    }

} // m_replaceVariableMacros()


/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_executeCommands () noexcept
{

    bool bRetVal = true;
    Timer timer("COMMANDS");

    for (auto& data : m_sScriptEntries->vCommands) {
        std::visit([this, &bRetVal](auto & item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, MacroCommand> || std::is_same_v<T, Command>) {
                if(m_strSkipUntilLabel.empty()) {
                    for (auto& plugin : m_sScriptEntries->vPlugins) {
                        if (item.strPlugin == plugin.strPluginName) {
                            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Executing"); LOG_STRING(item.strPlugin + "." + item.strCommand + " " + item.strParams));
                            m_replaceVariableMacros(item.strParams);
                            LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Executing"); LOG_STRING(item.strPlugin + "." + item.strCommand + " " + item.strParams));
                            Timer timer("Command");
                            if (false == plugin.shptrPluginEntryPoint->doDispatch(item.strCommand, item.strParams)) {
                                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed executing"); LOG_STRING(item.strPlugin); LOG_STRING(item.strCommand); LOG_STRING("args["); LOG_STRING(item.strParams); LOG_STRING("]"));
                                bRetVal = false;
                                break;
                            } else {
                                if constexpr (std::is_same_v<T, MacroCommand>) {
                                    item.strVarMacroValue = plugin.shptrPluginEntryPoint->getData();
                                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("VMACRO["); LOG_STRING(item.strVarMacroName); LOG_STRING("] -> [") LOG_STRING(item.strVarMacroValue); LOG_STRING("]"));
                                    plugin.shptrPluginEntryPoint->resetData();
                                }
                            }
                        }
                    }
                } else {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Skipped:"); LOG_STRING(item.strPlugin); LOG_STRING(item.strCommand); LOG_STRING("args["); LOG_STRING(item.strParams); LOG_STRING("]"));
                }

            } else if constexpr (std::is_same_v<T, Condition>) {
                if(m_strSkipUntilLabel.empty()) {
                    BoolExprParser beParser;
                    bool beResult = false;
                    if (true == beParser.evaluate(item.strCondition, beResult)) {
                        if (true == beResult) {
                            m_strSkipUntilLabel = item.strLabelName; // set the label to start skipping the execution
                            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Start skipping to label:"); LOG_STRING(m_strSkipUntilLabel));
                        }
                    } else {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to evaluate condition:"); LOG_STRING(item.strCondition));
                        bRetVal = false;
                    }
                } else {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Skipped:"); LOG_STRING("[IF ..] GOTO:"); LOG_STRING(item.strLabelName));
                }

            } else if constexpr (std::is_same_v<T, Label>) {
                if(m_strSkipUntilLabel == item.strLabelName) {
                    m_strSkipUntilLabel.clear(); // label found, reset the label so the further commands to be executed
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Stop skipping at label:"); LOG_STRING(item.strLabelName));
                }
            }
        }, data);
    }

    LOG_PRINT((bRetVal ? LOG_VERBOSE : LOG_ERROR), LOG_HDR; LOG_STRING("Command execution"); LOG_STRING(bRetVal ? "succeeded" : "failed"));

    return bRetVal;

} // m_executeCommands()
