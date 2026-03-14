
#include "uScriptValidator.hpp"
#include "uScriptDataTypes.hpp"
#include "IPluginDataTypes.hpp"

#include "uEvaluator.hpp"
#include "uString.hpp"
#include "uLogger.hpp"

#include <string>
#include <vector>
#include <set>
#include <stack>
#include <regex>
#include <unordered_map>
#include <map>
#include <variant>
#include <utility>


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "CORE_SCR_V  |"
#define LOG_HDR    LOG_STRING(LT_HDR)


/////////////////////////////////////////////////////////////////////////////////
//                            CLASS IMPLEMENTATION                             //
/////////////////////////////////////////////////////////////////////////////////

/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptValidator::validateScript(std::vector<ScriptRawLine>& vRawLines, ScriptEntriesType& sScriptEntries)
{

    bool bRetVal = false;

    m_sScriptEntries = &sScriptEntries;

    do {

        if (false == m_validateScriptStatements(vRawLines)) {
            break;
        }

        if (false == m_validateConditions()) {
            break;
        }

        if (false == m_validateLoops()) {
            break;
        }

        if (false == m_validatePlugins()) {
            break;
        }

        m_ListStatements();

        bRetVal = true;

    } while(false);

    return bRetVal;

} // validateScript()



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptValidator::m_validateScriptStatements(std::vector<ScriptRawLine>& vRawLines) noexcept
{
    Token token;

    return std::all_of(vRawLines.begin(), vRawLines.end(),
        [&](ScriptRawLine& rawLine) {
            m_iCurrentSourceLine = rawLine.iLineNumber;
            ustring::replaceMacros(rawLine.strContent, m_sScriptEntries->mapMacros, SCRIPT_MACRO_MARKER);
            if (!m_shpCommandValidator->validateCommand(rawLine.strContent, token)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to validate ["); LOG_STRING(rawLine.strContent); LOG_STRING("]"));
                return false;
            }
            return m_preprocessScriptStatements(rawLine.strContent, token);
        });

} // m_validateScriptStatements()



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptValidator::m_validateConditions() noexcept
{
    int iIndex = 0;
    enum class EvalType { GOTOLABEL, LABEL };
    std::map<std::string, int> gotolabelRegistry;               // Tracks earliest GOTOLABEL per label
    std::map<int, std::pair<std::string, EvalType>> mapCond;    // Indexed label entries
    std::set<std::string> definedLabels;                        // Tracks all defined LABELs
    bool bRetVal = true;

    auto isLabelAlreadyDefined = [&mapCond](const auto & label) {
        return std::any_of(mapCond.begin(), mapCond.end(), [&label](const auto & pair) {
            return ((pair.second.first == label) && (pair.second.second == EvalType::LABEL));
        });
    };

    auto hasValidGotoBeforeLabel = [&gotolabelRegistry](const auto & label, const int currentIndex) {
        auto it = gotolabelRegistry.find(label);
        return (it != gotolabelRegistry.end()) && (it->second < currentIndex);
    };

    for (const auto& command : m_sScriptEntries->vCommands) {
        std::visit([&](const auto & item) {
            using T = std::decay_t<decltype(item)>;

            if constexpr (std::is_same_v<T, Condition>) {
                const std::string& label = item.strLabelName;
                mapCond[iIndex] = {label, EvalType::GOTOLABEL};

                // Register the earliest GOTOLABEL
                gotolabelRegistry.try_emplace(label, iIndex);
            }

            if constexpr (std::is_same_v<T, Label>) {
                const std::string& label = item.strLabelName;

                if (!hasValidGotoBeforeLabel(label, iIndex)) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Label ["); LOG_STRING(label); LOG_STRING("] without preceding GOTO"));
                    bRetVal = false;
                }

                if (isLabelAlreadyDefined(label)) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Duplicate label found ["); LOG_STRING(label); LOG_STRING("]"));
                    bRetVal = false;
                } else {
                    mapCond[iIndex] = {label, EvalType::LABEL};
                    definedLabels.insert(label);
                }
            }
        }, command.command);

        ++iIndex;
    }

    // Post-validation: Ensure every GOTOLABEL has a corresponding LABEL
    for (const auto& [label, index] : gotolabelRegistry) {
        if (definedLabels.find(label) == definedLabels.end()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("GOTO"); LOG_STRING(label); LOG_STRING("without corresponding label"));
            bRetVal = false;
        }
    }

    LOG_PRINT((bRetVal ? LOG_INFO : LOG_ERROR), LOG_HDR; LOG_STRING("Conditions validation"); LOG_STRING(bRetVal ? "passed" : "failed"));

    return bRetVal;

} // m_validateConditions


/*-------------------------------------------------------------------------------
  Validates the structure of REPEAT_TIMES / REPEAT_UNTIL / ENDREP blocks:
    1. Every REPEAT_TIMES/UNTIL has a matching ENDREP with the same label.
    2. Blocks are properly nested (no crossing).
    3. Loop labels are distinct from GOTO/LABEL names to avoid ambiguity.
    4. No GOTO/LABEL pair crosses a loop boundary (i.e. the enclosing-loop
       context must be identical at both the GOTO site and its LABEL site).
-------------------------------------------------------------------------------*/

bool ScriptValidator::m_validateLoops() noexcept
{
    bool bRetVal = true;

    // --- collect all GOTO/LABEL names (used for name-collision checks) -------
    std::set<std::string> allGotoLabelNames;
    for (const auto& cmd : m_sScriptEntries->vCommands) {
        std::visit([&](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, Condition>) {
                allGotoLabelNames.insert(item.strLabelName);
            } else if constexpr (std::is_same_v<T, Label>) {
                allGotoLabelNames.insert(item.strLabelName);
            }
        }, cmd.command);
    }

    // --- collect all script-level variable macro names -----------------------
    // A loop index macro name must not shadow a script-level ?= macro: the
    // resolution order puts loop scope first, so the script-level value would
    // become permanently invisible inside the loop.
    std::set<std::string> allScriptMacroNames;
    for (const auto& cmd : m_sScriptEntries->vCommands) {
        std::visit([&](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, MacroCommand>) {
                allScriptMacroNames.insert(item.strVarMacroName);
            }
        }, cmd.command);
    }

    // --- single pass: validate structure and record per-command loop context -
    // The "loop context" of a command is the ordered list of enclosing loop
    // labels at that point in the script (outermost first).
    std::vector<std::string> loopStack;          // currently open loops (innermost at back)
    std::set<std::string>    allLoopLabels;       // all loop labels seen so far

    // For each GOTO and LABEL, remember the loop context at its position.
    // A GOTO may target the same label from multiple sites, so use a vector.
    std::vector<std::pair<std::string, std::vector<std::string>>> vGotoContexts;   // (targetLabel, context)
    std::map<std::string, std::vector<std::string>>               mapLabelContexts; // label → context

    for (const auto& cmd : m_sScriptEntries->vCommands) {

        if (!bRetVal) {
            break; // structural errors corrupt the stack; stop early
        }

        std::visit([&](const auto& item) {
            using T = std::decay_t<decltype(item)>;

            // ----- loop open markers -----
            if constexpr (std::is_same_v<T, RepeatTimes> || std::is_same_v<T, RepeatUntil>) {
                const std::string& label = item.strLabel;

                if (!allLoopLabels.insert(label).second) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Duplicate loop label:"); LOG_STRING(label));
                    bRetVal = false;
                    return;
                }

                if (allGotoLabelNames.count(label)) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Loop label conflicts with GOTO/LABEL name:"); LOG_STRING(label));
                    bRetVal = false;
                    return;
                }

                // Check that the iteration-index macro name (if any) does not collide
                // with an existing script-level variable macro.  Since loop scope is
                // resolved before script scope, such a collision would permanently hide
                // the script-level value inside the loop body.
                if (!item.strVarMacroName.empty() &&
                    allScriptMacroNames.count(item.strVarMacroName)) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR;
                              LOG_STRING("Loop index macro ["); LOG_STRING(item.strVarMacroName);
                              LOG_STRING("] shadows an existing script-level variable macro"));
                    bRetVal = false;
                    return;
                }

                loopStack.push_back(label);
            }

            // ----- loop close marker -----
            else if constexpr (std::is_same_v<T, RepeatEnd>) {
                const std::string& label = item.strLabel;

                if (loopStack.empty()) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("END_REPEAT without matching REPEAT:"); LOG_STRING(label));
                    bRetVal = false;
                    return;
                }

                if (loopStack.back() != label) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR;
                              LOG_STRING("END_REPEAT label mismatch: expected ["); LOG_STRING(loopStack.back());
                              LOG_STRING("] got ["); LOG_STRING(label); LOG_STRING("]"));
                    bRetVal = false;
                    return;
                }

                loopStack.pop_back();
            }

            // ----- record GOTO context -----
            else if constexpr (std::is_same_v<T, Condition>) {
                vGotoContexts.emplace_back(item.strLabelName, loopStack);
            }

            // ----- record LABEL context -----
            else if constexpr (std::is_same_v<T, Label>) {
                mapLabelContexts[item.strLabelName] = loopStack;
            }

            // ----- BREAK / CONTINUE — label must be an enclosing loop -----
            else if constexpr (std::is_same_v<T, LoopBreak> || std::is_same_v<T, LoopContinue>) {
                const std::string& label = item.strLabel;
                const char* pszKeyword   = std::is_same_v<T, LoopBreak> ? "BREAK" : "CONTINUE";

                if (loopStack.empty()) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR;
                              LOG_STRING(pszKeyword); LOG_STRING(label);
                              LOG_STRING("used outside any loop"));
                    bRetVal = false;
                    return;
                }

                // The label must appear somewhere in the current enclosing-loop stack.
                bool bFound = std::any_of(loopStack.begin(), loopStack.end(),
                    [&label](const std::string& l) { return l == label; });

                if (!bFound) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR;
                              LOG_STRING(pszKeyword); LOG_STRING(label);
                              LOG_STRING("does not name an enclosing loop"));
                    bRetVal = false;
                }
            }

        }, cmd.command);
    }

    // --- unclosed loops -------------------------------------------------------
    if (bRetVal && !loopStack.empty()) {
        for (const auto& label : loopStack) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unclosed loop (missing ENDREP):"); LOG_STRING(label));
        }
        bRetVal = false;
    }

    // --- GOTO must not cross loop boundaries ----------------------------------
    // Both the GOTO and its target LABEL must reside in exactly the same
    // enclosing-loop context.  This prevents jumping into or out of a loop body
    // which would leave the runtime loop-state stack in an inconsistent state.
    if (bRetVal) {
        for (const auto& [targetLabel, gotoCtx] : vGotoContexts) {
            auto it = mapLabelContexts.find(targetLabel);
            if (it != mapLabelContexts.end()) {
                if (gotoCtx != it->second) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR;
                              LOG_STRING("GOTO crosses loop boundary (contexts differ) for label:"); LOG_STRING(targetLabel));
                    bRetVal = false;
                }
            }
            // missing labels are already caught by m_validateConditions
        }
    }

    LOG_PRINT((bRetVal ? LOG_INFO : LOG_ERROR), LOG_HDR; LOG_STRING("Loops validation"); LOG_STRING(bRetVal ? "passed" : "failed"));

    return bRetVal;

} // m_validateLoops()


/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptValidator::m_validatePlugins () noexcept
{
    bool bRetVal = true;

    // list of plugins used by the commands (store them in a std::set because only one occurence is relevant)
    std::set<std::string> usedPlugins;
    std::for_each(m_sScriptEntries->vCommands.begin(), m_sScriptEntries->vCommands.end(),
        [&usedPlugins](const ScriptLine& data) {
            std::visit([&usedPlugins](const auto & item) {
                using T = std::decay_t<decltype(item)>;

                if constexpr (std::is_same_v<T, MacroCommand>) {
                    usedPlugins.insert(item.strPlugin);
                }

                if constexpr (std::is_same_v<T, Command>) {
                    usedPlugins.insert(item.strPlugin);
                }
            }, data.command);
        });

    // set of loaded plugins from the vPlugins
    std::set<std::string> loadedPlugins;
    std::transform(m_sScriptEntries->vPlugins.begin(), m_sScriptEntries->vPlugins.end(), std::inserter(loadedPlugins, loadedPlugins.begin()),
        [](const auto & item) {
            return item.strPluginName;
        });

    // set of used but not-loaded plugins
    std::set<std::string> notloadedPlugins;
    std::set_difference(usedPlugins.begin(), usedPlugins.end(), loadedPlugins.begin(), loadedPlugins.end(), std::inserter(notloadedPlugins, notloadedPlugins.begin()));

    // set of loaded but not used plugins
    std::set<std::string> notusedPlugins;
    std::set_difference(loadedPlugins.begin(), loadedPlugins.end(), usedPlugins.begin(), usedPlugins.end(), std::inserter(notusedPlugins, notusedPlugins.begin()));

    // lambda to print a set
    auto printSet = [](const std::set<std::string>& s, const std::string& name, bool bError = false) {
        std::ostringstream oss;
        oss << name << ": ";
        for (const auto& item : s) {
            oss << item << " ";
        }
        LOG_PRINT((bError ? LOG_ERROR : LOG_VERBOSE), LOG_HDR; LOG_STRING(oss.str()));
    };

    printSet(usedPlugins,   "Needed plugins");
    printSet(loadedPlugins, "Loaded plugins");

    // not really an error but printed in order to notify the user
    if (!notusedPlugins.empty()) {
        printSet(notusedPlugins, "Unused plugins", false /*bError*/);
    }

    if (!notloadedPlugins.empty()) {
        printSet(notloadedPlugins, "Missing plugins", true /*bError*/);
        bRetVal = false;
    }

    LOG_PRINT((bRetVal ? LOG_INFO : LOG_ERROR), LOG_HDR; LOG_STRING("Plugins validation"); LOG_STRING(bRetVal ? "passed" : "failed"));

    return bRetVal;

} // m_validatePlugins()


/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptValidator::m_preprocessScriptStatements ( const std::string& command, const Token token ) noexcept
{
    bool bRetVal = false;

    switch(token) {
        case Token::LOAD_PLUGIN: {
                bRetVal = m_HandleLoadPlugin(command);
            }
            break;
        case Token::CONSTANT_MACRO: {
                bRetVal = m_HandleConstantMacro(command);
            }
            break;
        case Token::VARIABLE_MACRO: {
                bRetVal = m_HandleVariableMacro(command);
            }
            break;
        case Token::COMMAND: {
                bRetVal = m_HandleCommand(command);
            }
            break;
        case Token::IF_GOTO_LABEL: {
                bRetVal = m_HandleCondition(command);
            }
            break;
        case Token::LABEL: {
                bRetVal = m_HandleLabel(command);
            }
            break;
        case Token::REPEAT: {
                bRetVal = m_HandleRepeat(command);
            }
            break;
        case Token::END_REPEAT: {
                bRetVal = m_HandleEndRepeat(command);
            }
            break;
        case Token::BREAK_LOOP: {
                bRetVal = m_HandleBreak(command);
            }
            break;
        case Token::CONTINUE_LOOP: {
                bRetVal = m_HandleContinue(command);
            }
            break;
        default: {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown command token received!"));
            }
            break;
    }

    if( false == bRetVal ) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to validate:"); LOG_STRING(command));
    }

    return bRetVal;

} // m_preprocessScriptStatements()


/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptValidator::m_HandleLoadPlugin ( const std::string& command ) noexcept
{
    bool bRetVal = false;

    std::vector<std::string> vstrTokens;
    ustring::tokenize(command, vstrTokens);
    size_t szSize = vstrTokens.size();


    do {

        if ((szSize != 2) && (szSize != 4)) {
            break;
        }

        if (std::find_if (m_sScriptEntries->vPlugins.begin(), m_sScriptEntries->vPlugins.end(),
        [&vstrTokens](const auto & item) {
        return item.strPluginName == vstrTokens[1];
        }) != m_sScriptEntries->vPlugins.end()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Plugin already exists:"); LOG_STRING(vstrTokens[1]));
            break;
        }

        bool bHasVersion = (4 == vstrTokens.size());
        m_sScriptEntries->vPlugins.emplace_back( vstrTokens[1], (bHasVersion ? vstrTokens[2] : std::string("")), (bHasVersion ? vstrTokens[3] : std::string("")), nullptr );

        bRetVal = true;

    } while(false);

    return bRetVal;

} // m_HandleLoadPlugin()



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptValidator::m_HandleConstantMacro ( const std::string& command ) noexcept
{
    std::vector<std::string> vstrTokens;
    ustring::tokenize(command, SCRIPT_CONSTANT_MACRO_SEPARATOR, vstrTokens);

    if (vstrTokens.size() < 2) {
        return false;
    }

    // cmacroname := cmacroval                         | cmacroname |  cmacroval   |
    auto aRetVal = m_sScriptEntries->mapMacros.emplace(vstrTokens[0], vstrTokens[1]);

    if (false == aRetVal.second) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Macro already exists:"); LOG_STRING(vstrTokens[0]));
    }

    // fail if the cmacro already exists
    return aRetVal.second;


} // m_HandleConstantMacro()



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptValidator::m_HandleVariableMacro ( const std::string& command ) noexcept
{
    std::vector<std::string> vstrDelimiters{SCRIPT_VARIABLE_MACRO_SEPARATOR, SCRIPT_PLUGIN_COMMAND_SEPARATOR, SCRIPT_COMMAND_PARAMS_SEPARATOR};
    std::vector<std::string> vstrTokens;
    ustring::tokenizeEx(command, vstrDelimiters, vstrTokens);
    size_t szSize = vstrTokens.size();

    if ((szSize != 3) && (szSize != 4)) {
        return false;
    }

    // vmacroname ?= plugin.command params
    m_sScriptEntries->vCommands.emplace_back(ScriptLine{m_iCurrentSourceLine,
        MacroCommand{vstrTokens[1], vstrTokens[2], (vstrTokens.size() == 4) ? vstrTokens[3] : "", vstrTokens[0], ""}});
    return true;

} // m_HandleVariableMacro()



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptValidator::m_HandleCommand(const std::string& command) noexcept
{
    std::vector<std::string> vstrDelimiters{SCRIPT_PLUGIN_COMMAND_SEPARATOR, SCRIPT_COMMAND_PARAMS_SEPARATOR};
    std::vector<std::string> vstrTokens;
    ustring::tokenizeEx(command, vstrDelimiters, vstrTokens);

    if (vstrTokens.size() < 2) {
        return false;
    }

    // plugin.command params
    m_sScriptEntries->vCommands.emplace_back(ScriptLine{m_iCurrentSourceLine,
        Command{vstrTokens[0], vstrTokens[1], (vstrTokens.size() == 3) ? vstrTokens[2] : ""}});
    return true;

} // m_HandleCommand()



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptValidator::m_HandleCondition ( const std::string& command ) noexcept
{
    auto tokenize = [](const std::string& expression, std::string& outCondition, std::string& outLabel) -> bool {
        static const std::regex pattern(R"(^(?:IF\s+(.*?)\s+)?GOTO\s+([A-Za-z_][A-Za-z0-9_]*)$)");
        std::smatch match;

        if (std::regex_match(expression, match, pattern)) {
            outCondition = match[1].matched ? match[1].str() : SCRIPT_COND_TRUE;
            outLabel = match[2];
            return true;
        }
        return false;
    };

    std::string condition, label;
    if (tokenize(command, condition, label)) {
        m_sScriptEntries->vCommands.emplace_back(ScriptLine{m_iCurrentSourceLine, Condition{condition, label}});
        return true;
    }

    return false;

} // m_HandleCondition()



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptValidator::m_HandleLabel ( const std::string& command ) noexcept
{
    std::vector<std::string> vstrTokens;
    ustring::tokenize(command, vstrTokens);

    if (vstrTokens.size() != 2) {
        return false;
    }

    // LABEL label
    m_sScriptEntries->vCommands.emplace_back(ScriptLine{m_iCurrentSourceLine, Label{vstrTokens[1]}});
    return true;

} // m_HandleLabel()


/*-------------------------------------------------------------------------------
  [varname ?=] REPEAT <label> <count>
  [varname ?=] REPEAT <label> UNTIL <condition>

  The optional "varname ?=" prefix names a variable macro that will receive the
  current 0-based iteration index as a string at the start of every iteration.
  Without the prefix the loop runs exactly as before (strVarMacroName is empty).

  A single handler distinguishes the two loop forms by inspecting the token
  that follows the label:
    positive integer → RepeatTimes (counted loop)
    keyword UNTIL    → RepeatUntil (conditional loop)

  Structural/nesting validation is deferred to m_validateLoops().
-------------------------------------------------------------------------------*/

bool ScriptValidator::m_HandleRepeat( const std::string& command ) noexcept
{
    // Parse the optional capture prefix and the mandatory REPEAT body.
    // Group 1 (optional): varname before "?="
    // Group 2:            loop label
    // Group 3:            remainder — either "N" or "UNTIL <cond>"
    static const std::regex pattern(
        R"(^(?:([A-Za-z_][A-Za-z0-9_]*)\s*\?=\s*)?REPEAT\s+([A-Za-z_][A-Za-z0-9_]*)\s+(\S+(?:\s+\S.*)?)$)");
    std::smatch match;

    if (!std::regex_match(command, match, pattern)) {
        return false;
    }

    const std::string strVarMacroName = match[1].matched ? match[1].str() : "";
    const std::string strLabel        = match[2].str();
    const std::string strRemainder    = match[3].str();   // either "N" or "UNTIL <cond>"

    // --- Counted form: [varname ?=] REPEAT label N ---
    static const std::regex countPattern(R"(^[1-9][0-9]*$)");
    if (std::regex_match(strRemainder, countPattern)) {
        int iCount = 0;
        try {
            iCount = std::stoi(strRemainder);
        } catch (...) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("REPEAT: invalid count value:"); LOG_STRING(strRemainder));
            return false;
        }
        m_sScriptEntries->vCommands.emplace_back(
            ScriptLine{m_iCurrentSourceLine, RepeatTimes{strLabel, iCount, strVarMacroName}});
        return true;
    }

    // --- Conditional form: [varname ?=] REPEAT label UNTIL <condition> ---
    static const std::regex untilPattern(R"(^UNTIL\s+(\S.*)$)");
    std::smatch untilMatch;
    if (std::regex_match(strRemainder, untilMatch, untilPattern)) {
        m_sScriptEntries->vCommands.emplace_back(
            ScriptLine{m_iCurrentSourceLine, RepeatUntil{strLabel, untilMatch[1].str(), strVarMacroName}});
        return true;
    }

    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("REPEAT: expected a count or UNTIL <condition> after label:"); LOG_STRING(strRemainder));
    return false;

} // m_HandleRepeat()


/*-------------------------------------------------------------------------------
  END_REPEAT <label>
-------------------------------------------------------------------------------*/

bool ScriptValidator::m_HandleEndRepeat( const std::string& command ) noexcept
{
    std::vector<std::string> vstrTokens;
    ustring::tokenize(command, vstrTokens);

    if (vstrTokens.size() != 2) {
        return false;
    }

    m_sScriptEntries->vCommands.emplace_back(ScriptLine{m_iCurrentSourceLine, RepeatEnd{vstrTokens[1]}});
    return true;

} // m_HandleEndRepeat()


/*-------------------------------------------------------------------------------
  BREAK <loop-label>
  CONTINUE <loop-label>
  Both share the same parse shape — one keyword, one identifier.
-------------------------------------------------------------------------------*/

bool ScriptValidator::m_HandleBreak( const std::string& command ) noexcept
{
    std::vector<std::string> vstrTokens;
    ustring::tokenize(command, vstrTokens);

    if (vstrTokens.size() != 2) {
        return false;
    }

    m_sScriptEntries->vCommands.emplace_back(ScriptLine{m_iCurrentSourceLine, LoopBreak{vstrTokens[1]}});
    return true;

} // m_HandleBreak()


bool ScriptValidator::m_HandleContinue( const std::string& command ) noexcept
{
    std::vector<std::string> vstrTokens;
    ustring::tokenize(command, vstrTokens);

    if (vstrTokens.size() != 2) {
        return false;
    }

    m_sScriptEntries->vCommands.emplace_back(ScriptLine{m_iCurrentSourceLine, LoopContinue{vstrTokens[1]}});
    return true;

} // m_HandleContinue()


/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptValidator::m_ListStatements () noexcept
{
    if(false == m_sScriptEntries->vPlugins.empty()) {
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("PLUGINS"));
        std::for_each(m_sScriptEntries->vPlugins.begin(), m_sScriptEntries->vPlugins.end(), [&](const auto & item) {
            LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("    "); LOG_STRING(item.strPluginName); LOG_STRING("|"); LOG_STRING(item.strPluginVersRule); LOG_STRING("|"); LOG_STRING(item.strPluginVersRequested));
        });
    }

    if(false == m_sScriptEntries->mapMacros.empty()) {
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("CMACROS"));
        std::for_each(m_sScriptEntries->mapMacros.begin(), m_sScriptEntries->mapMacros.end(), [&](const auto & item) {
            LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("    "); LOG_STRING(item.first); LOG_STRING("->"); LOG_STRING(item.second));

        });
    }

    if(false == m_sScriptEntries->vCommands.empty()) {
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("COMMANDS"));
        std::for_each(m_sScriptEntries->vCommands.begin(), m_sScriptEntries->vCommands.end(), [&](const ScriptLine& data) {
            std::visit([&data](const auto & item) {
                using T = std::decay_t<decltype(item)>;
                const std::string strLine = "[L" + std::to_string(data.iSourceLine) + "]";
                if constexpr (std::is_same_v<T, MacroCommand>) {
                    LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING(strLine); LOG_STRING("    MCMD:"); LOG_STRING(item.strPlugin); LOG_STRING("|"); LOG_STRING(item.strCommand); LOG_STRING("|"); LOG_STRING(item.strParams); LOG_STRING("|"); LOG_STRING(item.strVarMacroName); LOG_STRING("-> ["); LOG_STRING(item.strVarMacroValue); LOG_STRING("]"));
                } else if constexpr (std::is_same_v<T, Command>) {
                    LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING(strLine); LOG_STRING("     CMD:"); LOG_STRING(item.strPlugin); LOG_STRING("|"); LOG_STRING(item.strCommand); LOG_STRING("|"); LOG_STRING(item.strParams));
                } else if constexpr (std::is_same_v<T, Condition>) {
                    LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING(strLine); LOG_STRING("    COND:"); LOG_STRING(item.strCondition); LOG_STRING("LBL:"); LOG_STRING(item.strLabelName));
                } else if constexpr (std::is_same_v<T, Label>) {
                    LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING(strLine); LOG_STRING("     LBL:"); LOG_STRING(item.strLabelName));
                } else if constexpr (std::is_same_v<T, RepeatTimes>) {
                    const std::string strCapture = item.strVarMacroName.empty() ? "" : (" -> $" + item.strVarMacroName);
                    LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING(strLine); LOG_STRING("  REPEAT_N:"); LOG_STRING(item.strLabel); LOG_STRING("x"); LOG_STRING(std::to_string(item.iCount)); LOG_STRING(strCapture));
                } else if constexpr (std::is_same_v<T, RepeatUntil>) {
                    const std::string strCapture = item.strVarMacroName.empty() ? "" : (" -> $" + item.strVarMacroName);
                    LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING(strLine); LOG_STRING("  REPEAT_U:"); LOG_STRING(item.strLabel); LOG_STRING("until ["); LOG_STRING(item.strCondition); LOG_STRING("]"); LOG_STRING(strCapture));
                } else if constexpr (std::is_same_v<T, RepeatEnd>) {
                    LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING(strLine); LOG_STRING("END_REPEAT:"); LOG_STRING(item.strLabel));
                } else if constexpr (std::is_same_v<T, LoopBreak>) {
                    LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING(strLine); LOG_STRING("    BREAK:"); LOG_STRING(item.strLabel));
                } else if constexpr (std::is_same_v<T, LoopContinue>) {
                    LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING(strLine); LOG_STRING(" CONTINUE:"); LOG_STRING(item.strLabel));
                }
            }, data.command);
        });
    }

    return true;

} // m_ListStatements()
