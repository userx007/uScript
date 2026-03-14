#include "uScriptInterpreter.hpp"
#include "uScriptCommandValidator.hpp"    
#include "uScriptDataTypes.hpp"        
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

#define LT_HDR     "CORE_SCR_I  |"
#define LOG_HDR    LOG_STRING(LT_HDR)


/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::interpretScript(ScriptEntriesType& sScriptEntries)
{
    bool bRetVal = false;

    do {

        m_sScriptEntries = &sScriptEntries;

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

bool ScriptInterpreter::listMacrosPlugins()
{
    if (!m_sScriptEntries->mapMacros.empty()) {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--- cmacros"));
        std::for_each(m_sScriptEntries->mapMacros.begin(), m_sScriptEntries->mapMacros.end(),
            [&](auto& cmacro) {
                LOG_PRINT(LOG_EMPTY, LOG_STRING(cmacro.first); LOG_STRING(":"); LOG_STRING(cmacro.second));
            });
    }

    if (!m_sScriptEntries->vCommands.empty()) {
        std::unordered_set<std::string> reportedMacros;
        bool bHeaderPrinted = false;
        std::for_each(m_sScriptEntries->vCommands.rbegin(), m_sScriptEntries->vCommands.rend(),
            [&](const auto& data) {
                std::visit([&](const auto& command) {
                    using T = std::decay_t<decltype(command)>;
                    if constexpr (std::is_same_v<T, MacroCommand>) {
                        const std::string& name = command.strVarMacroName;
                        if (reportedMacros.insert(name).second) {
                            if (!bHeaderPrinted) {
                                LOG_PRINT(LOG_EMPTY, LOG_STRING("--- vmacros"));
                                bHeaderPrinted = true;
                            }
                            LOG_PRINT(LOG_EMPTY, LOG_STRING(name); LOG_STRING(":"); LOG_STRING(command.strVarMacroValue));
                        }
                    }
                }, data.command);
            }
        );
    }

    if (!m_ShellVarMacros.empty()) {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--- vmacros-shell"));
        std::for_each(m_ShellVarMacros.begin(), m_ShellVarMacros.end(),
            [&](auto& vmacro) {
                LOG_PRINT(LOG_EMPTY, LOG_STRING(vmacro.first); LOG_STRING(":"); LOG_STRING(vmacro.second));
            });
    }

    // Show any live loop-scope macros (only meaningful when called during execution).
    // Printed outermost → innermost so nesting depth is visually obvious.
    if (!m_loopStateStack.empty()) {
        for (size_t iDepth = 0; iDepth < m_loopStateStack.size(); ++iDepth) {
            const auto& scope = m_loopStateStack[iDepth];
            if (!scope.mapLoopMacros.empty()) {
                LOG_PRINT(LOG_EMPTY, LOG_STRING("--- vmacros-loop["); LOG_STRING(scope.strLabel); LOG_STRING("]"));
                std::for_each(scope.mapLoopMacros.begin(), scope.mapLoopMacros.end(),
                    [](const auto& vmacro) {
                        LOG_PRINT(LOG_EMPTY, LOG_STRING(vmacro.first); LOG_STRING(":"); LOG_STRING(vmacro.second));
                    });
            }
        }
    }

    if (!m_sScriptEntries->vPlugins.empty()) {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--- plugins"));
        std::for_each(m_sScriptEntries->vPlugins.begin(), m_sScriptEntries->vPlugins.end(),
            [&](auto& plugin) {
                LOG_PRINT(LOG_EMPTY, LOG_STRING([&]{ std::ostringstream o; o << std::right << std::setw(12) << plugin.strPluginName; return o.str(); }()); 
                    LOG_STRING("|"); LOG_STRING(plugin.sGetParams.strPluginVersion); 
                    LOG_STRING("|"); LOG_STRING(ustring::joinStrings(plugin.sGetParams.vstrPluginCommands, ' ')));
            });
    }

    return true;

} // listMacrosPlugins()


/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::listCommands()
{
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--- commands"));
    std::for_each(m_sScriptEntries->vCommands.begin(), m_sScriptEntries->vCommands.end(),
        [&](const ScriptLine& data) {
            std::visit([&data](const auto& command) {
                using T = std::decay_t<decltype(command)>;
                const std::string strLine = "[L" + std::to_string(data.iSourceLine) + "]";
                if constexpr (std::is_same_v<T, Command>) {
                    const std::vector<std::string> strInput{ command.strPlugin, command.strCommand, command.strParams };
                    LOG_PRINT(LOG_EMPTY, LOG_STRING(strLine); LOG_STRING("CMD:"); LOG_STRING(ustring::joinStrings(strInput, "|")));
                }
                else if constexpr (std::is_same_v<T, MacroCommand>) {
                    const std::vector<std::string> strInput {command.strPlugin, command.strCommand, command.strParams, command.strVarMacroName, command.strVarMacroValue};
                    LOG_PRINT(LOG_EMPTY, LOG_STRING(strLine); LOG_STRING("VMC:"); LOG_STRING(ustring::joinStrings(strInput, "|")));
                }
                else if constexpr (std::is_same_v<T, RepeatTimes>) {
                    const std::string strCapture = command.strVarMacroName.empty() ? "" : (" -> $" + command.strVarMacroName);
                    LOG_PRINT(LOG_EMPTY, LOG_STRING(strLine); LOG_STRING("REPEAT_N:"); LOG_STRING(command.strLabel); LOG_STRING("x"); LOG_STRING(std::to_string(command.iCount)); LOG_STRING(strCapture));
                }
                else if constexpr (std::is_same_v<T, RepeatUntil>) {
                    const std::string strCapture = command.strVarMacroName.empty() ? "" : (" -> $" + command.strVarMacroName);
                    LOG_PRINT(LOG_EMPTY, LOG_STRING(strLine); LOG_STRING("REPEAT_U:"); LOG_STRING(command.strLabel); LOG_STRING("until ["); LOG_STRING(command.strCondition); LOG_STRING("]"); LOG_STRING(strCapture));
                }
                else if constexpr (std::is_same_v<T, RepeatEnd>) {
                    LOG_PRINT(LOG_EMPTY, LOG_STRING(strLine); LOG_STRING("END_REPEAT:"); LOG_STRING(command.strLabel));
                }
                else if constexpr (std::is_same_v<T, LoopBreak>) {
                    LOG_PRINT(LOG_EMPTY, LOG_STRING(strLine); LOG_STRING("    BREAK:"); LOG_STRING(command.strLabel));
                }
                else if constexpr (std::is_same_v<T, LoopContinue>) {
                    LOG_PRINT(LOG_EMPTY, LOG_STRING(strLine); LOG_STRING(" CONTINUE:"); LOG_STRING(command.strLabel));
                }
            }, data.command);
        }
    );

    return true;

} // listCommands()


/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::loadPlugin(const std::string& strPluginName, bool bInitEnable)
{
    bool bRetVal = false;
    std::string strPluginNameUppecase = ustring::touppercase(strPluginName);

    if (!m_pluginIsLoaded(strPluginNameUppecase)) {
        PluginDataType command {
            strPluginNameUppecase,          // strPluginName
            "",                             // strPluginVersRule
            "",                             // strPluginVersRequested
            nullptr,                        // shptrPluginEntryPoint
            nullptr,                        // hLibHandle
            {},                             // sGetParams (empty PluginDataGet)
            {}                              // sSetParams (empty PluginDataSet)
        };

        if (true == (bRetVal = m_loadPlugin(command, bInitEnable))) {
            m_sScriptEntries->vPlugins.emplace_back(command);
        }
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
    ScriptCommandValidator validator;

    if (true == validator.validateCommand(strCommandTemp, token)) {
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
                    // Shell commands have no source line (iSourceLine = 0 signals "dynamic / shell origin")
                    ScriptLine data { 0,
                        MacroCommand{vstrTokens[1], vstrTokens[2], (vstrTokens.size() == 4) ? vstrTokens[3] : "", vstrTokens[0], ""}
                    };
                    size_t szDummyIndex = 0;
                    m_executeCommand(data, true, szDummyIndex);
                    // the macro is stored in the dedicated map (override the previous values if the macro is reused)
                    if (!vstrTokens[0].empty()) {
                        auto aVar  = std::get<struct MacroCommand>(data.command);
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
                    ScriptLine data { 0,
                        Command{vstrTokens[0], vstrTokens[1], (vstrTokens.size() == 3) ? vstrTokens[2] : ""}
                    };
                    size_t szDummyIndex = 0;
                    m_executeCommand(data, true, szDummyIndex);
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


/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_loadPlugin(PluginDataType& command, bool bInitEnable) noexcept
{
    bool bRetVal = false;

    do {
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("Loading :"); LOG_STRING(command.strPluginName));
        auto handle = m_PluginLoader(command.strPluginName);
        if (!(handle.first && handle.second))
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(command.strPluginName); LOG_STRING("-> loading failed"));
            break; // Exit early on failure
        }

        // Transfer the pointers to the internal storage
        command.hLibHandle = std::move(handle.first);
        command.shptrPluginEntryPoint = std::move(handle.second);

        // Retrieve data from plugin
        command.shptrPluginEntryPoint->getParams(&command.sGetParams);

        if (m_IniCfgLoader.isLoaded()) {
            if (m_IniCfgLoader.sectionExists(command.strPluginName)) {
                if (false == m_IniCfgLoader.resolveSection(command.strPluginName, command.sSetParams.mapSettings)) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR;
                              LOG_STRING(command.strPluginName);
                              LOG_STRING(": failed to load settings from .ini file"));
                    break;
                }
            } else {
                LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                          LOG_STRING(command.strPluginName);
                          LOG_STRING(": no settings in .ini file"));
            }
        }

        command.sSetParams.shpLogger = getLogger();

        // set parameters to plugin
        if (false == command.shptrPluginEntryPoint->setParams(&command.sSetParams)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(command.strPluginName); LOG_STRING(": failed to set params loaded from .ini file"));
            break; // Exit early on failure
        }

        // Lambda to print plugin info
        auto printPluginInfo =  [](const std::string& name, const std::string& version, const std::vector<std::string>& vs) {
            std::ostringstream oss;
            oss << name << "| v" << version << " | ";
            for (const auto& cmd : vs) {
                oss << cmd << " ";
            }
            LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING(oss.str()); LOG_STRING("| loaded"));
        };
        printPluginInfo(command.strPluginName, command.sGetParams.strPluginVersion, command.sGetParams.vstrPluginCommands);

        // if explicitly requested, perform also the plugin initialization and enabling 
        if (bInitEnable) {
            if (false == command.shptrPluginEntryPoint->doInit((true == command.shptrPluginEntryPoint->isPrivileged()) ? this : nullptr)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to initialize plugin:"); LOG_STRING(command.strPluginName));
                bRetVal = false;
            }
            command.shptrPluginEntryPoint->doEnable();
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(command.strPluginName); LOG_STRING("initialized and enabled"));
        }

        bRetVal = true;

    } while(false);

    return bRetVal;

} // m_loadPlugin()



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_pluginIsLoaded(const std::string& strPluginName) noexcept
{
    auto it = std::find_if(m_sScriptEntries->vPlugins.begin(), m_sScriptEntries->vPlugins.end(),
        [&strPluginName](const PluginDataType& p) { return p.strPluginName == strPluginName; });

    bool bFound = (it != m_sScriptEntries->vPlugins.end());
    if (bFound) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING(it->strPluginName); LOG_STRING("already loaded"));
    }

    return bFound;

} // m_pluginIsLoaded()



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_loadPlugins() noexcept
{
    bool bRetVal = true;

    for (auto& command : m_sScriptEntries->vPlugins) {
        if (false == m_loadPlugin(command, false)) {
            bRetVal = false;
            break;
        }
    }

    LOG_PRINT((bRetVal ? LOG_INFO : LOG_ERROR), LOG_HDR; LOG_STRING("Plugin loading"); LOG_STRING(bRetVal ? "passed" : "failed"));

    return bRetVal;

} // m_loadPlugins()



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_crossCheckCommands () noexcept
{
    bool bRetVal = true;

    for (const auto& data : m_sScriptEntries->vCommands) {
        std::visit([this, &bRetVal, &data](const auto & command) {
            using T = std::decay_t<decltype(command)>;
            if constexpr (std::is_same_v<T, MacroCommand> || std::is_same_v<T, Command>) {
                for (auto& plugin : m_sScriptEntries->vPlugins) {
                    if (command.strPlugin == plugin.strPluginName) {
                        auto& commands = plugin.sGetParams.vstrPluginCommands;
                        if (std::find(commands.begin(), commands.end(), command.strCommand) == commands.end()) {
                            LOG_PRINT(LOG_ERROR, LOG_HDR;
                                      LOG_STRING("[L"); LOG_STRING(std::to_string(data.iSourceLine)); LOG_STRING("]");
                                      LOG_STRING("Command") LOG_STRING(command.strCommand);
                                      LOG_STRING("unsupported by plugin"); LOG_STRING(plugin.strPluginName));
                            bRetVal = false;
                            break;
                        }
                    }
                }
            }
        }, data.command);
    }

    LOG_PRINT((bRetVal ? LOG_INFO : LOG_ERROR), LOG_HDR; LOG_STRING("Commands availability"); LOG_STRING(bRetVal ? "passed" : "failed"));

    return bRetVal;

} // m_crossCheckCommands()



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_initPlugins () noexcept
{
    bool bRetVal = true;

    for (const auto& plugin : m_sScriptEntries->vPlugins) {
        if (false == plugin.shptrPluginEntryPoint->doInit((true == plugin.shptrPluginEntryPoint->isPrivileged()) ? this : nullptr)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to initialize plugin:"); LOG_STRING(plugin.strPluginName));
            bRetVal = false;
            break;
        }
    }

    LOG_PRINT((bRetVal ? LOG_INFO : LOG_ERROR), LOG_HDR; LOG_STRING("Plugins initialization"); LOG_STRING(bRetVal ? "passed" : "failed"));

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

    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Plugins enabling passed"));

} // m_enablePlugins()



/*-------------------------------------------------------------------------------
 * Traverse the command list in reverse to resolve macros using their most recently assigned values.
-------------------------------------------------------------------------------*/

void ScriptInterpreter::m_replaceVariableMacros(std::string& input)
{
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
                if (std::holds_alternative<MacroCommand>(it->command)) {
                    const auto& macroCommand = std::get<MacroCommand>(it->command);
                    if (macroCommand.strVarMacroName == macroName) {
                        result.append(macroCommand.strVarMacroValue);
                        found = true;
                        replaced = true;
                        break;
                    }
                }
            }
            // Fall back to loop-scoped macros: scan from innermost (back) to outermost
            // (front) so that an inner loop's macro shadows an outer one with the same name.
            if (!found) {
                for (auto scopeIt = m_loopStateStack.rbegin();
                     scopeIt != m_loopStateStack.rend(); ++scopeIt)
                {
                    auto loopIt = scopeIt->mapLoopMacros.find(macroName);
                    if (loopIt != scopeIt->mapLoopMacros.end()) {
                        result.append(loopIt->second);
                        found    = true;
                        replaced = true;
                        break;
                    }
                }
            }
            // Finally fall back to shell/executeCmd macros (script-wide lifetime).
            if (!found) {
                auto shellIt = m_ShellVarMacros.find(macroName);
                if (shellIt != m_ShellVarMacros.end()) {
                    result.append(shellIt->second);
                    found    = true;
                    replaced = true;
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
  m_runEndRepeat — shared END_REPEAT logic.
  Called from the normal END_REPEAT path and from the CONTINUE_LOOP path.
  Assumes m_loopStateStack.back() is the loop being ended.
  May modify iIndex (loop-back) or pop the stack (loop done).
-------------------------------------------------------------------------------*/

void ScriptInterpreter::m_runEndRepeat(size_t& iIndex, bool& bRetVal) noexcept
{
    LoopState& state = m_loopStateStack.back();

    // Save values used in post-pop log lines before any pop_back().
    const std::string strLabel = state.strLabel;

    if (!state.bIsUntil) {
        // ---- REPEAT N ----
        --state.iRemaining;
        LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                  LOG_STRING("REPEAT"); LOG_STRING(strLabel);
                  LOG_STRING("remaining:"); LOG_STRING(std::to_string(state.iRemaining)));

        if (state.iRemaining > 0) {
            ++state.uIterationCount;
            if (!state.strVarMacroName.empty()) {
                state.mapLoopMacros[state.strVarMacroName] = std::to_string(state.uIterationCount);
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("REPEAT iter-index $"); LOG_STRING(state.strVarMacroName); LOG_STRING("="); LOG_STRING(std::to_string(state.uIterationCount)));
            }
            iIndex = state.szBeginIndex; // caller does ++iIndex → szBeginIndex+1
        } else {
            m_loopStateStack.pop_back(); // destroys mapLoopMacros — state ref is now dangling
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("REPEAT done:"); LOG_STRING(strLabel));
        }
    } else {
        // ---- REPEAT UNTIL ----
        // Copy the condition template before any macro expansion (do not mutate it).
        std::string strCondExpanded = state.strCondition;
        m_replaceVariableMacros(strCondExpanded);

        bool bCondResult = false;
        if (true == m_beEvaluator.evaluate(strCondExpanded, bCondResult)) {
            if (!bCondResult) {
                ++state.uIterationCount;
                if (!state.strVarMacroName.empty()) {
                    state.mapLoopMacros[state.strVarMacroName] = std::to_string(state.uIterationCount);
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("REPEAT UNTIL iter-index $"); LOG_STRING(state.strVarMacroName); LOG_STRING("="); LOG_STRING(std::to_string(state.uIterationCount)));
                }
                iIndex = state.szBeginIndex;
                LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                          LOG_STRING("REPEAT UNTIL looping:"); LOG_STRING(strLabel));
            } else {
                m_loopStateStack.pop_back(); // destroys mapLoopMacros — state ref is now dangling
                LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                          LOG_STRING("REPEAT UNTIL done:"); LOG_STRING(strLabel));
            }
        } else {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("REPEAT UNTIL: failed to evaluate condition:"); LOG_STRING(strCondExpanded));
            bRetVal = false;
        }
    }

} // m_runEndRepeat()


/*-------------------------------------------------------------------------------
  Execute a single IR command.

  iIndex is the current position in vCommands (owned by the caller's loop).
  Loop end nodes (RepeatEnd) may set iIndex to (desired_next - 1) so that the
  caller's unconditional ++iIndex lands at the correct body-start address.
-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_executeCommand (ScriptLine& data, bool bRealExec, size_t& iIndex) noexcept
{
    bool bRetVal = true;

    std::visit([this, bRealExec, &bRetVal, &iIndex](auto& command) {
        using T = std::decay_t<decltype(command)>;

        // -----------------------------------------------------------------
        // Plugin commands (Command / MacroCommand)
        // -----------------------------------------------------------------
        if constexpr (std::is_same_v<T, MacroCommand> || std::is_same_v<T, Command>) {
            if (m_eSkipReason == SkipReason::NONE) {
                for (auto& plugin : m_sScriptEntries->vPlugins) {
                    if (command.strPlugin == plugin.strPluginName) {
                        if(bRealExec) { // real execution
                            m_replaceVariableMacros(command.strParams);
                            LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("Executing"); LOG_STRING(command.strPlugin + "." + command.strCommand + " " + command.strParams));
                            // block to ensure correct command execution time measurement (separate from delay)
                            { 
                                utime::Timer timer("COMMAND");
                                if (false == plugin.shptrPluginEntryPoint->doDispatch(command.strCommand, command.strParams)) {
                                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed executing"); LOG_STRING(command.strPlugin); LOG_STRING(command.strCommand); LOG_STRING("args["); LOG_STRING(command.strParams); LOG_STRING("]"));
                                    bRetVal = false;
                                    break;
                                } else { // execution succceded, update the value of the associated macro if any
                                    if constexpr (std::is_same_v<T, MacroCommand>) {
                                        command.strVarMacroValue = plugin.shptrPluginEntryPoint->getData();
                                        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("VAR["); LOG_STRING(command.strVarMacroName); LOG_STRING("] -> [") LOG_STRING(command.strVarMacroValue); LOG_STRING("]"));
                                        plugin.shptrPluginEntryPoint->resetData();
                                    }
                                }
                            }
                            utime::delay_ms(m_szDelay); /* delay between the commands execution */
                        } else { // only for validation purposes; execute the plugin command section only until [if(false == m_bIsEnabled)] statement
                            if (false == plugin.shptrPluginEntryPoint->doDispatch(command.strCommand, command.strParams)) {
                                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed validating"); LOG_STRING(command.strPlugin); LOG_STRING(command.strCommand); LOG_STRING("args["); LOG_STRING(command.strParams); LOG_STRING("]"));
                                bRetVal = false;
                                break;
                            }
                        }
                    }
                }
            } else {
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Skipped:"); LOG_STRING(command.strPlugin); LOG_STRING(command.strCommand); LOG_STRING("args["); LOG_STRING(command.strParams); LOG_STRING("]"));
            }

        // -----------------------------------------------------------------
        // IF/GOTO condition
        // -----------------------------------------------------------------
        } else if constexpr (std::is_same_v<T, Condition>) {
            if(bRealExec) {
                if(m_eSkipReason == SkipReason::NONE) {
                    // Expand variable macros on a copy — constant macros were already
                    // substituted at validation time, but $vmacros are only known at
                    // runtime and must be resolved here before the evaluator sees them.
                    std::string strCondExpanded = command.strCondition;
                    m_replaceVariableMacros(strCondExpanded);

                    bool beResult = false;

                    if (true == m_beEvaluator.evaluate(strCondExpanded, beResult)) {
                        if (true == beResult) {
                            m_strSkipUntilLabel = command.strLabelName;
                            m_eSkipReason       = SkipReason::GOTO;
                            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Start skipping to label:"); LOG_STRING(m_strSkipUntilLabel));
                        }
                    } else {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to evaluate condition:"); LOG_STRING(strCondExpanded));
                        bRetVal = false;
                    }
                } else {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Skipped:"); LOG_STRING("[IF ..] GOTO:"); LOG_STRING(command.strLabelName));
                }
            }

        // -----------------------------------------------------------------
        // GOTO/IF target label
        // -----------------------------------------------------------------
        } else if constexpr (std::is_same_v<T, Label>) {
            if(bRealExec) {
                if (m_strSkipUntilLabel == command.strLabelName &&
                    m_eSkipReason       == SkipReason::GOTO) {
                    m_strSkipUntilLabel.clear();
                    m_eSkipReason = SkipReason::NONE;
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Stop skipping at label:"); LOG_STRING(command.strLabelName));
                }
            }

        // -----------------------------------------------------------------
        // REPEAT_TIMES — push loop state on first entry
        // (on loop-back iterations the caller jumps to iIndex+1, i.e. the
        // first body command, so this node is only executed once per loop)
        // -----------------------------------------------------------------
        } else if constexpr (std::is_same_v<T, RepeatTimes>) {
            if (bRealExec && m_eSkipReason == SkipReason::NONE) {
                LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                          LOG_STRING("REPEAT start:"); LOG_STRING(command.strLabel);
                          LOG_STRING("count:"); LOG_STRING(std::to_string(command.iCount)));
                m_loopStateStack.push_back({command.strLabel, iIndex, command.iCount, false, "",
                                            command.strVarMacroName, 0U, {}});
                // Write the initial iteration index "0" into the loop's own scope.
                if (!command.strVarMacroName.empty()) {
                    m_loopStateStack.back().mapLoopMacros[command.strVarMacroName] = "0";
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("REPEAT iter-index $"); LOG_STRING(command.strVarMacroName); LOG_STRING("= 0"));
                }
            }

        // -----------------------------------------------------------------
        // REPEAT_UNTIL — push loop state on first entry
        // -----------------------------------------------------------------
        } else if constexpr (std::is_same_v<T, RepeatUntil>) {
            if (bRealExec && m_eSkipReason == SkipReason::NONE) {
                LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                          LOG_STRING("REPEAT UNTIL start:"); LOG_STRING(command.strLabel);
                          LOG_STRING("cond:"); LOG_STRING(command.strCondition));
                m_loopStateStack.push_back({command.strLabel, iIndex, -1, true, command.strCondition,
                                            command.strVarMacroName, 0U, {}});
                // Write the initial iteration index "0" into the loop's own scope.
                if (!command.strVarMacroName.empty()) {
                    m_loopStateStack.back().mapLoopMacros[command.strVarMacroName] = "0";
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("REPEAT UNTIL iter-index $"); LOG_STRING(command.strVarMacroName); LOG_STRING("= 0"));
                }
            }

        // -----------------------------------------------------------------
        // END_REPEAT
        //
        // Four cases depending on m_eSkipReason:
        //
        //   NONE         — normal execution: call m_runEndRepeat.
        //
        //   GOTO         — a GOTO skip is in flight toward a LABEL node;
        //                  this END_REPEAT is transparent (no state change).
        //
        //   BREAK_LOOP   — unwinding toward the named target.
        //                  Always pop the innermost LoopState.
        //                  If this IS the target: clear skip, resume after node.
        //                  If this is NOT the target: keep skipping outward.
        //
        //   CONTINUE_LOOP— same incremental unwind, but when the target is
        //                  reached: do NOT pop — call m_runEndRepeat instead
        //                  so the loop decides whether to loop-back or exit.
        //
        // During dry-run (bRealExec == false) the node is always a no-op.
        // -----------------------------------------------------------------
        } else if constexpr (std::is_same_v<T, RepeatEnd>) {
            if (bRealExec) {

                if (m_eSkipReason == SkipReason::NONE) {
                    // ---- Normal execution path ----
                    if (m_loopStateStack.empty() || m_loopStateStack.back().strLabel != command.strLabel) {
                        LOG_PRINT(LOG_ERROR, LOG_HDR;
                                  LOG_STRING("END_REPEAT: unexpected label or empty stack:"); LOG_STRING(command.strLabel));
                        bRetVal = false;
                        return;
                    }
                    m_runEndRepeat(iIndex, bRetVal);

                } else if (m_eSkipReason == SkipReason::BREAK_LOOP) {
                    // ---- BREAK unwind ----
                    // Pop unconditionally — this loop is being exited regardless.
                    if (!m_loopStateStack.empty()) {
                        LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                                  LOG_STRING("BREAK: unwinding loop:"); LOG_STRING(command.strLabel));
                        m_loopStateStack.pop_back();
                    }
                    if (command.strLabel == m_strSkipUntilLabel) {
                        // Target reached — resume after this END_REPEAT with no loop-back.
                        m_strSkipUntilLabel.clear();
                        m_eSkipReason = SkipReason::NONE;
                        LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                                  LOG_STRING("BREAK: exited loop:"); LOG_STRING(command.strLabel));
                    }

                } else if (m_eSkipReason == SkipReason::CONTINUE_LOOP) {
                    // ---- CONTINUE unwind ----
                    if (command.strLabel != m_strSkipUntilLabel) {
                        // Not the target yet — pop this inner loop and continue skipping.
                        if (!m_loopStateStack.empty()) {
                            LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                                      LOG_STRING("CONTINUE: unwinding inner loop:"); LOG_STRING(command.strLabel));
                            m_loopStateStack.pop_back();
                        }
                    } else {
                        // Target reached — clear skip, keep LoopState alive, run loop logic.
                        m_strSkipUntilLabel.clear();
                        m_eSkipReason = SkipReason::NONE;
                        LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                                  LOG_STRING("CONTINUE: resuming at END_REPEAT:"); LOG_STRING(command.strLabel));
                        m_runEndRepeat(iIndex, bRetVal);
                    }
                }
                // SkipReason::GOTO — transparent, do nothing.
            }

        // -----------------------------------------------------------------
        // BREAK <loop-label>
        // Skip forward to END_REPEAT of the named loop; all intermediate
        // loops are unwound by the END_REPEAT handler above.
        // -----------------------------------------------------------------
        } else if constexpr (std::is_same_v<T, LoopBreak>) {
            if (bRealExec && m_eSkipReason == SkipReason::NONE) {
                LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                          LOG_STRING("BREAK:"); LOG_STRING(command.strLabel));
                m_strSkipUntilLabel = command.strLabel;
                m_eSkipReason       = SkipReason::BREAK_LOOP;
            }

        // -----------------------------------------------------------------
        // CONTINUE <loop-label>
        // Skip forward to END_REPEAT of the named loop, which then runs its
        // normal loop-back or exit logic.
        // -----------------------------------------------------------------
        } else if constexpr (std::is_same_v<T, LoopContinue>) {
            if (bRealExec && m_eSkipReason == SkipReason::NONE) {
                LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                          LOG_STRING("CONTINUE:"); LOG_STRING(command.strLabel));
                m_strSkipUntilLabel = command.strLabel;
                m_eSkipReason       = SkipReason::CONTINUE_LOOP;
            }
        }
    }, data.command);

    if (bRealExec && m_eSkipReason == SkipReason::NONE) {
        LOG_PRINT((bRetVal ? LOG_INFO : LOG_ERROR), LOG_HDR; LOG_STRING("Command"); LOG_STRING(bRetVal ? "succeeded" : "failed"));
    }

    return bRetVal;

} // m_executeCommand()


/*-------------------------------------------------------------------------------
  Index-based execution loop.
  Using an explicit index (instead of a range-for) allows RepeatEnd to set
  iIndex to (body_start - 1) so that the unconditional ++iIndex produces the
  correct next-iteration address.
-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_executeCommands (bool bRealExec) noexcept
{
    bool bRetVal = true;

    // Reset transient execution state before each pass.
    m_strSkipUntilLabel.clear();
    m_eSkipReason = SkipReason::NONE;
    m_loopStateStack.clear();

    auto& vCommands = m_sScriptEntries->vCommands;
    size_t i = 0;

    while (i < vCommands.size()) {
        if (false == m_executeCommand(vCommands[i], bRealExec, i)) {
            bRetVal = false;
            break;
        }
        ++i;
    }

    return bRetVal;

} /* m_executeCommands() */
