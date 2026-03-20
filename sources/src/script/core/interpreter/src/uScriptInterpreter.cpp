#include "uScriptInterpreter.hpp"
#include "uScriptCommandValidator.hpp"    
#include "uScriptDataTypes.hpp"        
#include "uString.hpp"
#include "uTimer.hpp"
#include "uLogger.hpp"
#include "uCalculator.hpp"
#include "uCheckContinue.hpp"

#include <regex>
#include <sstream>
#include <iomanip>
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


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL CONSTANTS                                  //
/////////////////////////////////////////////////////////////////////////////////

static constexpr std::string_view kEvalPrefix  = "EVAL ";
static constexpr std::string_view kMathPrefix  = "MATH ";
static constexpr std::string_view kFmtPrefix   = "FORMAT ";
static constexpr std::string_view kPrintPrefix = "PRINT ";

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

        // only validate commands (dry run)
        if (false == m_executeCommands(false)) {
            break;
        }

        // if plugins argument validation passed then we enable the pluggins for the real execution
        m_enablePlugins();

        // execute commands
        if (false == m_executeCommands(true)) {
            break;
        }

        bRetVal = true;

    } while(false);

    LOG_PRINT((bRetVal ? LOG_DEBUG : LOG_ERROR), LOG_HDR; LOG_STRING("Script execution"); LOG_STRING(bRetVal ? "ok" : "failed"));

    return bRetVal;

} // interpretScript()


/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::listMacrosPlugins()
{
    // Prints a header followed by every key:value pair in any string→string map.
    // Extracted to eliminate four structurally identical for_each blocks below.
    auto printKVMap = [](const auto& map, const std::string& header) {
        if (!map.empty()) {
            LOG_PRINT(LOG_EMPTY, LOG_STRING(header));
            for (const auto& entry : map) {
                LOG_PRINT(LOG_EMPTY, LOG_STRING(entry.first); LOG_STRING(":"); LOG_STRING(entry.second));
            }
        }
    };

    printKVMap(m_sScriptEntries->mapMacros, "\x1b[1;33mcmacros\x1b[0m");

    if (!m_sScriptEntries->mapArrayMacros.empty()) {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("\x1b[1;33marrays\x1b[0m"));
        std::for_each(m_sScriptEntries->mapArrayMacros.begin(), m_sScriptEntries->mapArrayMacros.end(),
            [](const auto& arr) {
                std::ostringstream oss;
                oss << arr.first << "[" << arr.second.size() << "]: ";
                for (size_t k = 0; k < arr.second.size(); ++k) {
                    if (k > 0) oss << ", ";
                    oss << "[" << k << "]=" << arr.second[k];
                }
                LOG_PRINT(LOG_EMPTY, LOG_STRING(oss.str()));
            });
    }

    // Show runtime variable macro values — these are the values most recently
    // written by executed MacroCommands, which is what the script actually sees.
    printKVMap(m_RuntimeVarMacros, "\x1b[1;33mvmacros\x1b[0m");

    if (!m_sScriptEntries->vPlugins.empty()) {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("\x1b[1;33mplugins\x1b[0m"));
        std::for_each(m_sScriptEntries->vPlugins.begin(), m_sScriptEntries->vPlugins.end(),
            [&](auto& plugin) {
                LOG_PRINT(LOG_EMPTY, LOG_STRING([&]{ std::ostringstream o; o << std::left << std::setw(12) << plugin.strPluginName; return o.str(); }()); 
                    LOG_STRING(plugin.sGetParams.strPluginVersion); 
                    LOG_STRING(ustring::joinStrings(plugin.sGetParams.vstrPluginCommands, ' ')));
            });
    }

    return true;

} // listMacrosPlugins()


/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::listCommands()
{
    LOG_PRINT(LOG_EMPTY, LOG_STRING("___commands___"));
    std::for_each(m_sScriptEntries->vCommands.begin(), m_sScriptEntries->vCommands.end(),
        [&](const ScriptLine& data) {
            std::visit([&data](const auto& command) {
                using T = std::decay_t<decltype(command)>;
                if constexpr (std::is_same_v<T, Command> || std::is_same_v<T, MacroCommand>) {
                    LOG_PRINT(LOG_EMPTY, LOG_STRING(command.strPlugin + "." + command.strCommand + " " + command.strParams));
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
  m_mirrorToShellVarMacros — copy a named runtime variable into the shell-scope
  map so that its value persists across executeCmd() calls.
  Called after every assignment-type token (VAR_MACRO_INIT, MATH_STMT,
  FORMAT_STMT, VARIABLE_MACRO) that runs through m_executeCommand.
-------------------------------------------------------------------------------*/

void ScriptInterpreter::m_mirrorToShellVarMacros(const std::string& strName)
{
    auto it = m_RuntimeVarMacros.find(strName);
    if (it != m_RuntimeVarMacros.end()) {
        m_ShellVarMacros[strName] = it->second;
    }
} // m_mirrorToShellVarMacros()


/*-------------------------------------------------------------------------------
  m_dispatchShellLine — wrap a pre-built command variant in a shell-origin
  ScriptLine (iLineNumber = 0) and execute it immediately (bRealExec = true).
  Centralises the boilerplate that every executeCmd() token case repeats.
-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_dispatchShellLine(decltype(ScriptLine::command) variant)
{
    ScriptLine data { 0, std::move(variant) };
    size_t szDummyIndex = 0;
    return m_executeCommand(data, true, szDummyIndex);
} // m_dispatchShellLine()


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

    if (true == validator.validateCommand(0, strCommandTemp, token)) {
        switch(token) {

            case Token::CONSTANT_MACRO : {
                std::vector<std::string> vstrTokens;
                ustring::tokenize(strCommandTemp, SCRIPT_CONSTANT_MACRO_SEPARATOR, vstrTokens);

                if (vstrTokens.size() == 2) {
                    // cmacroname := cmacroval                         | cmacroname |  cmacroval   |
                    auto aRetVal = m_sScriptEntries->mapMacros.emplace(vstrTokens[0], vstrTokens[1]);
                    if (false == aRetVal.second) {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("cmacro already exists:"); LOG_STRING(vstrTokens[0]));
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
                const size_t szSize = vstrTokens.size();

                if ((szSize == 3) || (szSize == 4)) {
                    bRetVal = m_dispatchShellLine(
                        MacroCommand{vstrTokens[1], vstrTokens[2], (szSize == 4) ? vstrTokens[3] : "", vstrTokens[0]}
                    );
                    // m_executeCommand already wrote the result into m_RuntimeVarMacros;
                    // mirror it to m_ShellVarMacros so it persists across executeCmd calls.
                    if (!vstrTokens[0].empty()) {
                        m_mirrorToShellVarMacros(vstrTokens[0]);
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
                    bRetVal = m_dispatchShellLine(
                        Command{vstrTokens[0], vstrTokens[1], (vstrTokens.size() == 3) ? vstrTokens[2] : ""}
                    );
                } else {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid command"));
                    bRetVal = false;
                }
                break;
            }

            case Token::PRINT_STMT : {
                std::string strText = strCommandTemp;
                ustring::stripPrefix(strText, kPrintPrefix);
                bRetVal = m_dispatchShellLine(PrintStatement{ strText });
                break;
            }

            case Token::MATH_STMT : {
                // Format: <n> ?= MATH <expression>  ->  tokens: [ name, "MATH <expr>" ]
                {
                    std::vector<std::string> vstrDelimiters{ SCRIPT_VARIABLE_MACRO_SEPARATOR };
                    std::vector<std::string> vstrTokens;
                    ustring::tokenizeEx(strCommandTemp, vstrDelimiters, vstrTokens);
                    if (vstrTokens.size() == 2) {
                        std::string strExpr = vstrTokens[1];
                        ustring::stripPrefix(strExpr, kMathPrefix);
                        bRetVal = m_dispatchShellLine(MathStatement{ vstrTokens[0], strExpr });
                        m_mirrorToShellVarMacros(vstrTokens[0]);
                    } else {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid MATH_STMT"));
                        bRetVal = false;
                    }
                }
                break;
            }

            case Token::VAR_MACRO_INIT : {
                // Format: <n> ?= <value template>  ->  tokens: [ name, valueTpl ]
                {
                    std::vector<std::string> vstrDelimiters{ SCRIPT_VARIABLE_MACRO_SEPARATOR };
                    std::vector<std::string> vstrTokens;
                    ustring::tokenizeEx(strCommandTemp, vstrDelimiters, vstrTokens);
                    if (vstrTokens.size() == 2) {
                        bRetVal = m_dispatchShellLine(VarMacroInit{ vstrTokens[0], vstrTokens[1] });
                        m_mirrorToShellVarMacros(vstrTokens[0]);
                    } else {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid VAR_MACRO_INIT"));
                        bRetVal = false;
                    }
                }
                break;
            }

            case Token::FORMAT_STMT : {
                // Format: <n> ?= FORMAT <input> | <pattern>  ->  tokens: [ name, "FORMAT <input>", pattern ]
                {
                    std::vector<std::string> vstrDelimiters{ SCRIPT_VARIABLE_MACRO_SEPARATOR, STRING_SEPARATOR_PIPE };
                    std::vector<std::string> vstrTokens;

                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("FORMAT_STMT strCommandTemp=["); LOG_STRING(strCommandTemp); LOG_STRING("]"));

                    ustring::tokenizeEx(strCommandTemp, vstrDelimiters, vstrTokens);
                    if (vstrTokens.size() == 3) {
                        std::string strInput = vstrTokens[1];
                        ustring::stripPrefix(strInput, kFmtPrefix);
                        bRetVal = m_dispatchShellLine(FormatStatement{ vstrTokens[0], strInput, vstrTokens[2] });
                        m_mirrorToShellVarMacros(vstrTokens[0]);
                    } else {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid FORMAT_STMT"));
                        bRetVal = false;
                    }
                }
                break;
            }

            case Token::ARRAY_MACRO : {
                // Format validated by ScriptCommandValidator:
                //   <name> := [ val0, val1, ... ]
                // Tokenize on ':=' to get [ name, "[ val0, val1, ... ]" ]
                // then strip brackets and split on ',' to build the vector.
                {
                    std::vector<std::string> vstrDelimiters{ SCRIPT_CONSTANT_MACRO_SEPARATOR };
                    std::vector<std::string> vstrTokens;
                    ustring::tokenizeEx(strCommandTemp, vstrDelimiters, vstrTokens);
                    if (vstrTokens.size() == 2) {
                        const std::string& strName    = vstrTokens[0];
                        std::string        strContent = vstrTokens[1];

                        // Strip surrounding '[' ... ']' (validator guarantees they exist).
                        const auto szOpen  = strContent.find('[');
                        const auto szClose = strContent.rfind(']');
                        if (szOpen != std::string::npos && szClose != std::string::npos && szClose > szOpen) {
                            strContent = strContent.substr(szOpen + 1, szClose - szOpen - 1);
                        }

                        // Split on ',' to obtain individual element strings.
                        std::vector<std::string> vstrElements;
                        ustring::tokenize(strContent, ",", vstrElements);

                        // Trim whitespace from every element.
                        for (auto& elem : vstrElements) {
                            ustring::trim(elem);
                        }

                        // Register (or overwrite) the array in the shared map.
                        auto aRetVal = m_sScriptEntries->mapArrayMacros.emplace(strName, vstrElements);
                        if (false == aRetVal.second) {
                            // Array already exists — update its value in-place.
                            aRetVal.first->second = vstrElements;
                            LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                                      LOG_STRING("ARRAY_MACRO updated:"); LOG_STRING(strName);
                                      LOG_STRING("size="); LOG_STRING(std::to_string(vstrElements.size())));
                        } else {
                            LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                                      LOG_STRING("ARRAY_MACRO created:"); LOG_STRING(strName);
                                      LOG_STRING("size="); LOG_STRING(std::to_string(vstrElements.size())));
                        }
                    } else {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid ARRAY_MACRO"));
                        bRetVal = false;
                    }
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
  m_evaluateCondition — unified condition evaluator.

  Dispatches to EvalExprEvaluator when the condition string starts with the
  "EVAL " prefix (case-sensitive), and to BoolExprEvaluator for everything
  else (plain TRUE / FALSE / || / && expressions as before).

  This is the single call-site that replaces all direct m_beEvaluator.evaluate()
  calls, so adding new evaluator back-ends in the future requires only changes
  here rather than throughout m_executeCommand / m_runEndRepeat.
-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_evaluateCondition(const std::string& strCondition, bool& result) noexcept
{
    std::string strExpr = strCondition;
    ustring::stripPrefix(strExpr, kEvalPrefix);

    if (strExpr.size() < strCondition.size()) {
        // Prefix was present — delegate to the typed evaluator.
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("EVAL expression:"); LOG_STRING(strExpr));
        return m_evalExprEvaluator.evaluate(strExpr, result);
    }

    // Plain boolean expression (TRUE / FALSE / && / ||).
    return m_beEvaluator.evaluate(strCondition, result);

} // m_evaluateCondition()


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

    LOG_PRINT((bRetVal ? LOG_DEBUG : LOG_ERROR), LOG_HDR; LOG_STRING("Plugin loading"); LOG_STRING(bRetVal ? "ok" : "failed"));

    return bRetVal;

} // m_loadPlugins()



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_crossCheckCommands () noexcept
{
    bool bRetVal = true;
    char strLineNumber[16];

    // Build the per-plugin command-set index once for this check.
    m_buildPluginCommandIndex();


    for (const auto& data : m_sScriptEntries->vCommands) {
        std::visit([this, &strLineNumber, &bRetVal, &data](const auto & command) {
            using T = std::decay_t<decltype(command)>;
            if constexpr (std::is_same_v<T, MacroCommand> || std::is_same_v<T, Command>) {
                auto pluginIt = m_pluginCmdIndex.find(command.strPlugin);
                if (pluginIt != m_pluginCmdIndex.end()) {
                    if (pluginIt->second.count(command.strCommand) == 0) {
                        std::snprintf(strLineNumber, sizeof(strLineNumber), "%03d:", data.iLineNumber);
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strLineNumber);
                                  LOG_STRING("Command") 
                                  LOG_STRING(command.strCommand);
                                  LOG_STRING("unsupported by plugin"); 
                                  LOG_STRING(command.strPlugin));
                        bRetVal = false;
                    }
                }
            }
        }, data.command);
    }

    LOG_PRINT((bRetVal ? LOG_DEBUG : LOG_ERROR), LOG_HDR; 
            LOG_STRING("Commands availability"); 
            LOG_STRING(bRetVal ? "ok" : "failed"));

    return bRetVal;

} // m_crossCheckCommands()



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptInterpreter::m_initPlugins () noexcept
{
    bool bRetVal = true;

    for (const auto& plugin : m_sScriptEntries->vPlugins) {
        if (false == plugin.shptrPluginEntryPoint->doInit((true == plugin.shptrPluginEntryPoint->isPrivileged()) ? this : nullptr)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; 
                      LOG_STRING("Failed to initialize plugin:"); 
                      LOG_STRING(plugin.strPluginName));
            bRetVal = false;
            break;
        }
    }

    LOG_PRINT((bRetVal ? LOG_DEBUG : LOG_ERROR), LOG_HDR; 
                LOG_STRING("Plugins initialization"); 
                LOG_STRING(bRetVal ? "ok" : "failed"));

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

    LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("Plugins enabling ok"));

} // m_enablePlugins()



/*-------------------------------------------------------------------------------
 * Traverse the command list in reverse to resolve macros using their most recently assigned values.
-------------------------------------------------------------------------------*/

void ScriptInterpreter::m_replaceVariableMacros(std::string& input)
{
    // Extended pattern — two forms:
    //   $NAME.$indexmacro  → array element access  (groups 1=NAME  2=indexmacro)
    //   $NAME              → regular macro lookup   (group  1=NAME, group 2 empty)
    //
    // The \.\$ in the optional suffix means a literal dot followed by a literal
    // dollar sign, ensuring that $NAME.$indexmacro is consumed as a single match
    // rather than two consecutive matches.
    static const std::regex macroPattern(
        R"(\$([A-Za-z_][A-Za-z0-9_]*)(?:\.\$([A-Za-z_][A-Za-z0-9_]*))?)");
    std::smatch match;

    // Helper: resolve a single bare macro name through all scope tiers.
    // Returns the resolved string, or an empty optional if not found.
    auto resolveName = [&](const std::string& name) -> std::pair<bool, std::string> {
        // 1. Loop-scoped macros — innermost first
        for (auto scopeIt = m_loopStateStack.rbegin();
             scopeIt != m_loopStateStack.rend(); ++scopeIt)
        {
            auto loopIt = scopeIt->mapLoopMacros.find(name);
            if (loopIt != scopeIt->mapLoopMacros.end()) {
                return {true, loopIt->second};
            }
        }
        // 2. Script-level variable macros — O(1) runtime map.
        //    Holds the value that was most recently EXECUTED, not the value that
        //    appears last in the IR.  This is correct when the same name is used
        //    on both sides of an assignment (e.g. score ?= CORE.MATH $score + 10).
        {
            auto rtIt = m_RuntimeVarMacros.find(name);
            if (rtIt != m_RuntimeVarMacros.end()) {
                return {true, rtIt->second};
            }
        }
        // 3. Shell macros
        {
            auto shellIt = m_ShellVarMacros.find(name);
            if (shellIt != m_ShellVarMacros.end()) {
                return {true, shellIt->second};
            }
        }
        return {false, {}};
    };

    bool replaced = true;
    while (replaced) {
        replaced = false;
        std::string result;
        result.reserve(input.size());
        std::string::const_iterator searchStart = input.cbegin();

        while (std::regex_search(searchStart, input.cend(), match, macroPattern)) {
            result.append(match.prefix());

            const std::string macroName  = match[1].str();
            const bool        hasIndex   = match[2].matched;
            const std::string indexName  = hasIndex ? match[2].str() : "";

            bool found = false;

            if (hasIndex) {
                // ---- Array element access: $macroName.$indexName ----
                auto arrIt = m_sScriptEntries->mapArrayMacros.find(macroName);
                if (arrIt != m_sScriptEntries->mapArrayMacros.end()) {
                    // Resolve the index macro to a numeric string
                    auto [idxFound, idxVal] = resolveName(indexName);
                    if (idxFound) {
                        try {
                            size_t idx = static_cast<size_t>(std::stoull(idxVal));
                            if (idx < arrIt->second.size()) {
                                result.append(arrIt->second[idx]);
                                found    = true;
                                replaced = true;
                            } else {
                                LOG_PRINT(LOG_ERROR, LOG_HDR;
                                          LOG_STRING("Array ["); LOG_STRING(macroName);
                                          LOG_STRING("] index"); LOG_STRING(idxVal);
                                          LOG_STRING("out of range (size=");
                                          LOG_STRING(std::to_string(arrIt->second.size())); LOG_STRING(")"));
                                // Leave unexpanded — do not crash
                            }
                        } catch (...) {
                            LOG_PRINT(LOG_ERROR, LOG_HDR;
                                      LOG_STRING("Array ["); LOG_STRING(macroName);
                                      LOG_STRING("] non-numeric index:"); LOG_STRING(idxVal));
                            // Leave unexpanded
                        }
                    }
                    // Index macro not yet resolved — leave unexpanded, next pass will retry
                } else {
                    // macroName is NOT an array — resolve it as a regular macro and
                    // re-emit the .$indexName suffix literally so it is not silently dropped.
                    auto [nameFound, nameVal] = resolveName(macroName);
                    if (nameFound) {
                        result.append(nameVal);
                        result.append(".$");
                        result.append(indexName);
                        found    = true;
                        replaced = true;
                    }
                    // else: leave the full $name.$index unexpanded
                }
            } else {
                // ---- Regular macro lookup ----
                auto [nameFound, nameVal] = resolveName(macroName);
                if (nameFound) {
                    result.append(nameVal);
                    found    = true;
                    replaced = true;
                }
            }

            if (!found) {
                result.append(match[0]); // leave unexpanded
            }
            searchStart = match.suffix().first;
        }
        result.append(searchStart, input.cend());
        input = result;
    }
}



/*-------------------------------------------------------------------------------
  m_initLoopIterIndex — write iteration counter "0" into the loop's own macro
  scope on first entry.  Called by both RepeatTimes and RepeatUntil handlers
  immediately after pushing a new LoopState onto the stack.
  No-op when strVarMacroName is empty (loop has no capture variable).
-------------------------------------------------------------------------------*/

void ScriptInterpreter::m_initLoopIterIndex(LoopState& state) noexcept
{
    if (!state.strVarMacroName.empty()) {
        state.mapLoopMacros[state.strVarMacroName] = "0";
        LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                  LOG_STRING("REPEAT iter-index $"); LOG_STRING(state.strVarMacroName);
                  LOG_STRING("= 0"));
    }
} // m_initLoopIterIndex()


/*-------------------------------------------------------------------------------
  m_advanceLoopIterIndex — increment the iteration counter and update the
  loop-scope macro.  Called by m_runEndRepeat on each loop-back.
  No-op when strVarMacroName is empty.
-------------------------------------------------------------------------------*/

void ScriptInterpreter::m_advanceLoopIterIndex(LoopState& state) noexcept
{
    ++state.uIterationCount;
    if (!state.strVarMacroName.empty()) {
        state.mapLoopMacros[state.strVarMacroName] = std::to_string(state.uIterationCount);
        LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                  LOG_STRING("REPEAT iter-index $"); LOG_STRING(state.strVarMacroName);
                  LOG_STRING("="); LOG_STRING(std::to_string(state.uIterationCount)));
    }
} // m_advanceLoopIterIndex()


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
            m_advanceLoopIterIndex(state);
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
        if (true == m_evaluateCondition(strCondExpanded, bCondResult)) {
            if (!bCondResult) {
                m_advanceLoopIterIndex(state);
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
    bool bIsPluginCommand = false;
    char strLineNumber[16];
    std::snprintf(strLineNumber, sizeof(strLineNumber), "%03d:", data.iLineNumber);

    std::visit([this, bRealExec, &strLineNumber, &bIsPluginCommand, &bRetVal, &iIndex](auto& command) {
        using T = std::decay_t<decltype(command)>;

        // -----------------------------------------------------------------
        // Plugin commands (Command / MacroCommand)
        // -----------------------------------------------------------------
        if constexpr (std::is_same_v<T, MacroCommand> || std::is_same_v<T, Command>) {
            if (m_eSkipReason == SkipReason::NONE) {
                bIsPluginCommand = true;
                for (auto& plugin : m_sScriptEntries->vPlugins) {
                    if (command.strPlugin == plugin.strPluginName) {
                        if(bRealExec) { // real execution
                            // Expand macros onto a copy — the IR must not be mutated so
                            // that every loop iteration starts from the original template.
                            std::string strExpandedParams = command.strParams;
                            m_replaceVariableMacros(strExpandedParams);
                            LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING(strLineNumber); 
                                LOG_STRING("Executing"); 
                                LOG_STRING(command.strPlugin + "." + command.strCommand + " " + strExpandedParams));
                            // block to ensure correct command execution time measurement (separate from delay)
                            {
                                utime::Timer timer(std::string(strLineNumber) + " Command");
                                if (false == plugin.shptrPluginEntryPoint->doDispatch(command.strCommand, strExpandedParams)) {
                                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strLineNumber); 
                                        LOG_STRING("Failed executing"); 
                                        LOG_STRING(command.strPlugin + "." + command.strCommand + " " + strExpandedParams)); 
                                        bRetVal = false;
                                    break;
                                } else { // execution succeeded, update the value of the associated macro if any
                                    if constexpr (std::is_same_v<T, MacroCommand>) {
                                        const std::string strValue = plugin.shptrPluginEntryPoint->getData();
                                        m_RuntimeVarMacros[command.strVarMacroName] = strValue;
                                        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLineNumber); 
                                            LOG_STRING("VAR["); LOG_STRING(command.strVarMacroName); 
                                            LOG_STRING("]->[") 
                                            LOG_STRING(strValue); 
                                            LOG_STRING("]"));
                                        plugin.shptrPluginEntryPoint->resetData();
                                    }
                                }
                            }
                            utime::delay_ms(m_szDelay); /* delay between the commands execution */
                        } else { // only for validation purposes
                            if (false == plugin.shptrPluginEntryPoint->doDispatch(command.strCommand, command.strParams)) {
                                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strLineNumber); 
                                    LOG_STRING("Failed validating"); 
                                    LOG_STRING(command.strPlugin + "." + command.strCommand); 
                                    LOG_STRING("args["); 
                                    LOG_STRING(command.strParams); 
                                    LOG_STRING("]"));
                                bRetVal = false;
                                break;
                            }
                        }
                    }
                }
            } else {
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLineNumber); 
                    LOG_STRING("Skipped:"); LOG_STRING(command.strPlugin); 
                    LOG_STRING(command.strCommand); LOG_STRING("args["); 
                    LOG_STRING(command.strParams); LOG_STRING("]"));
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

                    if (true == m_evaluateCondition(strCondExpanded, beResult)) {
                        if (true == beResult) {
                            m_strSkipUntilLabel = command.strLabelName;
                            m_eSkipReason       = SkipReason::GOTO;
                            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLineNumber); 
                                LOG_STRING("Start skipping to label:"); 
                                LOG_STRING(m_strSkipUntilLabel));
                        }
                    } else {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strLineNumber); 
                            LOG_STRING("Failed to evaluate condition:"); 
                            LOG_STRING(strCondExpanded));
                        bRetVal = false;
                    }
                } else {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLineNumber); 
                        LOG_STRING("Skipped:"); 
                        LOG_STRING("[IF ..] GOTO:"); 
                        LOG_STRING(command.strLabelName));
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
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLineNumber); 
                        LOG_STRING("Stop skipping at label:"); 
                        LOG_STRING(command.strLabelName));
                }
            }

        // -----------------------------------------------------------------
        // REPEAT_TIMES — push loop state on first entry
        // (on loop-back iterations the caller jumps to iIndex+1, i.e. the
        // first body command, so this node is only executed once per loop)
        // -----------------------------------------------------------------
        } else if constexpr (std::is_same_v<T, RepeatTimes>) {
            if (bRealExec && m_eSkipReason == SkipReason::NONE) {
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLineNumber); 
                          LOG_STRING("REPEAT start:"); 
                          LOG_STRING(command.strLabel);
                          LOG_STRING("count:"); 
                          LOG_STRING(std::to_string(command.iCount)));
                m_loopStateStack.push_back({command.strLabel, iIndex, command.iCount, false, "",
                                            command.strVarMacroName, 0U, {}});
                // Write the initial iteration index "0" into the loop's own scope.
                m_initLoopIterIndex(m_loopStateStack.back());
            }

        // -----------------------------------------------------------------
        // REPEAT_UNTIL — push loop state on first entry
        // -----------------------------------------------------------------
        } else if constexpr (std::is_same_v<T, RepeatUntil>) {
            if (bRealExec && m_eSkipReason == SkipReason::NONE) {
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLineNumber); 
                          LOG_STRING("REPEAT UNTIL start:"); 
                          LOG_STRING(command.strLabel);
                          LOG_STRING("cond:"); 
                          LOG_STRING(command.strCondition));
                m_loopStateStack.push_back({command.strLabel, iIndex, -1, true, command.strCondition,
                                            command.strVarMacroName, 0U, {}});
                // Write the initial iteration index "0" into the loop's own scope.
                m_initLoopIterIndex(m_loopStateStack.back());
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
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strLineNumber); 
                                  LOG_STRING("END_REPEAT: unexpected label or empty stack:"); 
                                  LOG_STRING(command.strLabel));
                        bRetVal = false;
                        return;
                    }
                    m_runEndRepeat(iIndex, bRetVal);

                } else if (m_eSkipReason == SkipReason::BREAK_LOOP) {
                    // ---- BREAK unwind ----
                    // Only pop if the back of the stack matches this END_REPEAT label.
                    // If it does not match, the REPEAT for this label was itself skipped
                    // (never pushed), so there is nothing to pop.
                    if (!m_loopStateStack.empty() &&
                        m_loopStateStack.back().strLabel == command.strLabel) {
                        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLineNumber); 
                                  LOG_STRING("BREAK: unwinding loop:"); 
                                  LOG_STRING(command.strLabel));
                        m_loopStateStack.pop_back();
                    }
                    if (command.strLabel == m_strSkipUntilLabel) {
                        // Target reached — resume after this END_REPEAT with no loop-back.
                        m_strSkipUntilLabel.clear();
                        m_eSkipReason = SkipReason::NONE;
                        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLineNumber); 
                                  LOG_STRING("BREAK: exited loop:"); 
                                  LOG_STRING(command.strLabel));
                    }

                } else if (m_eSkipReason == SkipReason::CONTINUE_LOOP) {
                    // ---- CONTINUE unwind ----
                    if (command.strLabel != m_strSkipUntilLabel) {
                        // Not the target yet. Only pop if this loop was actually pushed —
                        // i.e. its label matches the current stack back.
                        if (!m_loopStateStack.empty() &&
                            m_loopStateStack.back().strLabel == command.strLabel) {
                            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLineNumber); 
                                      LOG_STRING("CONTINUE: unwinding inner loop:"); 
                                      LOG_STRING(command.strLabel));
                            m_loopStateStack.pop_back();
                        }
                    } else {
                        // Target reached — clear skip, keep LoopState alive, run loop logic.
                        m_strSkipUntilLabel.clear();
                        m_eSkipReason = SkipReason::NONE;
                        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLineNumber); 
                                  LOG_STRING("CONTINUE: resuming at END_REPEAT:"); 
                                  LOG_STRING(command.strLabel));
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
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLineNumber); 
                          LOG_STRING("BREAK:"); 
                          LOG_STRING(command.strLabel));
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
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLineNumber); 
                          LOG_STRING("CONTINUE:"); 
                          LOG_STRING(command.strLabel));
                m_strSkipUntilLabel = command.strLabel;
                m_eSkipReason       = SkipReason::CONTINUE_LOOP;
            }

        // -----------------------------------------------------------------
        // PRINT [text]
        // Expand all $macros in the stored text template and emit one log
        // line.  An empty template produces a blank line.
        // No plugin is involved — this is a native interpreter statement.
        // -----------------------------------------------------------------
        } else if constexpr (std::is_same_v<T, PrintStatement>) {
            if (bRealExec && m_eSkipReason == SkipReason::NONE) {
                std::string strExpanded = command.strText;
                m_replaceVariableMacros(strExpanded);
                LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING(strLineNumber);
                          LOG_STRING(strExpanded));
            }

        // -----------------------------------------------------------------
        // DELAY <value> <unit>
        // Pause execution for the requested duration.
        // Value and unit are pre-resolved at validation time — no parsing
        // needed here.  The dry-run pass silently skips DELAY nodes so that
        // argument validation is not slowed down by actual sleeps.
        // Skipped (GOTO / BREAK / CONTINUE) DELAY nodes are also no-ops.
        // -----------------------------------------------------------------
        } else if constexpr (std::is_same_v<T, DelayStatement>) {
            if (bRealExec && m_eSkipReason == SkipReason::NONE) {
                const std::string strUnit = (command.eUnit == DelayUnit::US)  ? "us"  :
                                            (command.eUnit == DelayUnit::MS)  ? "ms"  : "sec";
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLineNumber); 
                          LOG_STRING("DELAY:"); 
                          LOG_STRING(std::to_string(command.szValue));
                          LOG_STRING(strUnit));
                switch (command.eUnit) {
                    case DelayUnit::US:  utime::delay_us(command.szValue);      break;
                    case DelayUnit::MS:  utime::delay_ms(command.szValue);      break;
                    case DelayUnit::SEC: utime::delay_seconds(command.szValue); break;
                }
            }

        // -----------------------------------------------------------------
        // name ?= <string value>
        // Expand $macros in the value template and write the result into
        // m_RuntimeVarMacros.  This makes the value immediately visible to
        // all subsequent $macro lookups at tier 2 — exactly the same as a
        // successful MacroCommand dispatch.
        // During the dry-run pass the node is silently ignored (no expansion,
        // no write), consistent with the two-pass model used by plugin commands.
        // -----------------------------------------------------------------
        } else if constexpr (std::is_same_v<T, VarMacroInit>) {
            if (bRealExec && m_eSkipReason == SkipReason::NONE) {
                std::string strExpanded = command.strValueTpl;
                m_replaceVariableMacros(strExpanded);

                // If the expanded value starts with "EVAL " delegate to the
                // unified condition evaluator and store "TRUE" or "FALSE".
                std::string strEvalCheck = strExpanded;
                ustring::stripPrefix(strEvalCheck, kEvalPrefix);
                if (strEvalCheck.size() < strExpanded.size())
                {
                    bool bEvalResult = false;
                    if (m_evaluateCondition(strExpanded, bEvalResult)) {
                        strExpanded = bEvalResult ? "TRUE" : "FALSE";
                        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLineNumber); 
                                  LOG_STRING("EVAL result for VAR_INIT ["); 
                                  LOG_STRING(command.strName);
                                  LOG_STRING("] -> ["); 
                                  LOG_STRING(strExpanded); LOG_STRING("]"));
                    } else {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strLineNumber); 
                                  LOG_STRING("EVAL failed for VAR_INIT ["); 
                                  LOG_STRING(command.strName);
                                  LOG_STRING("]"));
                        bRetVal = false;
                        return;
                    }
                }

                m_RuntimeVarMacros[command.strName] = strExpanded;
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLineNumber); 
                          LOG_STRING("VAR_INIT ["); LOG_STRING(command.strName);
                          LOG_STRING("]->["); 
                          LOG_STRING(strExpanded); LOG_STRING("]"));
            }

        // -----------------------------------------------------------------
        // name ?= FORMAT input | format_pattern
        //
        // 1. Expand $macros in both input and format templates.
        // 2. Tokenise the expanded input by whitespace → items[0..N-1].
        // 3. Walk the format template character by character:
        //      - '%' followed by a decimal digit → substitute items[digit]
        //      - '%' at end of template          → error (caught at validation)
        //      - any other char                  → copy verbatim
        // 4. Store the assembled string in m_RuntimeVarMacros[name].
        //
        // Out-of-range index (digit >= number of input tokens) is a runtime
        // error: logged and the command fails so the script is aborted.
        // -----------------------------------------------------------------
        } else if constexpr (std::is_same_v<T, FormatStatement>) {
            if (bRealExec && m_eSkipReason == SkipReason::NONE) {

                // ── Step 1: macro expansion ───────────────────────────────
                std::string strInput  = command.strInputTpl;
                std::string strFormat = command.strFormatTpl;
                m_replaceVariableMacros(strInput);
                m_replaceVariableMacros(strFormat);

                // ── Step 2: tokenise input by whitespace ──────────────────
                std::vector<std::string> vItems;
                {
                    std::istringstream iss(strInput);
                    std::string token;
                    while (iss >> token) {
                        vItems.push_back(std::move(token));
                    }
                }
                const size_t szNrItems = vItems.size();

                if (szNrItems == 0) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strLineNumber); 
                              LOG_STRING("FORMAT ["); LOG_STRING(command.strName);
                              LOG_STRING("]: input expanded to empty — no items to substitute"));
                    bRetVal = false;
                    return;
                }

                // ── Step 3: build output by walking the format template ───
                std::string strResult;
                strResult.reserve(strFormat.size());

                for (size_t i = 0; i < strFormat.size(); ++i) {
                    const char c = strFormat[i];
                    if (c == '%') {
                        // Validator guarantees a digit follows, but guard anyway.
                        if (i + 1 >= strFormat.size()) {
                            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strLineNumber); 
                                      LOG_STRING("FORMAT ["); 
                                      LOG_STRING(command.strName);
                                      LOG_STRING("]: '%' at end of expanded format template"));
                            bRetVal = false;
                            return;
                        }
                        const char cIdx = strFormat[++i];
                        if (!std::isdigit(static_cast<unsigned char>(cIdx))) {
                            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strLineNumber); 
                                      LOG_STRING("FORMAT ["); 
                                      LOG_STRING(command.strName);
                                      LOG_STRING("]: '%"); 
                                      LOG_STRING(std::string(1, cIdx));
                                      LOG_STRING("' — index character is not a digit"));
                            bRetVal = false;
                            return;
                        }
                        const size_t uiIndex = static_cast<size_t>(cIdx - '0');
                        if (uiIndex >= szNrItems) {
                            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strLineNumber); 
                                      LOG_STRING("FORMAT ["); 
                                      LOG_STRING(command.strName);
                                      LOG_STRING("]: index %"); 
                                      LOG_STRING(std::string(1, cIdx));
                                      LOG_STRING("out of range (input has");
                                      LOG_SIZET(szNrItems); 
                                      LOG_STRING("items)"));
                            bRetVal = false;
                            return;
                        }
                        strResult += vItems[uiIndex];
                    } else {
                        strResult += c;
                    }
                }

                // ── Step 4: store result ──────────────────────────────────
                m_RuntimeVarMacros[command.strName] = strResult;
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLineNumber); 
                          LOG_STRING("FORMAT ["); 
                          LOG_STRING(command.strName);
                          LOG_STRING("]->["); 
                          LOG_STRING(strResult); 
                          LOG_STRING("]"));
            }

        // -----------------------------------------------------------------
        // name ?= MATH <expression>
        //
        // 1. Expand $macros in the expression template.
        // 2. Feed the expanded string to Calculator::evaluate().
        // 3. Convert the returned double to a clean string:
        //      - Integer-valued results print without a decimal point (5, not 5.0)
        //      - Floating-point results use up to 15 significant digits with
        //        trailing zeros stripped (3.14159, not 3.141590000000000)
        // 4. Store the string result in m_RuntimeVarMacros[name].
        //
        // The Calculator variable map (m_mathVars) is persistent for the
        // lifetime of this ScriptInterpreter instance, so intra-expression
        // assignments  (e.g.  MATH x = 5 + 3)  survive across MATH statements
        // and are accessible in later evaluations as plain identifiers.
        //
        // During the dry-run pass (bRealExec == false) the node is silently
        // ignored — consistent with VarMacroInit and FormatStatement.
        // -----------------------------------------------------------------
        } else if constexpr (std::is_same_v<T, MathStatement>) {
            if (bRealExec && m_eSkipReason == SkipReason::NONE) {

                // ── Step 1: macro expansion ───────────────────────────────
                std::string strExpr = command.strExprTpl;
                m_replaceVariableMacros(strExpr);

                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLineNumber); 
                          LOG_STRING("MATH ["); 
                          LOG_STRING(command.strName);
                          LOG_STRING("] expr=["); 
                          LOG_STRING(strExpr); 
                          LOG_STRING("]"));

                // ── Step 2: evaluate ──────────────────────────────────────
                double dResult = 0.0;
                try {
                    Calculator calc(strExpr, m_mathVars);
                    dResult = calc.evaluate();
                } catch (const std::exception& ex) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strLineNumber); 
                              LOG_STRING("MATH ["); 
                              LOG_STRING(command.strName);
                              LOG_STRING("]: evaluation failed:"); 
                              LOG_STRING(ex.what());
                              LOG_STRING("expr=["); 
                              LOG_STRING(strExpr); 
                              LOG_STRING("]"));
                    bRetVal = false;
                    return;
                }

                // ── Step 3: double → string ───────────────────────────────
                // Use defaultfloat + 15 significant digits so integer results
                // print cleanly (5, not 5.000000) and precision is preserved.
                std::string strResult;
                {
                    std::ostringstream oss;
                    oss << std::defaultfloat << std::setprecision(15) << dResult;
                    strResult = oss.str();
                }

                // ── Step 4: store ─────────────────────────────────────────
                m_RuntimeVarMacros[command.strName] = strResult;
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLineNumber); 
                          LOG_STRING("MATH ["); 
                          LOG_STRING(command.strName);
                          LOG_STRING("]->["); 
                          LOG_STRING(strResult); 
                          LOG_STRING("]"));
            }

        // -----------------------------------------------------------------
        // BREAKPOINT [label]
        //
        // Suspends script execution and waits for user input via CheckContinue.
        //
        //   a/A  → confirm abort (y/Y) → bRetVal = false → script aborts
        //   Space → skip this breakpoint, continue normally
        //   other → continue normally
        //
        // The optional label template is $macro-expanded at runtime so that
        // loop indices and variable values are reflected in the log output.
        //
        // Skipped silently during the dry-run validation pass (bRealExec == false)
        // and inside any active GOTO / BREAK / CONTINUE skip region — exactly
        // consistent with DELAY and PRINT behaviour.
        // -----------------------------------------------------------------
        } else if constexpr (std::is_same_v<T, BreakpointStatement>) {
            if (bRealExec && m_eSkipReason == SkipReason::NONE) {

                // Expand $macros in the label so the user sees current values
                std::string strLabel = command.strLabelTpl;
                m_replaceVariableMacros(strLabel);

                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLineNumber); 
                          LOG_STRING("BREAKPOINT hit:");
                          LOG_STRING(strLabel.empty() ? "<no label>" : strLabel));

                CheckContinue checkContinue;
                const bool bOk = checkContinue(strLabel);

                if (!bOk) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strLineNumber); 
                              LOG_STRING("BREAKPOINT: script aborted by user"));
                    bRetVal = false;
                }
                // Note: the skip path (Space key) is handled inside CheckContinue
                // by setting *pbSkip. BREAKPOINT does not propagate the skip to
                // surrounding script flow — it only skips THIS breakpoint, not
                // the next command. nullptr is passed for pbSkip intentionally:
                // the Space key simply acts as "continue" for BREAKPOINT.
            }
        }
    }, data.command);

    if (bRealExec && m_eSkipReason == SkipReason::NONE && bIsPluginCommand) {
        LOG_PRINT((bRetVal ? LOG_INFO : LOG_ERROR), LOG_HDR; LOG_STRING(strLineNumber);
                LOG_STRING("Command execution");
                LOG_STRING(bRetVal ? "ok" : "failed"));
    }

    return bRetVal;

} // m_executeCommand()


/*-------------------------------------------------------------------------------
  Build a per-plugin O(1) command-name lookup used by m_crossCheckCommands.
  Replaces the inner std::find over vstrPluginCommands (a vector) with an
  unordered_set membership test.
-------------------------------------------------------------------------------*/

void ScriptInterpreter::m_buildPluginCommandIndex() noexcept
{
    m_pluginCmdIndex.clear();

    for (const auto& plugin : m_sScriptEntries->vPlugins) {
        auto& cmdSet = m_pluginCmdIndex[plugin.strPluginName];
        for (const auto& cmd : plugin.sGetParams.vstrPluginCommands) {
            cmdSet.insert(cmd);
        }
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("Plugin command index built:");
              LOG_STRING(std::to_string(m_pluginCmdIndex.size())); LOG_STRING("plugins"));

} // m_buildPluginCommandIndex()


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
    m_RuntimeVarMacros.clear();

    auto& vCommands = m_sScriptEntries->vCommands;
    size_t i = 0;

    while (i < vCommands.size()) {
        if (false == m_executeCommand(vCommands[i], bRealExec, i)) {
            bRetVal = false;
            break;
        }
        ++i;
    }

    LOG_PRINT((bRetVal ? LOG_DEBUG : LOG_ERROR), LOG_HDR; 
        LOG_STRING("Commands"); 
        LOG_STRING(bRealExec ? "execution" : "validation"); 
        LOG_STRING(bRetVal ? "ok" : "failed"));

    return bRetVal;

} /* m_executeCommands() */
