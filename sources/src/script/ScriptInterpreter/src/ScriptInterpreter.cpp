
#include "ScriptInterpreter.hpp"
#include "ItemValidator.hpp"    // to validate items from the shell input
#include "IScriptDataTypes.hpp" // to execute shell input
#include "uBoolExprParser.hpp"
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

        // only validate commands
        if (false == m_executeCommands(false)) {
            break;
        }

        m_enablePlugins();

        // real-execute commands
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

bool ScriptInterpreter::listItems()
{
    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("----- cmacros -----"));
    std::for_each(m_sScriptEntries->mapMacros.begin(), m_sScriptEntries->mapMacros.end(),
        [&](auto& cmacro) {
            LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING(cmacro.first); LOG_STRING(":"); LOG_STRING(cmacro.second));
        });

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

    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("----- plugins -----"));
    std::for_each(m_sScriptEntries->vPlugins.begin(), m_sScriptEntries->vPlugins.end(),
        [&](auto& plugin) {
            LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING(plugin.strPluginName); LOG_STRING("|"); LOG_STRING(plugin.sGetParams.strPluginVersion); LOG_STRING("|"); LOG_STRING(ustring::joinStrings(plugin.sGetParams.vstrPluginCommands)));
        });

    return true;
}


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
}



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::executeCmd(const std::string& strCommand)
{
    bool bRetVal = true;

    std::string strLocal(strCommand);

    m_replaceConstantMacros(strLocal);

    Token token;
    ItemValidator validator;
    ScriptCommandType data;

    if (true == validator.validateItem(strLocal, token)) {
        switch(token) {

            case Token::CONSTANT_MACRO : {
                std::vector<std::string> vstrTokens;
                ustring::tokenize(strLocal, SCRIPT_CONSTANT_MACRO_SEPARATOR, vstrTokens);

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
                ustring::tokenizeEx(strLocal, vstrDelimiters, vstrTokens);
                size_t szSize = vstrTokens.size();

                if ((szSize == 3) || (szSize == 4)) {
                    ScriptCommandType data {
                        //          |  plugin     |    command    |            params                           |vmacroname   | vmacroval |
                        MacroCommand{vstrTokens[1], vstrTokens[2], (vstrTokens.size() == 4) ? vstrTokens[3] : "", vstrTokens[0], ""}
                    };
                    m_executeCommand(data, true);
                    m_sScriptEntries->vCommands.emplace_back(data);
                } else {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid vmacro"));
                    bRetVal = false;
                }

                break;
            }

            case Token::COMMAND : {
                std::vector<std::string> vstrDelimiters{SCRIPT_PLUGIN_COMMAND_SEPARATOR, SCRIPT_COMMAND_PARAMS_SEPARATOR};
                std::vector<std::string> vstrTokens;
                ustring::tokenizeEx(strLocal, vstrDelimiters, vstrTokens);
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

    //m_executeCommand(data, true);

    return bRetVal;
}



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_retrieveSettings() noexcept
{
    return true;
}



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_loadPlugin(PluginDataType& item) noexcept
{
    bool bRetVal = false;

    do{
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
        if (m_bIniConfigAvailable) {
            if (m_IniParser.sectionExists(item.strPluginName)) {
                if (!m_IniParser.getResolvedSection(item.strPluginName, item.sSetParams.mapSettings)) {
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
    std::regex pattern(R"(\$([A-Za-z_][A-Za-z0-9_]*))");
    std::smatch match;
    std::string temp = input;

    while (std::regex_search(temp, match, pattern)) {
        std::string macroName = match[1];  // Extract macro name without "$"

        // Search for the corresponding value in vCommands from back to front
        for (auto it = m_sScriptEntries->vCommands.rbegin(); it != m_sScriptEntries->vCommands.rend(); ++it) {
            if (std::holds_alternative<MacroCommand>(*it)) {
                const auto& macroCommand = std::get<MacroCommand>(*it);
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
}



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

void ScriptInterpreter::m_replaceConstantMacros(std::string& input) noexcept
{
    ustring::replaceMacros(input, m_sScriptEntries->mapMacros, SCRIPT_MACRO_MARKER);

} // m_replaceConstantMacros()



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_executeCommand(ScriptCommandType& data, bool bRealExec ) noexcept
{
    bool bRetVal = true;

    std::visit([this, bRealExec, &bRetVal](auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, MacroCommand> || std::is_same_v<T, Command>) {
            if(m_strSkipUntilLabel.empty()) {
                for (auto& plugin : m_sScriptEntries->vPlugins) {
                    if (item.strPlugin == plugin.strPluginName) {
                        if(bRealExec) {
                            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Executing"); LOG_STRING(item.strPlugin + "." + item.strCommand + " " + item.strParams));
                        }
                        m_replaceVariableMacros(item.strParams);
                        if(bRealExec) {
                            LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Executing"); LOG_STRING(item.strPlugin + "." + item.strCommand + " " + item.strParams));
                            Timer timer("Command");
                        }
                        if (false == plugin.shptrPluginEntryPoint->doDispatch(item.strCommand, item.strParams)) {
                            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed executing"); LOG_STRING(item.strPlugin); LOG_STRING(item.strCommand); LOG_STRING("args["); LOG_STRING(item.strParams); LOG_STRING("]"));
                            bRetVal = false;
                            break;
                        } else {
                            if(bRealExec) {
                                if constexpr (std::is_same_v<T, MacroCommand>) {
                                    item.strVarMacroValue = plugin.shptrPluginEntryPoint->getData();
                                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("VMACRO["); LOG_STRING(item.strVarMacroName); LOG_STRING("] -> [") LOG_STRING(item.strVarMacroValue); LOG_STRING("]"));
                                    plugin.shptrPluginEntryPoint->resetData();
                                }
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
                    BoolExprParser beParser;
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

bool ScriptInterpreter::m_executeCommands (bool bRealExec ) noexcept
{
    bool bRetVal = true;

    for (auto& data : m_sScriptEntries->vCommands) {
        if(!m_executeCommand(data, bRealExec)) {
            bRetVal = false;
            break;
        }
    }

    return bRetVal;

} /* m_executeCommands() */

