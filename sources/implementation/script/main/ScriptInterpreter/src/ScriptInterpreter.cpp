
#include "ScriptInterpreter.hpp"
#include "ScriptItemValidator.hpp"    // to validate items from the shell input
#include "ScriptDataTypes.hpp"        // to execute shell input
#include "uBoolExprEvaluator.hpp"
#include "uString.hpp"
#include "uTimer.hpp"
#include "uLogger.hpp"

#include <regex>
#include <sstream>
#include <unordered_set>

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

        if (false == m_IniParser.load(m_strIniCfgPathName)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to load settings from:"); LOG_STRING(m_strIniCfgPathName));
            m_bIniConfigAvailable = false;
        } else {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Loaded settings from:"); LOG_STRING(m_strIniCfgPathName));
            if (false == m_retrieveScriptSettings()) {
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

        // only validate commands
        if (false == m_executeCommands(false)) {
            break;
        }

        // if plugins argument validation passed then we enable the pluggins for the real execution
        m_enablePlugins();

        // real-execute commands
        if (false == m_executeCommands(true)) {
            break;
        }

        bRetVal = true;

    } while(false);

    LOG_PRINT((bRetVal ? LOG_VERBOSE : LOG_ERROR), LOG_HDR; LOG_STRING("Script execution"); LOG_STRING(bRetVal ? "passed" : "failed"));

    return bRetVal;

} // interpretScript()


/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::listItems()
{
    if (!m_sScriptEntries->mapMacros.empty()) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("----- cmacros -----"));
        std::for_each(m_sScriptEntries->mapMacros.begin(), m_sScriptEntries->mapMacros.end(),
            [&](auto& cmacro) {
                LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING(cmacro.first); LOG_STRING(":"); LOG_STRING(cmacro.second));
            });
    }

    if (!m_sScriptEntries->vCommands.empty()) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("----- vmacros -----"));
        std::unordered_set<std::string> reportedMacros;
        std::for_each(m_sScriptEntries->vCommands.rbegin(), m_sScriptEntries->vCommands.rend(),
            [&](const auto& data) {
                std::visit([&](const auto& item) {
                    using T = std::decay_t<decltype(item)>;
                    if constexpr (std::is_same_v<T, MacroCommand>) {
                        const std::string& name = item.strVarMacroName;
                        if (reportedMacros.insert(name).second) {  // true if inserted (i.e., first occurrence)
                            LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING(name); LOG_STRING(":"); LOG_STRING(item.strVarMacroValue));
                        }
                    }
                }, data);
            }
        );
    }

    if (!m_ShellVarMacros.empty()) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("---vmacros-shell---"));
        std::for_each(m_ShellVarMacros.begin(), m_ShellVarMacros.end(),
            [&](auto& vmacro) {
                LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING(vmacro.first); LOG_STRING(":"); LOG_STRING(vmacro.second));
            });
    }

    if (!m_sScriptEntries->vPlugins.empty()) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("----- plugins -----"));
        std::for_each(m_sScriptEntries->vPlugins.begin(), m_sScriptEntries->vPlugins.end(),
            [&](auto& plugin) {
                LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING(plugin.strPluginName); LOG_STRING("|"); LOG_STRING(plugin.sGetParams.strPluginVersion); LOG_STRING("|"); LOG_STRING(ustring::joinStrings(plugin.sGetParams.vstrPluginCommands, ' ')));
            });
    }

    return true;

} // listItems()


/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::listCommands()
{
    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("----- commands -----"));
    std::for_each(m_sScriptEntries->vCommands.begin(), m_sScriptEntries->vCommands.end(),
        [&](const auto& data) {
            std::visit([&](const auto& item) {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<T, Command>) {
                    const std::vector<std::string> strInput{ item.strPlugin, item.strCommand, item.strParams };
                    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Command:"); LOG_STRING(ustring::joinStrings(strInput, "|")));
                }
                else if constexpr (std::is_same_v<T, MacroCommand>) {
                    const std::vector<std::string> strInput {item.strPlugin, item.strCommand, item.strParams, item.strVarMacroName, item.strVarMacroValue};
                    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("VMacroC:"); LOG_STRING(ustring::joinStrings(strInput, "|")));
                }
            }, data);
        }
    );

    return true;

} // listCommands()


/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::loadPlugin(const std::string& strPluginName)
{
    bool bRetVal = false;
    std::string strPluginNameUppecase = ustring::touppercase(strPluginName);

    PluginDataType item {
        strPluginNameUppecase,          // strPluginName
        "",                             // strPluginVersRule
        "",                             // strPluginVersRequested
        nullptr,                        // shptrPluginEntryPoint
        nullptr,                        // hLibHandle
        {},                             // sGetParams (empty PluginDataGet)
        {}                              // sSetParams (empty PluginDataSet)
    };

    if (true == (bRetVal = m_loadPlugin(item))) {
        m_sScriptEntries->vPlugins.emplace_back(item);
    }

    return bRetVal;

} // loadPlugin()


/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::executeCmd(const std::string& strCommand)
{
    bool bRetVal = true;

    std::string strCommandTemp(strCommand);

    ustring::replaceMacros(strCommandTemp, m_sScriptEntries->mapMacros, SCRIPT_MACRO_MARKER);
    ustring::replaceMacros(strCommandTemp, m_ShellVarMacros, SCRIPT_MACRO_MARKER);

    Token token;
    ScriptItemValidator validator;
    ScriptCommandType data;

    if (true == validator.validateItem(strCommandTemp, token)) {
        switch(token) {

            case Token::CONSTANT_MACRO : {
                std::vector<std::string> vstrTokens;
                ustring::tokenize(strCommandTemp, SCRIPT_CONSTANT_MACRO_SEPARATOR, vstrTokens);

                if (vstrTokens.size() == 2) {
                    // cmacroname := cmacroval                         | cmacroname |  cmacroval   |
                    auto aRetVal = m_sScriptEntries->mapMacros.emplace(vstrTokens[0], vstrTokens[1]);
                    if (false == aRetVal.second) {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CMacro already exists:"); LOG_STRING(vstrTokens[0]));
                        bRetVal = false;
                    }
                } else {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid cmacro"));
                    bRetVal = false;
                }
                break;
            }

            case Token::VARIABLE_MACRO : {
                std::vector<std::string> vstrDelimiters{SCRIPT_VARIABLE_MACRO_SEPARATOR, SCRIPT_PLUGIN_COMMAND_SEPARATOR, SCRIPT_COMMAND_PARAMS_SEPARATOR};
                std::vector<std::string> vstrTokens;
                ustring::tokenizeEx(strCommandTemp, vstrDelimiters, vstrTokens);
                size_t szSize = vstrTokens.size();

                if ((szSize == 3) || (szSize == 4)) {
                    ScriptCommandType data {
                        //          |  plugin     |    command   |            params                            |  vmacroname  | vmacroval |
                        MacroCommand{vstrTokens[1], vstrTokens[2], (vstrTokens.size() == 4) ? vstrTokens[3] : "", vstrTokens[0], ""}
                    };
                    m_executeCommand(data, true);
                    // the macro is stored in the dedicated map (override the previous values if the macro is reused)
                    if (!vstrTokens[0].empty()) {
                        auto aVar  = std::get<struct MacroCommand>(data);
                        m_ShellVarMacros[aVar.strVarMacroName] = aVar.strVarMacroValue;
                    }
                } else {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid vmacro"));
                    bRetVal = false;
                }

                break;
            }

            case Token::COMMAND : {
                std::vector<std::string> vstrDelimiters{SCRIPT_PLUGIN_COMMAND_SEPARATOR, SCRIPT_COMMAND_PARAMS_SEPARATOR};
                std::vector<std::string> vstrTokens;
                ustring::tokenizeEx(strCommandTemp, vstrDelimiters, vstrTokens);
                if (vstrTokens.size() >= 2) {
                    ScriptCommandType data {
                        Command{vstrTokens[0], vstrTokens[1], (vstrTokens.size() == 3) ? vstrTokens[2] : ""}
                    };
                    m_executeCommand(data, true);
                } else {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid command"));
                    bRetVal = false;
                }
                break;
            }

            default: {
                break;
            }
        };
    }

    return bRetVal;

} // executeCmd()



/////////////////////////////////////////////////////////////////////////////////
//                       PRIVATE INTERFACES                                    //
/////////////////////////////////////////////////////////////////////////////////


/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_retrieveScriptSettings() noexcept
{
    bool bRetVal = false;

    do {

        // check if the section exists
        if (true == m_IniParser.sectionExists(SCRIPT_INI_SECTION_NAME)) {
            if (false == m_IniParser.getResolvedSection(SCRIPT_INI_SECTION_NAME, m_mapSettings)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(SCRIPT_INI_SECTION_NAME); LOG_STRING(": failed to load settings from .ini file"));
                break;
            }
        } else {
            LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING(SCRIPT_INI_SECTION_NAME); LOG_STRING(": no settings in .ini file"));
            bRetVal = true;
            break;
        }

        // section exists, check if there is any content inside
        if (false == m_mapSettings.empty()) {
            if (m_mapSettings.count(SCRIPT_INI_CMD_EXEC_DELAY) > 0) {
                if(true == numeric::str2sizet(m_mapSettings.at(SCRIPT_INI_CMD_EXEC_DELAY), m_szDelay)) {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("cmd_delay :"); LOG_UINT64(m_szDelay));
                }
            }
        }

        bRetVal = true;

    } while(false);

    return bRetVal;

} // m_retrieveScriptSettings()



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_loadPlugin(PluginDataType& item) noexcept
{
    bool bRetVal = false;

    do {
        auto handle = m_PluginLoader(item.strPluginName);
        if (!(handle.first && handle.second))
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(item.strPluginName); LOG_STRING("-> loading failed"));
            break; // Exit early on failure
        }

        // Transfer the pointers to the internal storage
        item.hLibHandle = std::move(handle.first);
        item.shptrPluginEntryPoint = std::move(handle.second);

        // Retrieve data from plugin
        item.shptrPluginEntryPoint->getParams(&item.sGetParams);

        // get data to be set as params to plugin
        if (true == m_bIniConfigAvailable) {
            if (true == m_IniParser.sectionExists(item.strPluginName)) {
                if (false == m_IniParser.getResolvedSection(item.strPluginName, item.sSetParams.mapSettings)) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(item.strPluginName); LOG_STRING(": failed to load settings from .ini file"));
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

        bRetVal = true;

    } while(false);

    return bRetVal;

} // m_loadPlugin()



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_loadPlugins() noexcept
{
    bool bRetVal = true;

    for (auto& item : m_sScriptEntries->vPlugins) {
        if (false == m_loadPlugin(item)) {
            bRetVal = false;
            break;
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
 * Traverse the command list in reverse to resolve macros using their most recently assigned values.
-------------------------------------------------------------------------------*/

void ScriptInterpreter::m_replaceVariableMacros(std::string& input)
{
    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(__FUNCTION__); LOG_STRING(input));

    std::regex macroPattern(R"(\$([A-Za-z_][A-Za-z0-9_]*))");
    std::smatch match;

    bool replaced = true;
    while (replaced) {
        replaced = false;
        std::string result;
        std::string::const_iterator searchStart = input.cbegin();

        while (std::regex_search(searchStart, input.cend(), match, macroPattern)) {
            std::string macroName = match[1];
            result.append(match.prefix());

            bool found = false;
            for (auto it = m_sScriptEntries->vCommands.rbegin();
                 it != m_sScriptEntries->vCommands.rend(); ++it)
            {
                if (std::holds_alternative<MacroCommand>(*it)) {
                    const auto& macroCommand = std::get<MacroCommand>(*it);
                    if (macroCommand.strVarMacroName == macroName) {
                        result.append(macroCommand.strVarMacroValue);
                        found = true;
                        replaced = true;
                        break;
                    }
                }
            }
            if (!found) {
                result.append(match[0]);
            }
            searchStart = match.suffix().first;
        }
        result.append(searchStart, input.cend());
        input = result;
    }
}



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_executeCommand (ScriptCommandType& data, bool bRealExec) noexcept
{
    bool bRetVal = true;

    std::visit([this, bRealExec, &bRetVal](auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, MacroCommand> || std::is_same_v<T, Command>) {
            if (m_strSkipUntilLabel.empty()) {
                for (auto& plugin : m_sScriptEntries->vPlugins) {
                    if (item.strPlugin == plugin.strPluginName) {
                        if(bRealExec) { // real execution
                            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Executing"); LOG_STRING(item.strPlugin + "." + item.strCommand + " " + item.strParams));
                            m_replaceVariableMacros(item.strParams);
                            LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Executing"); LOG_STRING(item.strPlugin + "." + item.strCommand + " " + item.strParams));
                            if(true) { // dummy block to ensure correct command execution time measurement (separate from delay)
                                utime::Timer timer("COMMAND");
                                if (false == plugin.shptrPluginEntryPoint->doDispatch(item.strCommand, item.strParams)) {
                                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed executing"); LOG_STRING(item.strPlugin); LOG_STRING(item.strCommand); LOG_STRING("args["); LOG_STRING(item.strParams); LOG_STRING("]"));
                                    break;
                                } else { // execution succceded, update the value of the associated macro if any
                                    if constexpr (std::is_same_v<T, MacroCommand>) {
                                        item.strVarMacroValue = plugin.shptrPluginEntryPoint->getData();
                                        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("VMACRO["); LOG_STRING(item.strVarMacroName); LOG_STRING("] -> [") LOG_STRING(item.strVarMacroValue); LOG_STRING("]"));
                                        plugin.shptrPluginEntryPoint->resetData();
                                    }
                                }
                            }
                            utime::delay_ms(m_szDelay); /* delay between the commands execution */
                        } else { // only for validation purposes; execute the plugin command section only until [if(false == m_bIsEnabled)] statement
                            if (false == plugin.shptrPluginEntryPoint->doDispatch(item.strCommand, item.strParams)) {
                                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed validating"); LOG_STRING(item.strPlugin); LOG_STRING(item.strCommand); LOG_STRING("args["); LOG_STRING(item.strParams); LOG_STRING("]"));
                                break;
                            }
                        }
                    }
                }
            } else {
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Skipped:"); LOG_STRING(item.strPlugin); LOG_STRING(item.strCommand); LOG_STRING("args["); LOG_STRING(item.strParams); LOG_STRING("]"));
            }

        } else if constexpr (std::is_same_v<T, Condition>) {
            if(bRealExec) {
                if(m_strSkipUntilLabel.empty()) {
                    bool beResult = false;
                    BoolExprEvaluator beEvaluator;
                    if (true == beEvaluator.evaluate(item.strCondition, beResult)) {
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
            }

        } else if constexpr (std::is_same_v<T, Label>) {
            if(bRealExec) {
                if(m_strSkipUntilLabel == item.strLabelName) {
                    m_strSkipUntilLabel.clear(); // label found, reset the label so the further commands to be executed
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Stop skipping at label:"); LOG_STRING(item.strLabelName));
                }
            }
        }
    }, data);

    if (bRealExec && m_strSkipUntilLabel.empty()) {
        LOG_PRINT((bRetVal ? LOG_VERBOSE : LOG_ERROR), LOG_HDR; LOG_STRING("Command execution"); LOG_STRING(bRetVal ? "succeeded" : "failed"));
    }

    return bRetVal;

} // m_executeCommand()


/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_executeCommands (bool bRealExec) noexcept
{
    bool bRetVal = true;

    for (auto& data : m_sScriptEntries->vCommands) {
        if(false == m_executeCommand(data, bRealExec)) {
            bRetVal = false;
            break;
        }
    }

    return bRetVal;

} /* m_executeCommands() */


