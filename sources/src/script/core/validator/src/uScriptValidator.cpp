
#include "uScriptValidator.hpp"
#include "uScriptDataTypes.hpp"
#include "IPluginDataTypes.hpp"

#include "uMathOpsValidator.hpp"
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
            if (!m_shpCommandValidator->validateCommand(rawLine.iLineNumber, rawLine.strContent, token)) {
                char strLineNumber[16];
                std::snprintf(strLineNumber, sizeof(strLineNumber), "%03d:", rawLine.iLineNumber);
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(strLineNumber); 
                          LOG_STRING("Failed to validate ["); 
                          LOG_STRING(rawLine.strContent); 
                          LOG_STRING("]"));
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
    std::map<std::string, int> gotolabelRegistry;   // earliest GOTO index per label
    std::set<std::string>      definedLabels;        // all LABEL names seen so far
    bool bRetVal = true;

    auto hasValidGotoBeforeLabel = [&gotolabelRegistry](const auto& label, int currentIndex) {
        auto it = gotolabelRegistry.find(label);
        return (it != gotolabelRegistry.end()) && (it->second < currentIndex);
    };

    for (const auto& command : m_sScriptEntries->vCommands) {
        std::visit([&](const auto& item) {
            using T = std::decay_t<decltype(item)>;

            if constexpr (std::is_same_v<T, Condition>) {
                gotolabelRegistry.try_emplace(item.strLabelName, iIndex);
            }

            if constexpr (std::is_same_v<T, Label>) {
                const std::string& label = item.strLabelName;

                if (!hasValidGotoBeforeLabel(label, iIndex)) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Label ["); LOG_STRING(label); LOG_STRING("] without preceding GOTO"));
                    bRetVal = false;
                }

                if (!definedLabels.insert(label).second) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Duplicate label found ["); LOG_STRING(label); LOG_STRING("]"));
                    bRetVal = false;
                }
            }
        }, command.command);

        ++iIndex;
    }

    // Post-validation: every GOTO must have a corresponding LABEL.
    for (const auto& [label, index] : gotolabelRegistry) {
        if (definedLabels.find(label) == definedLabels.end()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("GOTO"); LOG_STRING(label); LOG_STRING("without corresponding label"));
            bRetVal = false;
        }
    }

    LOG_PRINT((bRetVal ? LOG_DEBUG : LOG_ERROR), LOG_HDR; LOG_STRING("Conditions validation"); LOG_STRING(bRetVal ? "passed" : "failed"));

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

    // Single pass: collect name sets and validate loop structure simultaneously.
    //
    // allGotoLabelNames   — GOTO target names and LABEL names (for loop-label collision check)
    // allScriptMacroNames — script-level ?= macro names (for loop-index-macro collision check)
    // allLoopLabels       — loop labels seen so far (duplicate detection)
    // loopStack           — currently open loops, outermost at front / innermost at back
    //
    // The collision checks are applied when a REPEAT node is encountered.
    // Because GOTO/LABEL/MacroCommand nodes always appear before any REPEAT that
    // could conflict with them (the script is validated top-to-bottom), collecting
    // and checking in a single pass is correct: by the time any REPEAT is visited,
    // all preceding names are already in the sets.
    // If a REPEAT appears before a LABEL that shares its name, the name won't be
    // in allGotoLabelNames yet — this is acceptable because forward LABEL
    // declarations after a REPEAT are already caught by m_validateConditions.

    std::set<std::string> allGotoLabelNames;
    std::set<std::string> allScriptMacroNames;
    std::set<std::string> allLoopLabels;
    std::vector<std::string> loopStack;

    std::vector<std::pair<std::string, std::vector<std::string>>> vGotoContexts;
    std::map<std::string, std::vector<std::string>>               mapLabelContexts;
    std::set<std::string>                                         definedLabels;

    for (const auto& cmd : m_sScriptEntries->vCommands) {

        if (!bRetVal) {
            break;
        }

        std::visit([&](const auto& item) {
            using T = std::decay_t<decltype(item)>;

            // ----- collect name sets as we walk forward -----
            if constexpr (std::is_same_v<T, Condition>) {
                allGotoLabelNames.insert(item.strLabelName);
                vGotoContexts.emplace_back(item.strLabelName, loopStack);
            }
            else if constexpr (std::is_same_v<T, Label>) {
                allGotoLabelNames.insert(item.strLabelName);
                mapLabelContexts[item.strLabelName] = loopStack;
                definedLabels.insert(item.strLabelName);
            }
            else if constexpr (std::is_same_v<T, MacroCommand>) {
                allScriptMacroNames.insert(item.strVarMacroName);
            }
            else if constexpr (std::is_same_v<T, VarMacroInit>) {
                allScriptMacroNames.insert(item.strName);
            }

            // ----- loop open markers -----
            else if constexpr (std::is_same_v<T, RepeatTimes> || std::is_same_v<T, RepeatUntil>) {
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
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unclosed loop (missing END_REPEAT):"); LOG_STRING(label));
        }
        bRetVal = false;
    }

    // --- GOTO must not cross loop boundaries ----------------------------------
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
        }
    }

    LOG_PRINT((bRetVal ? LOG_DEBUG : LOG_ERROR), LOG_HDR; LOG_STRING("Loops validation"); LOG_STRING(bRetVal ? "passed" : "failed"));

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

    LOG_PRINT((bRetVal ? LOG_DEBUG : LOG_ERROR), LOG_HDR; LOG_STRING("Plugins validation"); LOG_STRING(bRetVal ? "passed" : "failed"));

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
        case Token::ARRAY_MACRO: {
                bRetVal = m_HandleArrayMacro(command);
            }
            break;
        case Token::VARIABLE_MACRO: {
                bRetVal = m_HandleVariableMacro(command);
            }
            break;
        case Token::VAR_MACRO_INIT: {
                bRetVal = m_HandleVarMacroInit(command);
            }
            break;
        case Token::FORMAT_STMT: {
                bRetVal = m_HandleFormatStmt(command);
            }
            break;
        case Token::MATH_STMT: {
                bRetVal = m_HandleMathStmt(command);
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
        case Token::PRINT_STMT: {
                bRetVal = m_HandlePrint(command);
            }
            break;
        case Token::DELAY_STMT: {
                bRetVal = m_HandleDelay(command);
            }
            break;
        case Token::BREAKPOINT_STMT: {
                bRetVal = m_HandleBreakpoint(command);
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
  m_parseArrayElements — CSV parser for array element lists.

  Parses the string after [= as a comma-separated list.  Elements may be
  enclosed in double-quotes to embed commas.  Leading/trailing whitespace is
  trimmed from each element.  Empty elements (e.g. from a trailing comma) are
  silently skipped.  Quotes are stripped from the stored value.

  Examples:
    "elem1, elem2, elem3"          → ["elem1", "elem2", "elem3"]
    "aaa bbb, ddd eee"             → ["aaa bbb", "ddd eee"]
    "\"a, b\", \"c, d\""           → ["a, b", "c, d"]
-------------------------------------------------------------------------------*/

bool ScriptValidator::m_parseArrayElements( const std::string& strList,
                                             std::vector<std::string>& vElements ) noexcept
{
    vElements.clear();

    bool        bInQuotes  = false;
    std::string strCurrent;

    for (size_t i = 0; i < strList.size(); ++i) {
        const char c = strList[i];

        if (c == '"') {
            bInQuotes = !bInQuotes;
            // do not add the quote character to the element value
        } else if (c == ',' && !bInQuotes) {
            // commit current element
            // trim leading and trailing whitespace
            size_t start = strCurrent.find_first_not_of(" \t");
            size_t end   = strCurrent.find_last_not_of(" \t");
            if (start != std::string::npos) {
                vElements.push_back(strCurrent.substr(start, end - start + 1));
            }
            // empty elements (nothing between two commas) are silently skipped
            strCurrent.clear();
        } else {
            strCurrent += c;
        }
    }

    if (bInQuotes) {
        // Unterminated quote
        return false;
    }

    // commit the last element
    size_t start = strCurrent.find_first_not_of(" \t");
    size_t end   = strCurrent.find_last_not_of(" \t");
    if (start != std::string::npos) {
        vElements.push_back(strCurrent.substr(start, end - start + 1));
    }

    return true;

} // m_parseArrayElements()


/*-------------------------------------------------------------------------------
  ARRAY_MACRO handler:  NAME [= elem1, elem2, ...

  - The macro name must be unique across constant macros and array macros.
  - The element list is parsed by m_parseArrayElements.
  - The resulting vector is stored in mapArrayMacros.
  - Array macros are NOT added to vCommands; like constant macros they are a
    declaration, not a runtime command.
-------------------------------------------------------------------------------*/

bool ScriptValidator::m_HandleArrayMacro( const std::string& command ) noexcept
{
    // Split at [= to get name and element list
    static const std::string kSep = "[=";
    auto sepPos = command.find(kSep);
    if (sepPos == std::string::npos) {
        return false;
    }

    std::string strName    = command.substr(0, sepPos);
    std::string strList    = command.substr(sepPos + kSep.size());

    // trim name
    size_t ns = strName.find_first_not_of(" \t");
    size_t ne = strName.find_last_not_of(" \t");
    if (ns == std::string::npos) { return false; }
    strName = strName.substr(ns, ne - ns + 1);

    // trim list
    size_t ls = strList.find_first_not_of(" \t");
    if (ls == std::string::npos) { return false; }
    strList = strList.substr(ls);

    // Name must not collide with an existing constant macro
    if (m_sScriptEntries->mapMacros.count(strName)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Array macro name conflicts with constant macro:"); LOG_STRING(strName));
        return false;
    }

    // Name must not collide with an existing array macro (duplicate)
    if (m_sScriptEntries->mapArrayMacros.count(strName)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Array macro already declared:"); LOG_STRING(strName));
        return false;
    }

    std::vector<std::string> vElements;
    if (!m_parseArrayElements(strList, vElements)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Array macro ["); LOG_STRING(strName);
                  LOG_STRING("]: unterminated quote in element list"));
        return false;
    }

    if (vElements.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Array macro ["); LOG_STRING(strName); LOG_STRING("]: no elements"));
        return false;
    }

    m_sScriptEntries->mapArrayMacros.emplace(strName, std::move(vElements));

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("Array macro ["); LOG_STRING(strName);
              LOG_STRING("] ="); LOG_STRING(std::to_string(
                  m_sScriptEntries->mapArrayMacros.at(strName).size()));
              LOG_STRING("elements"));

    return true;

} // m_HandleArrayMacro()



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
        MacroCommand{vstrTokens[1], vstrTokens[2], (vstrTokens.size() == 4) ? vstrTokens[3] : "", vstrTokens[0]}});
    return true;

} // m_HandleVariableMacro()


/*-------------------------------------------------------------------------------
  VAR_MACRO_INIT handler:  name ?= <string value>

  Splits on the first occurrence of ?= to separate the macro name from the
  value template.  The value is stored verbatim — $macro expansion is deferred
  to execution time so that loop indices, array elements, and other runtime
  values are reflected correctly.

  Rules enforced here:
  - The name must be a valid identifier (guaranteed by the regex in the command
    validator, re-checked below for safety).
  - The name must not already exist as a constant macro (it would be shadowed by
    the runtime map at tier-2, making the constant value unreachable).
  - An empty value (bare "name ?=") is valid and initialises the macro to "".

  The resulting VarMacroInit node is pushed to vCommands so that at execution
  time m_executeCommand can write the expanded value into m_RuntimeVarMacros.
-------------------------------------------------------------------------------*/

bool ScriptValidator::m_HandleVarMacroInit( const std::string& command ) noexcept
{
    // Split at first '?=' to get name and value template.
    static const std::string kSep = "?=";
    auto sepPos = command.find(kSep);
    if (sepPos == std::string::npos) {
        return false;
    }

    // Extract and trim the macro name.
    std::string strName = command.substr(0, sepPos);
    size_t ns = strName.find_first_not_of(" \t");
    size_t ne = strName.find_last_not_of(" \t");
    if (ns == std::string::npos) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("VAR_MACRO_INIT: missing macro name"));
        return false;
    }
    strName = strName.substr(ns, ne - ns + 1);

    // Extract and trim the value template (may be empty).
    std::string strValue;
    const size_t valStart = sepPos + kSep.size();
    if (valStart < command.size()) {
        strValue = command.substr(valStart);
        size_t vs = strValue.find_first_not_of(" \t");
        strValue  = (vs == std::string::npos) ? "" : strValue.substr(vs);
    }

    // A constant macro with the same name would be permanently shadowed by the
    // runtime tier-2 lookup — reject to avoid a confusing silent override.
    if (m_sScriptEntries->mapMacros.count(strName)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("VAR_MACRO_INIT ["); LOG_STRING(strName);
                  LOG_STRING("]: name already used as a constant macro (:=)"));
        return false;
    }

    m_sScriptEntries->vCommands.emplace_back(
        ScriptLine{m_iCurrentSourceLine, VarMacroInit{strName, strValue}});

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("VAR_MACRO_INIT ["); LOG_STRING(strName);
              LOG_STRING("] = ["); LOG_STRING(strValue.empty() ? "<empty>" : strValue); LOG_STRING("]"));

    return true;

} // m_HandleVarMacroInit()


/*-------------------------------------------------------------------------------
  FORMAT_STMT handler:  name ?= FORMAT input | format_pattern

  Splits at the first '?=' to extract the destination macro name, then at
  the first '|' within the remainder to separate the input template from the
  format template.  Both templates are stored verbatim — $macro substitution
  and %N expansion are deferred to execution time.

  Rules enforced at validation time:
  - The destination name must be a valid identifier.
  - The name must not collide with a constant macro (would be permanently
    shadowed by the runtime tier-2 lookup).
  - Both the input and format sides of '|' must be non-empty after trimming.
  - The format template must contain at least one %N placeholder.
  - Every %N index in the format template must be a single decimal digit (0-9).
    Out-of-range indices are not checked here; that is a runtime concern because
    the input word count is only known after $macro expansion.
-------------------------------------------------------------------------------*/

bool ScriptValidator::m_HandleFormatStmt( const std::string& command ) noexcept
{
    // ── 1.  Split at first '?=' ────────────────────────────────────────────
    static const std::string kAssign = "?=";
    const auto assignPos = command.find(kAssign);
    if (assignPos == std::string::npos) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FORMAT: missing '?='"));
        return false;
    }

    // Extract and trim destination name
    std::string strName = command.substr(0, assignPos);
    {
        const size_t ns = strName.find_first_not_of(" \t");
        const size_t ne = strName.find_last_not_of(" \t");
        if (ns == std::string::npos) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FORMAT: missing destination macro name"));
            return false;
        }
        strName = strName.substr(ns, ne - ns + 1);
    }

    // ── 2.  Strip "FORMAT" keyword from the RHS ────────────────────────────
    const size_t rhsStart = assignPos + kAssign.size();
    std::string strRhs = command.substr(rhsStart);
    {
        // trim leading whitespace
        const size_t rs = strRhs.find_first_not_of(" \t");
        strRhs = (rs == std::string::npos) ? "" : strRhs.substr(rs);
    }

    static const std::string kKeyword = "FORMAT";
    if (strRhs.size() < kKeyword.size() ||
        strRhs.compare(0, kKeyword.size(), kKeyword) != 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FORMAT: missing FORMAT keyword in RHS"));
        return false;
    }
    strRhs = strRhs.substr(kKeyword.size());  // strip "FORMAT"
    {
        const size_t rs = strRhs.find_first_not_of(" \t");
        strRhs = (rs == std::string::npos) ? "" : strRhs.substr(rs);
    }

    // ── 3.  Split at first '|' ─────────────────────────────────────────────
    const auto pipePos = strRhs.find('|');
    if (pipePos == std::string::npos) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("FORMAT: missing '|' separator between input and format template"));
        return false;
    }

    std::string strInput  = strRhs.substr(0, pipePos);
    std::string strFormat = strRhs.substr(pipePos + 1);

    // trim both sides
    auto trimStr = [](std::string& s) {
        const size_t fs = s.find_first_not_of(" \t");
        const size_t fe = s.find_last_not_of(" \t");
        s = (fs == std::string::npos) ? "" : s.substr(fs, fe - fs + 1);
    };
    trimStr(strInput);
    trimStr(strFormat);

    if (strInput.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FORMAT: input template is empty"));
        return false;
    }
    if (strFormat.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FORMAT: format template is empty"));
        return false;
    }

    // ── 4.  Validate format template has at least one %N placeholder ───────
    bool bHasPlaceholder = false;
    for (size_t i = 0; i < strFormat.size(); ++i) {
        if (strFormat[i] == '%') {
            if (i + 1 >= strFormat.size()) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("FORMAT: '%' at end of format template has no index"));
                return false;
            }
            const char cIdx = strFormat[i + 1];
            if (!std::isdigit(static_cast<unsigned char>(cIdx))) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("FORMAT: '%" ); LOG_STRING(std::string(1, cIdx));
                          LOG_STRING("' — index must be a single decimal digit (0-9)"));
                return false;
            }
            bHasPlaceholder = true;
            ++i; // skip the digit
        }
    }
    if (!bHasPlaceholder) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("FORMAT: format template contains no %N placeholder"));
        return false;
    }

    // ── 5.  Name collision with constant macros ────────────────────────────
    if (m_sScriptEntries->mapMacros.count(strName)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("FORMAT ["); 
                  LOG_STRING(strName);
                  LOG_STRING("]: name already used as a constant macro (:=)"));
        return false;
    }

    // ── 6.  Emit IR node ──────────────────────────────────────────────────
    m_sScriptEntries->vCommands.emplace_back(
        ScriptLine{m_iCurrentSourceLine, FormatStatement{strName, strInput, strFormat}});

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("FORMAT ["); LOG_STRING(strName);
              LOG_STRING("] input=["); LOG_STRING(strInput);
              LOG_STRING("] fmt=["); LOG_STRING(strFormat); 
              LOG_STRING("]"));

    return true;

} // m_HandleFormatStmt()


/*-------------------------------------------------------------------------------
  MATH_STMT handler:  name ?= MATH <expression>

  Splits at the first '?=' to extract the destination macro name, strips the
  "MATH" keyword, and stores the remainder verbatim as the expression template.
  $macro substitution and Calculator evaluation are both deferred to execution
  time — the expression may reference variable macros whose values are only
  known at runtime (loop indices, earlier MATH results, plugin outputs, etc.).

  Rules enforced at validation time:
  - The destination name must be a valid identifier.
  - The name must not collide with a constant macro (would be permanently
    shadowed at runtime).
  - The expression template must be non-empty after trimming.

  No arithmetic validation is attempted here.  Syntax errors in the expression
  are reported at execution time via Calculator::evaluate() throwing
  std::runtime_error, which is caught and logged as a command failure.
-------------------------------------------------------------------------------*/

bool ScriptValidator::m_HandleMathStmt( const std::string& command ) noexcept
{
    // ── 1. Split at first '?=' ─────────────────────────────────────────────
    static const std::string kAssign = "?=";
    const auto assignPos = command.find(kAssign);
    if (assignPos == std::string::npos) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("MATH: missing '?='"));
        return false;
    }

    // Extract and trim destination name
    std::string strName = command.substr(0, assignPos);
    {
        const size_t ns = strName.find_first_not_of(" \t");
        const size_t ne = strName.find_last_not_of(" \t");
        if (ns == std::string::npos) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("MATH: missing destination macro name"));
            return false;
        }
        strName = strName.substr(ns, ne - ns + 1);
    }

    // ── 2. Strip "MATH" keyword from the RHS ──────────────────────────────
    std::string strRhs = command.substr(assignPos + kAssign.size());
    {
        const size_t rs = strRhs.find_first_not_of(" \t");
        strRhs = (rs == std::string::npos) ? "" : strRhs.substr(rs);
    }

    static const std::string kKeyword = "MATH";
    if (strRhs.size() < kKeyword.size() ||
        strRhs.compare(0, kKeyword.size(), kKeyword) != 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("MATH: missing MATH keyword in RHS"));
        return false;
    }
    strRhs = strRhs.substr(kKeyword.size());
    {
        const size_t rs = strRhs.find_first_not_of(" \t");
        strRhs = (rs == std::string::npos) ? "" : strRhs.substr(rs);
    }

    // ── 3. Expression must be non-empty ───────────────────────────────────
    if (strRhs.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("MATH: expression template is empty"));
        return false;
    }

    // ── 4. Constant-macro name collision ──────────────────────────────────
    if (m_sScriptEntries->mapMacros.count(strName)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("MATH ["); LOG_STRING(strName);
                  LOG_STRING("]: name already used as a constant macro (:=)"));
        return false;
    }

    // ── 5. Emit IR node ───────────────────────────────────────────────────
    m_sScriptEntries->vCommands.emplace_back(
        ScriptLine{m_iCurrentSourceLine, MathStatement{strName, strRhs}});

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("MATH ["); LOG_STRING(strName);
              LOG_STRING("] expr=["); LOG_STRING(strRhs); LOG_STRING("]"));

    return true;

} // m_HandleMathStmt()



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
  PRINT handler.

  Syntax:   PRINT [text]
  The text portion (everything after the leading "PRINT" keyword and its
  separating whitespace) is stored verbatim — $macros are NOT expanded here.
  Expansion is deferred to execution time so that volatile macro values and
  loop index macros are always reflected correctly.
  A bare "PRINT" with no text is valid and will output a blank line at runtime.
-------------------------------------------------------------------------------*/

bool ScriptValidator::m_HandlePrint( const std::string& command ) noexcept
{
    // Strip the "PRINT" keyword and the single separating space (if present).
    // Everything that remains is the raw text template.
    std::string strText;
    const std::string kKeyword = "PRINT";
    if (command.size() > kKeyword.size()) {
        // skip keyword + one space
        strText = command.substr(kKeyword.size() + 1);
    }
    // else: bare "PRINT" — strText stays empty → blank line at runtime

    m_sScriptEntries->vCommands.emplace_back(
        ScriptLine{m_iCurrentSourceLine, PrintStatement{strText}});

    return true;

} // m_HandlePrint()


/*-------------------------------------------------------------------------------
  DELAY_STMT handler:  DELAY <value> <unit>

  Parses the two mandatory tokens after the DELAY keyword:
    <value>  — positive integer (>= 1); validated by the regex in the command
               validator, re-checked here with std::stoull for safety.
    <unit>   — one of:  us  (microseconds)
                        ms  (milliseconds)
                        sec (seconds)

  The value and unit are resolved to a DelayStatement at validation time so
  the interpreter does not need to parse anything at runtime — it just calls
  the appropriate utime::delay_* function directly.
-------------------------------------------------------------------------------*/

bool ScriptValidator::m_HandleDelay( const std::string& command ) noexcept
{
    // Tokenise: expect exactly ["DELAY", "<value>", "<unit>"]
    std::vector<std::string> vstrTokens;
    ustring::tokenize(command, vstrTokens);

    if (vstrTokens.size() != 3) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("DELAY: expected 'DELAY <value> <unit>', got");
                  LOG_UINT32(static_cast<uint32_t>(vstrTokens.size())); LOG_STRING("tokens"));
        return false;
    }

    // Parse value
    size_t szValue = 0;
    try {
        const unsigned long long ullVal = std::stoull(vstrTokens[1]);
        if (ullVal == 0) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("DELAY: value must be >= 1"));
            return false;
        }
        szValue = static_cast<size_t>(ullVal);
    } catch (...) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("DELAY: invalid value:"); LOG_STRING(vstrTokens[1]));
        return false;
    }

    // Parse unit
    DelayUnit eUnit;
    const std::string& strUnit = vstrTokens[2];
    if      (strUnit == "us")  { eUnit = DelayUnit::US;  }
    else if (strUnit == "ms")  { eUnit = DelayUnit::MS;  }
    else if (strUnit == "sec") { eUnit = DelayUnit::SEC; }
    else {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("DELAY: unknown unit '"); LOG_STRING(strUnit);
                  LOG_STRING("' — use us, ms or sec"));
        return false;
    }

    m_sScriptEntries->vCommands.emplace_back(
        ScriptLine{m_iCurrentSourceLine, DelayStatement{szValue, eUnit}});

    // Build a human-readable label for the log
    const std::string strLabel = std::to_string(szValue) + " " + strUnit;
    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("DELAY:"); LOG_STRING(strLabel));

    return true;

} // m_HandleDelay()


/*-------------------------------------------------------------------------------
  BREAKPOINT_STMT handler:  BREAKPOINT [label]

  Strips the BREAKPOINT keyword and stores the remainder verbatim as the label
  template.  The label is optional; an empty label is valid.  $macro expansion
  is deferred to execution time so that loop indices and variable macro values
  are current when the breakpoint fires.

  No validation of the label content is performed — it is purely cosmetic.
-------------------------------------------------------------------------------*/

bool ScriptValidator::m_HandleBreakpoint( const std::string& command ) noexcept
{
    // Strip the "BREAKPOINT" keyword; everything after the separating space
    // (if present) is the raw label template.
    std::string strLabel;
    const std::string kKeyword = "BREAKPOINT";
    if (command.size() > kKeyword.size()) {
        strLabel = command.substr(kKeyword.size() + 1); // skip keyword + one space
    }

    m_sScriptEntries->vCommands.emplace_back(
        ScriptLine{m_iCurrentSourceLine, BreakpointStatement{strLabel}});

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("BREAKPOINT label=[");
              LOG_STRING(strLabel.empty() ? "<none>" : strLabel);
              LOG_STRING("]"));

    return true;

} // m_HandleBreakpoint()


/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptValidator::m_ListStatements () noexcept
{
    if(false == m_sScriptEntries->vPlugins.empty()) {
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("PLUGINS"));
        std::for_each(m_sScriptEntries->vPlugins.begin(), m_sScriptEntries->vPlugins.end(), [&](const auto & item) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("    "); LOG_STRING(item.strPluginName); LOG_STRING("|"); LOG_STRING(item.strPluginVersRule); LOG_STRING("|"); LOG_STRING(item.strPluginVersRequested));
        });
    }

    if(false == m_sScriptEntries->mapMacros.empty()) {
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("CMACROS"));
        std::for_each(m_sScriptEntries->mapMacros.begin(), m_sScriptEntries->mapMacros.end(), [&](const auto & item) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("    "); LOG_STRING(item.first); LOG_STRING("->"); LOG_STRING(item.second));

        });
    }

    if(false == m_sScriptEntries->mapArrayMacros.empty()) {
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("ARRAY MACROS"));
        std::for_each(m_sScriptEntries->mapArrayMacros.begin(), m_sScriptEntries->mapArrayMacros.end(),
            [&](const auto& item) {
                std::ostringstream oss;
                oss << item.first << " [" << item.second.size() << "] = ";
                for (size_t k = 0; k < item.second.size(); ++k) {
                    if (k > 0) oss << ", ";
                    oss << "[" << k << "]=" << item.second[k];
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("    "); LOG_STRING(oss.str()));
            });
    }

    if(false == m_sScriptEntries->vCommands.empty()) {
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("COMMANDS"));
        std::for_each(m_sScriptEntries->vCommands.begin(), m_sScriptEntries->vCommands.end(), [&](const ScriptLine& data) {
            std::visit([&data](const auto & item) {
                using T = std::decay_t<decltype(item)>;
                const std::string strLine = std::to_string(data.iLineNumber) + ":";
                if constexpr (std::is_same_v<T, MacroCommand>) {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLine); LOG_STRING("    MCMD:"); LOG_STRING(item.strPlugin); LOG_STRING("|"); LOG_STRING(item.strCommand); LOG_STRING("|"); LOG_STRING(item.strParams); LOG_STRING("|"); LOG_STRING(item.strVarMacroName));
                } else if constexpr (std::is_same_v<T, Command>) {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLine); LOG_STRING("     CMD:"); LOG_STRING(item.strPlugin); LOG_STRING("|"); LOG_STRING(item.strCommand); LOG_STRING("|"); LOG_STRING(item.strParams));
                } else if constexpr (std::is_same_v<T, Condition>) {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLine); LOG_STRING("    COND:"); LOG_STRING(item.strCondition); LOG_STRING("LBL:"); LOG_STRING(item.strLabelName));
                } else if constexpr (std::is_same_v<T, Label>) {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLine); LOG_STRING("     LBL:"); LOG_STRING(item.strLabelName));
                } else if constexpr (std::is_same_v<T, RepeatTimes>) {
                    const std::string strCapture = item.strVarMacroName.empty() ? "" : (" -> $" + item.strVarMacroName);
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLine); LOG_STRING("  REPEAT_N:"); LOG_STRING(item.strLabel); LOG_STRING("x"); LOG_STRING(std::to_string(item.iCount)); LOG_STRING(strCapture));
                } else if constexpr (std::is_same_v<T, RepeatUntil>) {
                    const std::string strCapture = item.strVarMacroName.empty() ? "" : (" -> $" + item.strVarMacroName);
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLine); LOG_STRING("  REPEAT_U:"); LOG_STRING(item.strLabel); LOG_STRING("until ["); LOG_STRING(item.strCondition); LOG_STRING("]"); LOG_STRING(strCapture));
                } else if constexpr (std::is_same_v<T, RepeatEnd>) {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLine); LOG_STRING("END_REPEAT:"); LOG_STRING(item.strLabel));
                } else if constexpr (std::is_same_v<T, LoopBreak>) {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLine); LOG_STRING("    BREAK:"); LOG_STRING(item.strLabel));
                } else if constexpr (std::is_same_v<T, LoopContinue>) {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLine); LOG_STRING(" CONTINUE:"); LOG_STRING(item.strLabel));
                } else if constexpr (std::is_same_v<T, PrintStatement>) {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLine); LOG_STRING("    PRINT:"); LOG_STRING(item.strText.empty() ? "<blank>" : item.strText));
                } else if constexpr (std::is_same_v<T, DelayStatement>) {
                    const std::string strUnit = (item.eUnit == DelayUnit::US)  ? "us"  :(item.eUnit == DelayUnit::MS)  ? "ms"  : "sec";
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLine); LOG_STRING("    DELAY:"); LOG_STRING(std::to_string(item.szValue)); LOG_STRING(strUnit));
                } else if constexpr (std::is_same_v<T, BreakpointStatement>) {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLine); LOG_STRING("BREAKPOINT:"); LOG_STRING(item.strLabelTpl.empty() ? "<none>" : item.strLabelTpl));
                } else if constexpr (std::is_same_v<T, VarMacroInit>) {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLine); LOG_STRING(" VAR_INIT:"); LOG_STRING(item.strName); LOG_STRING("="); LOG_STRING(item.strValueTpl.empty() ? "<empty>" : item.strValueTpl));
                } else if constexpr (std::is_same_v<T, FormatStatement>) {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLine); LOG_STRING("   FORMAT:"); LOG_STRING(item.strName); LOG_STRING("<-["); LOG_STRING(item.strInputTpl); LOG_STRING("]|["); LOG_STRING(item.strFormatTpl); LOG_STRING("]"));
                } else if constexpr (std::is_same_v<T, MathStatement>) {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strLine); LOG_STRING("     MATH:"); LOG_STRING(item.strName); LOG_STRING("= eval["); LOG_STRING(item.strExprTpl); LOG_STRING("]"));
                }
            }, data.command);
        });
    }

    return true;

} // m_ListStatements()
