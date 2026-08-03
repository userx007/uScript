
#include "uScriptValidator.hpp"
#include "uScriptDataTypes.hpp"
#include "uStreamStatementParser.hpp"
#include "IPluginDataTypes.hpp"

#include "uMathOpsValidator.hpp"
#include "uString.hpp"
#include "uLogger.hpp"
#include "uGuiNotify.hpp"

#include <string>
#include <vector>
#include <set>
#include <stack>
#include <regex>
#include <sstream>
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

        if (false == m_validateArraySizeUsage()) {
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
                auto lineNr = ustring::fmtLineNr(rawLine.iLineNumber);
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data()); 
                          LOG_STRING("Failed to validate ["); 
                          LOG_STRING(rawLine.strContent); 
                          LOG_STRING("]"));
                gui_notify_error_main(rawLine.iLineNumber);
                return false;
            }
            return m_preprocessScriptStatements(rawLine, token);
        });

} // m_validateScriptStatements()



/*-------------------------------------------------------------------------------
  Validates every $NAME.SIZE reference found in the compiled command list.
  NAME must be a declared array macro (present in mapArrayMacros); if NAME is
  a plain/constant/variable macro — or simply undeclared — SIZE cannot be
  applied to it and validation fails.

  Runs after m_validateScriptStatements() so mapArrayMacros already reflects
  every ARRAY_MACRO declaration in the file (forward references are allowed,
  mirroring the existing $NAME.$index runtime lookup behaviour).

  Only raw $macro *templates* are scanned (strParams, strCondition, strText,
  etc.) — these are exactly the fields left un-expanded by the constant-macro
  substitution pass, so any $NAME.SIZE still present here can only refer to
  an array macro, a variable ("?=") macro, or an undeclared name.
-------------------------------------------------------------------------------*/

bool ScriptValidator::m_validateArraySizeUsage() noexcept
{
    bool bRetVal = true;

    static const std::regex sizePattern(R"(\$([A-Za-z_][A-Za-z0-9_]*)\.SIZE(?![A-Za-z0-9_]))");

    auto checkField = [&](const std::string& strField, int iLineNumber) {
        auto itMatch = std::sregex_iterator(strField.begin(), strField.end(), sizePattern);
        auto itEnd   = std::sregex_iterator();

        for (; itMatch != itEnd; ++itMatch) {
            const std::string strName = (*itMatch)[1].str();

            if (m_sScriptEntries->mapArrayMacros.find(strName) == m_sScriptEntries->mapArrayMacros.end()) {
                auto lineNr = ustring::fmtLineNr(iLineNumber);
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                          LOG_STRING("SIZE applied to non-array macro ["); LOG_STRING(strName); LOG_STRING("]"));
                gui_notify_error_main(iLineNumber);
                bRetVal = false;
            }
        }
    };

    for (const auto& scriptLine : m_sScriptEntries->vCommands) {
        std::visit([&](const auto& command) {
            using T = std::decay_t<decltype(command)>;

            if constexpr (std::is_same_v<T, MacroCommand> || std::is_same_v<T, Command>) {
                checkField(command.strParams, scriptLine.iLineNumber);
            } else if constexpr (std::is_same_v<T, Condition>) {
                checkField(command.strCondition, scriptLine.iLineNumber);
            } else if constexpr (std::is_same_v<T, RepeatTimes>) {
                checkField(command.begin.strExpr, scriptLine.iLineNumber);
                checkField(command.end.strExpr,   scriptLine.iLineNumber);
                checkField(command.step.strExpr,  scriptLine.iLineNumber);
            } else if constexpr (std::is_same_v<T, RepeatUntil>) {
                checkField(command.strCondition, scriptLine.iLineNumber);
            } else if constexpr (std::is_same_v<T, PrintStatement>) {
                checkField(command.strText, scriptLine.iLineNumber);
            } else if constexpr (std::is_same_v<T, VarMacroInit>) {
                checkField(command.strValueTpl, scriptLine.iLineNumber);
            } else if constexpr (std::is_same_v<T, FormatStatement>) {
                checkField(command.strInputTpl, scriptLine.iLineNumber);
                checkField(command.strFormatTpl, scriptLine.iLineNumber);
            } else if constexpr (std::is_same_v<T, MathStatement>) {
                checkField(command.strExprTpl, scriptLine.iLineNumber);
            } else if constexpr (std::is_same_v<T, StreamStatement>) {
                for (const auto& field : command.vFields) {
                    checkField(field.strOffsetTpl, scriptLine.iLineNumber);
                    checkField(field.strLengthTpl, scriptLine.iLineNumber);
                    checkField(field.strValueTpl,  scriptLine.iLineNumber);
                }
            } else if constexpr (std::is_same_v<T, BreakpointStatement>) {
                checkField(command.strLabelTpl, scriptLine.iLineNumber);
            }
            // Label, RepeatEnd, LoopBreak, LoopContinue carry only plain
            // identifiers (no $macro templates) — nothing to scan.
        }, scriptLine.command);
    }

    LOG_PRINT((bRetVal ? LOG_DEBUG : LOG_ERROR), LOG_HDR;
               LOG_STRING("Array SIZE usage validation"); LOG_STRING(bRetVal ? "ok" : "failed"));

    return bRetVal;

} // m_validateArraySizeUsage()


/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptValidator::m_validateConditions() noexcept
{
    int iIndex = 0;
    std::map<std::string, int> gotolabelRegistry;   // earliest GOTO index per label
    std::set<std::string>      definedLabels;       // all LABEL names seen so far
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
                    gui_notify_error_main(command.iLineNumber);
                    bRetVal = false;
                }

                if (!definedLabels.insert(label).second) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Duplicate label found ["); LOG_STRING(label); LOG_STRING("]"));
                    gui_notify_error_main(command.iLineNumber);
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

    LOG_PRINT((bRetVal ? LOG_DEBUG : LOG_ERROR), LOG_HDR; LOG_STRING("Conditions validation"); LOG_STRING(bRetVal ? "ok" : "failed"));

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
                    gui_notify_error_main(cmd.iLineNumber);
                    bRetVal = false;
                    return;
                }
                if (allGotoLabelNames.count(label)) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Loop label conflicts with GOTO/LABEL name:"); LOG_STRING(label));
                    gui_notify_error_main(cmd.iLineNumber);
                    bRetVal = false;
                    return;
                }
                if (!item.strVarMacroName.empty() &&
                    allScriptMacroNames.count(item.strVarMacroName)) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR;
                              LOG_STRING("Loop index macro ["); LOG_STRING(item.strVarMacroName);
                              LOG_STRING("] shadows an existing script-level variable macro"));
                    gui_notify_error_main(cmd.iLineNumber);
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
                    gui_notify_error_main(cmd.iLineNumber);
                    bRetVal = false;
                    return;
                }
                if (loopStack.back() != label) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR;
                              LOG_STRING("END_REPEAT label mismatch: expected ["); LOG_STRING(loopStack.back());
                              LOG_STRING("] got ["); LOG_STRING(label); LOG_STRING("]"));
                    gui_notify_error_main(cmd.iLineNumber);
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
                    gui_notify_error_main(cmd.iLineNumber);
                    bRetVal = false;
                    return;
                }
                bool bFound = std::any_of(loopStack.begin(), loopStack.end(),
                    [&label](const std::string& l) { return l == label; });
                if (!bFound) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR;
                              LOG_STRING(pszKeyword); LOG_STRING(label);
                              LOG_STRING("does not name an enclosing loop"));
                    gui_notify_error_main(cmd.iLineNumber);
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

    LOG_PRINT((bRetVal ? LOG_DEBUG : LOG_ERROR), LOG_HDR; LOG_STRING("Loops validation"); LOG_STRING(bRetVal ? "ok" : "failed"));

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

    // Helper: extract the base name from a plugin name, stripping any ":N" suffix.
    // "UART:1" → "UART",  "UART" → "UART"
    auto baseName = [](const std::string& name) -> std::string {
        const auto pos = name.find(':');
        return (pos != std::string::npos) ? name.substr(0, pos) : name;
    };

    // set of used but not-loaded plugins.
    // An instanced name like "UART:1" is satisfied when its base "UART" is loaded,
    // so we only flag it missing when neither the exact name nor the base is loaded.
    std::set<std::string> notloadedPlugins;
    for (const auto& used : usedPlugins) {
        if (loadedPlugins.count(used) == 0 && loadedPlugins.count(baseName(used)) == 0) {
            notloadedPlugins.insert(used);
        }
    }

    // set of loaded but not used plugins.
    // A base plugin "UART" is considered used when any instanced command "UART:N"
    // references it, even if "UART" itself never appears verbatim in a command.
    std::set<std::string> notusedPlugins;
    for (const auto& loaded : loadedPlugins) {
        bool bUsed = usedPlugins.count(loaded) > 0;
        if (!bUsed) {
            for (const auto& used : usedPlugins) {
                if (baseName(used) == loaded) { bUsed = true; break; }
            }
        }
        if (!bUsed) notusedPlugins.insert(loaded);
    }

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

    LOG_PRINT((bRetVal ? LOG_DEBUG : LOG_ERROR), LOG_HDR; LOG_STRING("Plugins validation"); LOG_STRING(bRetVal ? "ok" : "failed"));

    return bRetVal;

} // m_validatePlugins()


/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptValidator::m_preprocessScriptStatements ( const ScriptRawLine& rawLine, const Token token ) noexcept
{
    bool bRetVal = false;

    switch(token) {
        case Token::LOAD_PLUGIN: {
                bRetVal = m_HandleLoadPlugin(rawLine);
            }
            break;
        case Token::CONSTANT_MACRO: {
                bRetVal = m_HandleConstantMacro(rawLine);
            }
            break;
        case Token::ARRAY_MACRO: {
                bRetVal = m_HandleArrayMacro(rawLine);
            }
            break;
        case Token::VARIABLE_MACRO: {
                bRetVal = m_HandleVariableMacro(rawLine);
            }
            break;
        case Token::VAR_MACRO_INIT: {
                bRetVal = m_HandleVarMacroInit(rawLine);
            }
            break;
        case Token::FORMAT_STMT: {
                bRetVal = m_HandleFormatStmt(rawLine);
            }
            break;
        case Token::MATH_STMT: {
                bRetVal = m_HandleMathStmt(rawLine);
            }
            break;
        case Token::BITSTREAM_STMT: {
                bRetVal = m_HandleBitstreamStmt(rawLine);
            }
            break;
        case Token::BYTESTREAM_STMT: {
                bRetVal = m_HandleBytestreamStmt(rawLine);
            }
            break;
        case Token::COMMAND: {
                bRetVal = m_HandleCommand(rawLine);
            }
            break;
        case Token::IF_GOTO_LABEL: {
                bRetVal = m_HandleCondition(rawLine);
            }
            break;
        case Token::LABEL: {
                bRetVal = m_HandleLabel(rawLine);
            }
            break;
        case Token::REPEAT: {
                bRetVal = m_HandleRepeat(rawLine);
            }
            break;
        case Token::END_REPEAT: {
                bRetVal = m_HandleEndRepeat(rawLine);
            }
            break;
        case Token::BREAK_LOOP: {
                bRetVal = m_HandleBreak(rawLine);
            }
            break;
        case Token::CONTINUE_LOOP: {
                bRetVal = m_HandleContinue(rawLine);
            }
            break;
        case Token::PRINT_STMT: {
                bRetVal = m_HandlePrint(rawLine);
            }
            break;
        case Token::DELAY_STMT: {
                bRetVal = m_HandleDelay(rawLine);
            }
            break;
        case Token::BREAKPOINT_STMT: {
                bRetVal = m_HandleBreakpoint(rawLine);
            }
            break;
        default: {  
                auto lineNr = ustring::fmtLineNr(rawLine.iLineNumber);
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                    LOG_STRING("Unknown command token received!"));
            }
            break;
    }

    if( false == bRetVal ) {
        auto lineNr = ustring::fmtLineNr(rawLine.iLineNumber);
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
            LOG_STRING("Failed to validate:"); LOG_STRING(rawLine.strContent));
        gui_notify_error_main(rawLine.iLineNumber);
    }

    return bRetVal;

} // m_preprocessScriptStatements()


/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptValidator::m_HandleLoadPlugin ( const ScriptRawLine& rawLine ) noexcept
{
    bool bRetVal = false;

    std::vector<std::string> vstrTokens;
    ustring::tokenize(rawLine.strContent, vstrTokens);
    size_t szSize = vstrTokens.size();


    do {

        if ((szSize != 2) && (szSize != 4)) {
            break;
        }

        if (std::find_if (m_sScriptEntries->vPlugins.begin(), m_sScriptEntries->vPlugins.end(),
        [&vstrTokens](const auto & item) {
        return item.strPluginName == vstrTokens[1];
        }) != m_sScriptEntries->vPlugins.end()) {
            auto lineNr = ustring::fmtLineNr(rawLine.iLineNumber);
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                LOG_STRING("Plugin already exists:"); 
                LOG_STRING(vstrTokens[1]));
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

bool ScriptValidator::m_HandleConstantMacro ( const ScriptRawLine& rawLine ) noexcept
{
    std::vector<std::string> vstrTokens;
    ustring::tokenize(rawLine.strContent, SCRIPT_CONSTANT_MACRO_SEPARATOR, vstrTokens);

    if (vstrTokens.size() < 2) {
        return false;
    }

    // cmacroname := cmacroval                         | cmacroname |  cmacroval   |
    auto aRetVal = m_sScriptEntries->mapMacros.emplace(vstrTokens[0], vstrTokens[1]);

    if (false == aRetVal.second) {
        auto lineNr = ustring::fmtLineNr(rawLine.iLineNumber);
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                  LOG_STRING("Macro already exists:"); LOG_STRING(vstrTokens[0]));
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

bool ScriptValidator::m_HandleArrayMacro( const ScriptRawLine& rawLine ) noexcept
{
    // Split at [= to get name and element list
    static const std::string kSep = "[=";
    auto sepPos = rawLine.strContent.find(kSep);
    if (sepPos == std::string::npos) {
        return false;
    }

    std::string strName = rawLine.strContent.substr(0, sepPos);
    std::string strList = rawLine.strContent.substr(sepPos + kSep.size());

    // trim name
    size_t ns = strName.find_first_not_of(" \t");
    size_t ne = strName.find_last_not_of(" \t");
    if (ns == std::string::npos) { 
        return false; 
    }
    strName = strName.substr(ns, ne - ns + 1);

    // trim list
    size_t ls = strList.find_first_not_of(" \t");
    if (ls == std::string::npos) { 
        return false; 
    }
    strList = strList.substr(ls);

    auto lineNr = ustring::fmtLineNr(rawLine.iLineNumber);
    // Name must not collide with an existing constant macro
    if (m_sScriptEntries->mapMacros.count(strName)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                  LOG_STRING("Array macro name conflicts with constant macro:"); 
                  LOG_STRING(strName));
        return false;
    }

    // Name must not collide with an existing array macro (duplicate)
    if (m_sScriptEntries->mapArrayMacros.count(strName)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                  LOG_STRING("Array macro already declared:"); 
                  LOG_STRING(strName));
        return false;
    }

    std::vector<std::string> vElements;
    if (!m_parseArrayElements(strList, vElements)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                  LOG_STRING("Array macro ["); 
                  LOG_STRING(strName);
                  LOG_STRING("]: unterminated quote in element list"));
        return false;
    }

    if (vElements.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                  LOG_STRING("Array macro ["); 
                  LOG_STRING(strName); 
                  LOG_STRING("]: no elements"));
        return false;
    }

    m_sScriptEntries->mapArrayMacros.emplace(strName, std::move(vElements));

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data());
              LOG_STRING("Array macro ["); 
              LOG_STRING(strName);
              LOG_STRING("]="); 
              LOG_STRING(std::to_string(m_sScriptEntries->mapArrayMacros.at(strName).size()));
              LOG_STRING("elements"));

    return true;

} // m_HandleArrayMacro()



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptValidator::m_HandleVariableMacro ( const ScriptRawLine& rawLine ) noexcept
{
    std::vector<std::string> vstrDelimiters{SCRIPT_VARIABLE_MACRO_SEPARATOR, 
                                            SCRIPT_PLUGIN_COMMAND_SEPARATOR, 
                                            SCRIPT_COMMAND_PARAMS_SEPARATOR};
    std::vector<std::string> vstrTokens;
    ustring::tokenizeEx(rawLine.strContent, vstrDelimiters, vstrTokens);
    size_t szSize = vstrTokens.size();

    if ((szSize != 3) && (szSize != 4)) {
        return false;
    }

    std::string strParams = (szSize == 4) ? vstrTokens[3] : "";
    const bool bThreaded = extractIsThreaded(strParams);

    // A threaded variable-capture command (?= ... &) is allowed: it launches
    // a background thread that keeps re-dispatching the underlying
    // PLUGIN.CMD in an endless loop (until script end or an unrecoverable
    // dispatch failure), atomically updating the runtime variable macro
    // every time new data is produced (see
    // ScriptInterpreter::m_executeCommand / m_setRuntimeVarMacro).  The
    // common use case is a "receive whatever is sent" CMD, e.g.
    // "VAL ?= UART.CMD < &" or "VAL ?= UART.CMD > H\"AABB\" | &", that keeps
    // VAL refreshed with the latest received data while the rest of the
    // script keeps running; VAL simply reads whatever value is current at
    // the time it is used.

    // vmacroname ?= plugin.command params
    m_sScriptEntries->vCommands.emplace_back(ScriptLine{m_iCurrentSourceLine,
        MacroCommand{vstrTokens[1], vstrTokens[2], strParams, vstrTokens[0], bThreaded}});
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

bool ScriptValidator::m_HandleVarMacroInit( const ScriptRawLine& rawLine ) noexcept
{
    // Split at first '?=' to get name and value template.
    static const std::string kSep = "?=";
    auto sepPos = rawLine.strContent.find(kSep);
    if (sepPos == std::string::npos) {
        return false;
    }

    auto lineNr = ustring::fmtLineNr(rawLine.iLineNumber);

    // Extract and trim the macro name.
    std::string strName = rawLine.strContent.substr(0, sepPos);
    size_t ns = strName.find_first_not_of(" \t");
    size_t ne = strName.find_last_not_of(" \t");
    if (ns == std::string::npos) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                  LOG_STRING("VAR_MACRO_INIT: missing macro name"));
        return false;
    }
    strName = strName.substr(ns, ne - ns + 1);

    // Extract and trim the value template (may be empty).
    std::string strValue;
    const size_t valStart = sepPos + kSep.size();
    if (valStart < rawLine.strContent.size()) {
        strValue = rawLine.strContent.substr(valStart);
        size_t vs = strValue.find_first_not_of(" \t");
        strValue  = (vs == std::string::npos) ? "" : strValue.substr(vs);
    }

    // A constant macro with the same name would be permanently shadowed by the
    // runtime tier-2 lookup — reject to avoid a confusing silent override.
    if (m_sScriptEntries->mapMacros.count(strName)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                  LOG_STRING("VAR_MACRO_INIT ["); LOG_STRING(strName);
                  LOG_STRING("]: name already used as a constant macro (:=)"));
        return false;
    }

    m_sScriptEntries->vCommands.emplace_back(
        ScriptLine{m_iCurrentSourceLine, VarMacroInit{strName, strValue}});

        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data());
              LOG_STRING("VAR_MACRO_INIT ["); LOG_STRING(strName);
              LOG_STRING("]=["); LOG_STRING(strValue.empty() ? "<none>" : strValue); LOG_STRING("]"));

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

bool ScriptValidator::m_HandleFormatStmt( const ScriptRawLine& rawLine ) noexcept
{
    auto lineNr = ustring::fmtLineNr(rawLine.iLineNumber);

    // ── 1.  Split at first '?=' ────────────────────────────────────────────
    static const std::string kAssign = "?=";
    const auto assignPos = rawLine.strContent.find(kAssign);
    if (assignPos == std::string::npos) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                  LOG_STRING("FORMAT: missing '?='"));
        return false;
    }

    // Extract and trim destination name
    std::string strName = rawLine.strContent.substr(0, assignPos);
    {
        const size_t ns = strName.find_first_not_of(" \t");
        const size_t ne = strName.find_last_not_of(" \t");
        if (ns == std::string::npos) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                      LOG_STRING("FORMAT: missing destination macro name"));
            return false;
        }
        strName = strName.substr(ns, ne - ns + 1);
    }

    // ── 2.  Strip "FORMAT" keyword from the RHS ────────────────────────────
    const size_t rhsStart = assignPos + kAssign.size();
    std::string strRhs = rawLine.strContent.substr(rhsStart);
    {
        // trim leading whitespace
        const size_t rs = strRhs.find_first_not_of(" \t");
        strRhs = (rs == std::string::npos) ? "" : strRhs.substr(rs);
    }

    static const std::string kKeyword = "FORMAT";
    if (strRhs.size() < kKeyword.size() ||
        strRhs.compare(0, kKeyword.size(), kKeyword) != 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                  LOG_STRING("FORMAT: missing FORMAT keyword in RHS"));
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
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
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
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                  LOG_STRING("FORMAT: input template is empty"));
        return false;
    }
    if (strFormat.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                  LOG_STRING("FORMAT: format template is empty"));
        return false;
    }

    // ── 4.  Validate format template has at least one %N placeholder ───────
    bool bHasPlaceholder = false;
    for (size_t i = 0; i < strFormat.size(); ++i) {
        if (strFormat[i] == '%') {
            if (i + 1 >= strFormat.size()) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                          LOG_STRING("FORMAT: '%' at end of format template has no index"));
                return false;
            }
            const char cIdx = strFormat[i + 1];
            if (!std::isdigit(static_cast<unsigned char>(cIdx))) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                          LOG_STRING("FORMAT: '%" ); 
                          LOG_STRING(std::string(1, cIdx));
                          LOG_STRING("' — index must be a single decimal digit (0-9)"));
                return false;
            }
            bHasPlaceholder = true;
            ++i; // skip the digit
        }
    }
    if (!bHasPlaceholder) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                  LOG_STRING("FORMAT: format template contains no %N placeholder"));
        return false;
    }

    // ── 5.  Name collision with constant macros ────────────────────────────
    if (m_sScriptEntries->mapMacros.count(strName)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                  LOG_STRING("FORMAT ["); 
                  LOG_STRING(strName);
                  LOG_STRING("]: name already used as a constant macro (:=)"));
        return false;
    }

    // ── 6.  Emit IR node ──────────────────────────────────────────────────
    m_sScriptEntries->vCommands.emplace_back(
        ScriptLine{m_iCurrentSourceLine, FormatStatement{strName, strInput, strFormat}});

        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data());
              LOG_STRING("FORMAT ["); LOG_STRING(strName);
              LOG_STRING("] input=["); LOG_STRING(strInput);
              LOG_STRING("] fmt=["); LOG_STRING(strFormat); 
              LOG_STRING("]"));

    return true;

} // m_HandleFormatStmt()


/*-------------------------------------------------------------------------------
  MATH_STMT handler:  name ?= MATH <expression> [| HEX[_<width>][_<endian>]]

  Splits at the first '?=' to extract the destination macro name, strips the
  "MATH" keyword, and stores the remainder verbatim as the expression template.
  $macro substitution and Calculator evaluation are both deferred to execution
  time — the expression may reference variable macros whose values are only
  known at runtime (loop indices, earlier MATH results, plugin outputs, etc.).

  Optional trailing "| HEX..." post-processor:
  - HEX / HEX_8                          → 1-byte zero-padded hex (no endian)
  - HEX_16_LE / HEX_16_BE                → 2-byte zero-padded hex (integer)
  - HEX_32_LE / HEX_32_BE                → 4-byte zero-padded hex (integer)
  - HEX_64_LE / HEX_64_BE                → 8-byte zero-padded hex (integer)
  - HEX_128_LE / HEX_128_BE              → 16-byte zero-padded hex (integer)
  - HEX_FLOAT_LE / HEX_FLOAT_BE          → 4-byte raw IEEE-754 binary32 bit pattern
  - HEX_DOUBLE_LE / HEX_DOUBLE_BE        → 8-byte raw IEEE-754 binary64 bit pattern
  Width determines the number of zero-padded bytes the result is widened to;
  BE/LE controls the byte order of the hexlified output. HEX_8 has no
  endianness (a single byte has none) and rejects a _LE/_BE suffix.
  FLOAT/DOUBLE always require an explicit _LE/_BE suffix, same as every
  integer width above HEX_8, and reinterpret the result's IEEE-754 bit
  pattern directly rather than converting it to a two's-complement integer
  first — see HexOutputFormat in uScriptDataTypes.hpp for why this means
  they have no integer-width "does it fit" overflow/truncation concern.

  Rules enforced at validation time:
  - The destination name must be a valid identifier.
  - The name must not collide with a constant macro (would be permanently
    shadowed at runtime).
  - The expression template must be non-empty after trimming.
  - If a "| HEX..." suffix is present, it must be one of the supported
    width/endian combinations above.

  No arithmetic validation is attempted here.  Syntax errors in the expression
  are reported at execution time via Calculator::evaluate() throwing
  std::runtime_error, which is caught and logged as a command failure.
-------------------------------------------------------------------------------*/

bool ScriptValidator::m_HandleMathStmt( const ScriptRawLine& rawLine ) noexcept
{
    auto lineNr = ustring::fmtLineNr(rawLine.iLineNumber);

    // ── 1. Split at first '?=' ─────────────────────────────────────────────
    static const std::string kAssign = "?=";
    const auto assignPos = rawLine.strContent.find(kAssign);
    if (assignPos == std::string::npos) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                  LOG_STRING("MATH: missing '?='"));
        return false;
    }

    // Extract and trim destination name
    std::string strName = rawLine.strContent.substr(0, assignPos);
    {
        const size_t ns = strName.find_first_not_of(" \t");
        const size_t ne = strName.find_last_not_of(" \t");
        if (ns == std::string::npos) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                      LOG_STRING("MATH: missing destination macro name"));
            return false;
        }
        strName = strName.substr(ns, ne - ns + 1);
    }

    // ── 2. Strip "MATH" keyword from the RHS ──────────────────────────────
    std::string strRhs = rawLine.strContent.substr(assignPos + kAssign.size());
    {
        const size_t rs = strRhs.find_first_not_of(" \t");
        strRhs = (rs == std::string::npos) ? "" : strRhs.substr(rs);
    }

    static const std::string kKeyword = "MATH";
    if (strRhs.size() < kKeyword.size() ||
        strRhs.compare(0, kKeyword.size(), kKeyword) != 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                  LOG_STRING("MATH: missing MATH keyword in RHS"));
        return false;
    }
    strRhs = strRhs.substr(kKeyword.size());
    {
        const size_t rs = strRhs.find_first_not_of(" \t");
        strRhs = (rs == std::string::npos) ? "" : strRhs.substr(rs);
    }

    // ── 3. Expression must be non-empty ───────────────────────────────────
    if (strRhs.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                  LOG_STRING("MATH: expression template is empty"));
        return false;
    }

    // ── 3b. Detect optional | HEX post-processor ──────────────────────────
    // Syntax: name ?= MATH expression | HEX[_<width>][_<endian>]
    // Supported forms:
    //   | HEX, | HEX_8                          (1 byte,  no endianness)
    //   | HEX_16_LE  | HEX_16_BE                 (2 bytes, zero-padded, integer)
    //   | HEX_32_LE  | HEX_32_BE                 (4 bytes, zero-padded, integer)
    //   | HEX_64_LE  | HEX_64_BE                 (8 bytes, zero-padded, integer)
    //   | HEX_128_LE | HEX_128_BE                (16 bytes, zero-padded, integer)
    //   | HEX_FLOAT_LE  | HEX_FLOAT_BE            (4 bytes, raw IEEE-754 binary32)
    //   | HEX_DOUBLE_LE | HEX_DOUBLE_BE           (8 bytes, raw IEEE-754 binary64)
    // Requests that the result be converted to a fixed-width hex string at
    // execution time, in the requested byte order (e.g. with HEX_16_BE:
    // 255 → "00FF"; with HEX_16_LE: 255 → "FF00"). The FLOAT/DOUBLE forms
    // render the result's raw IEEE-754 bit pattern instead of a two's-
    // complement integer conversion — see HexOutputFormat in
    // uScriptDataTypes.hpp for the distinction.
    HexOutputFormat eHexFormat = HexOutputFormat::NONE;
    {
        // Group 1: optional width (8/16/32/64/128/FLOAT/DOUBLE) — defaults to 8 if absent.
        // Group 2: optional endianness (LE/BE) — only valid for width != 8.
        static const std::regex reHexSuffix(
            R"(\|\s*HEX(?:_(8|16|32|64|128|FLOAT|DOUBLE))?(?:_(LE|BE))?\s*$)");

        std::smatch match;
        if (std::regex_search(strRhs, match, reHexSuffix)) {
            const std::string strWidth  = match[1].matched ? match[1].str() : "8";
            const std::string strEndian = match[2].matched ? match[2].str() : "";

            if (strWidth == "8") {
                if (!strEndian.empty()) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                              LOG_STRING("MATH: HEX_8 does not take an endianness suffix (single byte has none)"));
                    return false;
                }
                eHexFormat = HexOutputFormat::HEX_8;
            } else if (strEndian.empty()) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                          LOG_STRING("MATH: HEX_"); LOG_STRING(strWidth);
                          LOG_STRING(" requires an endianness suffix (_LE or _BE)"));
                return false;
            } else {
                const bool bBigEndian = (strEndian == "BE");
                if      (strWidth == "16")     eHexFormat = bBigEndian ? HexOutputFormat::HEX_16_BE     : HexOutputFormat::HEX_16_LE;
                else if (strWidth == "32")     eHexFormat = bBigEndian ? HexOutputFormat::HEX_32_BE     : HexOutputFormat::HEX_32_LE;
                else if (strWidth == "64")     eHexFormat = bBigEndian ? HexOutputFormat::HEX_64_BE     : HexOutputFormat::HEX_64_LE;
                else if (strWidth == "128")    eHexFormat = bBigEndian ? HexOutputFormat::HEX_128_BE    : HexOutputFormat::HEX_128_LE;
                else if (strWidth == "FLOAT")  eHexFormat = bBigEndian ? HexOutputFormat::HEX_FLOAT_BE  : HexOutputFormat::HEX_FLOAT_LE;
                else /* "DOUBLE" */            eHexFormat = bBigEndian ? HexOutputFormat::HEX_DOUBLE_BE : HexOutputFormat::HEX_DOUBLE_LE;
            }

            // Strip the matched suffix and any whitespace left behind.
            strRhs = strRhs.substr(0, match.position(0));
            const size_t ne = strRhs.find_last_not_of(" \t");
            strRhs = (ne == std::string::npos) ? "" : strRhs.substr(0, ne + 1);

            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data());
                      LOG_STRING("MATH: HEX output requested for ["); LOG_STRING(strName);
                      LOG_STRING("] format=["); LOG_STRING(getHexFormatName(eHexFormat)); LOG_STRING("]"));
        }
    }

    // expression must still be non-empty after stripping the suffix
    if (strRhs.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                  LOG_STRING("MATH: expression template is empty after stripping | HEX"));
        return false;
    }

    // ── 4. Constant-macro name collision ──────────────────────────────────
    if (m_sScriptEntries->mapMacros.count(strName)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                  LOG_STRING("MATH ["); LOG_STRING(strName);
                  LOG_STRING("]: name already used as a constant macro (:=)"));
        return false;
    }

    // ── 5. Emit IR node ───────────────────────────────────────────────────
    m_sScriptEntries->vCommands.emplace_back(
        ScriptLine{m_iCurrentSourceLine, MathStatement{strName, strRhs, eHexFormat}});

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data());
              LOG_STRING("MATH ["); LOG_STRING(strName);
              LOG_STRING("] expr=["); LOG_STRING(strRhs);
              LOG_STRING("] hex=["); LOG_STRING(getHexFormatName(eHexFormat)); LOG_STRING("]"));

    return true;

} // m_HandleMathStmt()


/*-------------------------------------------------------------------------------
  BITSTREAM_STMT / BYTESTREAM_STMT handlers:
    name ?= BITSTREAM  offset:length:value ... [| REVERSE_BIT|REVERSE_BYTE]
    name ?= BYTESTREAM byte_offset:length:value ... [| REVERSE_BIT|REVERSE_BYTE]

  Both keywords share one implementation (m_HandleStreamStmt) since the only
  difference between them — bit-granular vs. byte-granular offsets, and
  BITSTREAM fields being allowed to span multiple bytes while BYTESTREAM
  fields cannot — is a runtime packing detail (see StreamStatement's doc
  comment in uScriptDataTypes.hpp and ScriptInterpreter's execution of it),
  not something this validator needs to know about.

  Uses parseStreamStatement() (uStreamStatementParser.hpp) — the same
  structural parser ScriptInterpreter::executeCmd() uses for interactively
  typed BITSTREAM/BYTESTREAM lines — to split the line into a destination
  name and a list of offset/length/value templates. All three of
  offset/length/value are stored verbatim (may contain $macros); parsing
  them as numbers and every numeric/overlap/fit check is deferred to
  execution time, exactly like MathStatement's expression template, since a
  variable ("?=") macro's value is only known once the script is running.

  Rules enforced here (structural, at validation time):
  - The destination name must be a valid identifier.
  - The name must not collide with a constant macro (would be permanently
    shadowed at runtime).
  - The RHS must start with the expected keyword and contain at least one
    well-formed "offset:length:value" field.
  - An optional "| REVERSE_BIT" or "| REVERSE_BYTE" suffix, if present, must
    be exactly one of those two (mutually exclusive by construction — the
    grammar only allows one "| ..." suffix at all).
-------------------------------------------------------------------------------*/

bool ScriptValidator::m_HandleBitstreamStmt( const ScriptRawLine& rawLine ) noexcept
{
    return m_HandleStreamStmt(rawLine, "BITSTREAM", false);
} // m_HandleBitstreamStmt()

bool ScriptValidator::m_HandleBytestreamStmt( const ScriptRawLine& rawLine ) noexcept
{
    return m_HandleStreamStmt(rawLine, "BYTESTREAM", true);
} // m_HandleBytestreamStmt()

bool ScriptValidator::m_HandleStreamStmt( const ScriptRawLine& rawLine, const std::string& strKeyword, bool bByteMode ) noexcept
{
    auto lineNr = ustring::fmtLineNr(rawLine.iLineNumber);

    StreamStatement sStmt;
    std::string     strError;

    if (!parseStreamStatement(strKeyword, rawLine.strContent, sStmt, strError)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data()); LOG_STRING(strError));
        return false;
    }
    sStmt.bByteMode = bByteMode;

    // Name collision with constant macros
    if (m_sScriptEntries->mapMacros.count(sStmt.strName)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                  LOG_STRING(strKeyword); LOG_STRING("[");
                  LOG_STRING(sStmt.strName);
                  LOG_STRING("]: name already used as a constant macro (:=)"));
        return false;
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data());
              LOG_STRING(strKeyword); LOG_STRING("["); LOG_STRING(sStmt.strName);
              LOG_STRING("]:"); LOG_SIZET(sStmt.vFields.size()); LOG_STRING("field(s)"));

    m_sScriptEntries->vCommands.emplace_back(ScriptLine{m_iCurrentSourceLine, std::move(sStmt)});

    return true;

} // m_HandleStreamStmt()



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptValidator::m_HandleCommand ( const ScriptRawLine& rawLine ) noexcept
{
    std::vector<std::string> vstrDelimiters{SCRIPT_PLUGIN_COMMAND_SEPARATOR, SCRIPT_COMMAND_PARAMS_SEPARATOR};
    std::vector<std::string> vstrTokens;
    ustring::tokenizeEx(rawLine.strContent, vstrDelimiters, vstrTokens);

    if (vstrTokens.size() < 2) {
        return false;
    }

    // plugin.command params
    std::string strParams = (vstrTokens.size() == 3) ? vstrTokens[2] : "";
    const bool bThreaded = extractIsThreaded(strParams);

    // Validation: reject a blocking command scheduled without '&'.
    // We check mapBlockingCommands which is populated by generic_getparams
    // from the plugin's command table (bBlocking flag in the X-macro).
    // A blocking command launched sequentially would hang script execution.
    for (const auto& plugin : m_sScriptEntries->vPlugins) {
        if (plugin.strPluginName == vstrTokens[0]) {
            const auto& mapBlocking = plugin.sGetParams.mapBlockingCommands;
            if (!bThreaded && mapBlocking.count(vstrTokens[1])) {
                auto lineNr = ustring::fmtLineNr(rawLine.iLineNumber);
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                    LOG_STRING("Command"); LOG_STRING(vstrTokens[0] + "." + vstrTokens[1]);
                    LOG_STRING("is a blocking command and must be launched with '&'"));
                gui_notify_error_main(rawLine.iLineNumber);
                return false;
            }
            break;
        }
    }

    m_sScriptEntries->vCommands.emplace_back(ScriptLine{m_iCurrentSourceLine,
        Command{vstrTokens[0], vstrTokens[1], strParams, bThreaded}});
    return true;

} // m_HandleCommand()



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptValidator::m_HandleCondition ( const ScriptRawLine& rawLine ) noexcept
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
    if (tokenize(rawLine.strContent, condition, label)) {
        m_sScriptEntries->vCommands.emplace_back(ScriptLine{m_iCurrentSourceLine, Condition{condition, label}});
        return true;
    }

    return false;

} // m_HandleCondition()



/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptValidator::m_HandleLabel ( const ScriptRawLine& rawLine ) noexcept
{
    std::vector<std::string> vstrTokens;
    ustring::tokenize(rawLine.strContent, vstrTokens);

    if (vstrTokens.size() != 2) {
        return false;
    }

    // LABEL label
    m_sScriptEntries->vCommands.emplace_back(ScriptLine{m_iCurrentSourceLine, Label{vstrTokens[1]}});
    return true;

} // m_HandleLabel()


/*-------------------------------------------------------------------------------
  [varname ?=] REPEAT <label> <end>
  [varname ?=] REPEAT <label> <begin>, <end>
  [varname ?=] REPEAT <label> <begin>, <end>, <step>
  [varname ?=] REPEAT <label> UNTIL <condition>

  The optional "varname ?=" prefix names a variable macro that will receive the
  current loop value as a string at the start of every iteration. Without the
  prefix the loop runs exactly as before (strVarMacroName is empty).

  The counted/ranged forms all compile to the same RepeatTimes node — a
  half-open range [begin, end) walked by step — with the single-parameter form
  defaulting to begin=0, step=1 (i.e. identical to the original "repeat N
  times" semantics). Each of begin/end/step may be a literal integer (decimal,
  hex, binary, or octal, any sign), a literal double, or a "$macroname"
  reference deferred to runtime.

  A single handler distinguishes the counted/ranged forms from the conditional
  form by inspecting the token that follows the label:
    UNTIL <condition>  → RepeatUntil (conditional loop)
    otherwise          → RepeatTimes (counted/ranged loop), split on ','

  Structural/nesting validation is deferred to m_validateLoops().
-------------------------------------------------------------------------------*/

bool ScriptValidator::m_HandleRepeat( const ScriptRawLine& rawLine ) noexcept
{
    // Parse the optional capture prefix and the mandatory REPEAT body.
    // Group 1 (optional): varname before "?="
    // Group 2:            loop label
    // Group 3:            remainder — either "<params>" or "UNTIL <cond>"
    static const std::regex pattern(
        R"(^(?:([A-Za-z_][A-Za-z0-9_]*)\s*\?=\s*)?REPEAT\s+([A-Za-z_][A-Za-z0-9_]*)\s+(\S+(?:\s+\S.*)?)$)");
    std::smatch match;

    if (!std::regex_match(rawLine.strContent, match, pattern)) {
        return false;
    }

    const std::string strVarMacroName = match[1].matched ? match[1].str() : "";
    const std::string strLabel        = match[2].str();
    const std::string strRemainder    = match[3].str();   // either "<params>" or "UNTIL <cond>"

    auto lineNr = ustring::fmtLineNr(rawLine.iLineNumber);

    // --- Conditional form: [varname ?=] REPEAT label UNTIL <condition> ---
    static const std::regex untilPattern(R"(^UNTIL\s+(\S.*)$)");
    std::smatch untilMatch;
    if (std::regex_match(strRemainder, untilMatch, untilPattern)) {
        m_sScriptEntries->vCommands.emplace_back(
            ScriptLine{m_iCurrentSourceLine, RepeatUntil{strLabel, untilMatch[1].str(), strVarMacroName}});
        return true;
    }

    // --- Counted / ranged form: end | begin,end | begin,end,step ---
    // m_isRepeat() has already confirmed strRemainder is 1-3 comma-separated
    // well-formed number-or-macro tokens; split and trim them here.
    std::vector<std::string> vstrParams;
    {
        std::stringstream ss(strRemainder);
        std::string strTok;
        while (std::getline(ss, strTok, ',')) {
            const size_t first = strTok.find_first_not_of(" \t");
            const size_t last  = strTok.find_last_not_of(" \t");
            if (first == std::string::npos) {
                vstrParams.clear();
                break;
            }
            vstrParams.push_back(strTok.substr(first, last - first + 1));
        }
    }

    if (vstrParams.empty() || vstrParams.size() > 3) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                  LOG_STRING("REPEAT: expected <end> | <begin,end> | <begin,end,step> or UNTIL <condition> after label:");
                  LOG_STRING(strRemainder));
        return false;
    }

    // Build a RepeatRangeValue for one already-trimmed token: either a deferred
    // "$macroname" reference or a literal resolved to int/double right now.
    auto makeRangeValue = [&](const std::string& strTok, bool& bOk) -> RepeatRangeValue {
        RepeatRangeValue val;
        val.strExpr = strTok;

        if (!strTok.empty() && strTok[0] == '$') {
            val.bIsMacro = true;
            bOk = true;
            return val;
        }

        long long llVal = 0;
        double    dVal  = 0.0;
        bool      bIsInt = true;
        if (!parseRepeatNumber(strTok, bIsInt, llVal, dVal)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                      LOG_STRING("REPEAT: invalid numeric literal:"); LOG_STRING(strTok));
            bOk = false;
            return val;
        }
        val.bIsInteger = bIsInt;
        val.llValue    = llVal;
        val.dValue     = dVal;
        bOk = true;
        return val;
    };

    bool bOk = true;
    RepeatRangeValue rangeBegin{"0", false, true, 0, 0.0};
    RepeatRangeValue rangeEnd;
    RepeatRangeValue rangeStep{"1", false, true, 1, 1.0};

    switch (vstrParams.size()) {
        case 1:
            rangeEnd = makeRangeValue(vstrParams[0], bOk);
            break;
        case 2:
            rangeBegin = makeRangeValue(vstrParams[0], bOk);
            if (bOk) { rangeEnd = makeRangeValue(vstrParams[1], bOk); }
            break;
        default: // 3
            rangeBegin = makeRangeValue(vstrParams[0], bOk);
            if (bOk) { rangeEnd  = makeRangeValue(vstrParams[1], bOk); }
            if (bOk) { rangeStep = makeRangeValue(vstrParams[2], bOk); }
            break;
    }
    if (!bOk) {
        return false;
    }

    // A literal step of exactly zero can never reach <end> — reject now.
    // Deferred ("$macro") steps are re-checked at runtime, once resolved.
    if (!rangeStep.bIsMacro &&
        ((rangeStep.bIsInteger && rangeStep.llValue == 0) ||
         (!rangeStep.bIsInteger && rangeStep.dValue == 0.0))) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                  LOG_STRING("REPEAT: step must not be 0:"); LOG_STRING(strRemainder));
        return false;
    }

    m_sScriptEntries->vCommands.emplace_back(
        ScriptLine{m_iCurrentSourceLine, RepeatTimes{strLabel, rangeBegin, rangeEnd, rangeStep, strVarMacroName}});
    return true;

} // m_HandleRepeat()


/*-------------------------------------------------------------------------------
  END_REPEAT <label>
-------------------------------------------------------------------------------*/

bool ScriptValidator::m_HandleEndRepeat( const ScriptRawLine& rawLine ) noexcept
{
    std::vector<std::string> vstrTokens;
    ustring::tokenize(rawLine.strContent, vstrTokens);

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

bool ScriptValidator::m_HandleBreak( const ScriptRawLine& rawLine ) noexcept
{
    std::vector<std::string> vstrTokens;
    ustring::tokenize(rawLine.strContent, vstrTokens);

    if (vstrTokens.size() != 2) {
        return false;
    }

    m_sScriptEntries->vCommands.emplace_back(ScriptLine{m_iCurrentSourceLine, LoopBreak{vstrTokens[1]}});
    return true;

} // m_HandleBreak()



bool ScriptValidator::m_HandleContinue( const ScriptRawLine& rawLine ) noexcept
{
    std::vector<std::string> vstrTokens;
    ustring::tokenize(rawLine.strContent, vstrTokens);

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

bool ScriptValidator::m_HandlePrint( const ScriptRawLine& rawLine ) noexcept
{
    // Strip the "PRINT" keyword and the single separating space (if present).
    // Everything that remains is the raw text template.
    std::string strText;
    const std::string kKeyword = "PRINT";
    if (rawLine.strContent.size() > kKeyword.size()) {
        // skip keyword + one space
        strText = rawLine.strContent.substr(kKeyword.size() + 1);
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

bool ScriptValidator::m_HandleDelay( const ScriptRawLine& rawLine ) noexcept
{
    // Tokenise: expect exactly ["DELAY", "<value>", "<unit>"]
    std::vector<std::string> vstrTokens;
    ustring::tokenize(rawLine.strContent, vstrTokens);

    auto lineNr = ustring::fmtLineNr(rawLine.iLineNumber);

    if (vstrTokens.size() != 3) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                  LOG_STRING("DELAY: expected 'DELAY <value> <unit>', got");
                  LOG_UINT32(static_cast<uint32_t>(vstrTokens.size())); 
                  LOG_STRING("tokens"));
        return false;
    }

    // Parse value
    size_t szValue = 0;
    try {
        const unsigned long long ullVal = std::stoull(vstrTokens[1]);
        if (ullVal == 0) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                      LOG_STRING("DELAY: value must be >= 1"));
            return false;
        }
        szValue = static_cast<size_t>(ullVal);
    } catch (...) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                  LOG_STRING("DELAY: invalid value:"); 
                  LOG_STRING(vstrTokens[1]));
        return false;
    }

    // Parse unit
    DelayUnit eUnit;
    const std::string& strUnit = vstrTokens[2];
    if      (strUnit == "us")  { eUnit = DelayUnit::US;  }
    else if (strUnit == "ms")  { eUnit = DelayUnit::MS;  }
    else if (strUnit == "sec") { eUnit = DelayUnit::SEC; }
    else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(lineNr.data());
                  LOG_STRING("DELAY: unknown unit '"); LOG_STRING(strUnit);
                  LOG_STRING("' — use us, ms or sec"));
        return false;
    }

    m_sScriptEntries->vCommands.emplace_back(
        ScriptLine{m_iCurrentSourceLine, DelayStatement{szValue, eUnit}});

    // Build a human-readable label for the log
    const std::string strLabel = std::to_string(szValue) + " " + strUnit;
    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data());
              LOG_STRING("DELAY:"); 
              LOG_STRING(strLabel));
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

bool ScriptValidator::m_HandleBreakpoint( const ScriptRawLine& rawLine ) noexcept
{
    // Strip the "BREAKPOINT" keyword; everything after the separating space
    // (if present) is the raw label template.
    std::string strLabel;
    const std::string kKeyword = "BREAKPOINT";
    if (rawLine.strContent.size() > kKeyword.size()) {
        strLabel = rawLine.strContent.substr(kKeyword.size() + 1); // skip keyword + one space
    }

    m_sScriptEntries->vCommands.emplace_back(
        ScriptLine{m_iCurrentSourceLine, BreakpointStatement{strLabel}});

    auto lineNr = ustring::fmtLineNr(rawLine.iLineNumber);
    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data());
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
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(LOG_HEADER_PLUGINS));
        std::for_each(m_sScriptEntries->vPlugins.begin(), m_sScriptEntries->vPlugins.end(), [&](const auto & item) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(item.strPluginName); LOG_STRING(item.strPluginVersRule); LOG_STRING(item.strPluginVersRequested));
        });
    }

    if(false == m_sScriptEntries->mapMacros.empty()) {
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(LOG_HEADER_CMACROS));
        std::for_each(m_sScriptEntries->mapMacros.begin(), m_sScriptEntries->mapMacros.end(), [&](const auto & item) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(item.first); LOG_STRING(":"); LOG_STRING(item.second));

        });
    }

    if(false == m_sScriptEntries->mapArrayMacros.empty()) {
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(LOG_HEADER_ARRAYS));
        std::for_each(m_sScriptEntries->mapArrayMacros.begin(), m_sScriptEntries->mapArrayMacros.end(),
            [&](const auto& item) {
                std::ostringstream oss;
                oss << item.first << " [" << item.second.size() << "] = ";
                for (size_t k = 0; k < item.second.size(); ++k) {
                    if (k > 0) oss << ", ";
                    oss << "[" << k << "]=" << item.second[k];
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(oss.str()));
            });
    }

    if(false == m_sScriptEntries->vCommands.empty()) {
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(LOG_HEADER_COMMANDS));
        std::for_each(m_sScriptEntries->vCommands.begin(), m_sScriptEntries->vCommands.end(), [&](const ScriptLine& data) {
            std::visit([&data](const auto & item) {
                using T = std::decay_t<decltype(item)>;
                auto lineNr = ustring::fmtLineNr(data.iLineNumber);

                if constexpr (std::is_same_v<T, MacroCommand>) {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); LOG_STRING("VMACRO_CMD:"); LOG_STRING(item.strPlugin); LOG_STRING("|"); LOG_STRING(item.strCommand); LOG_STRING("|"); LOG_STRING(item.strParams); LOG_STRING("|"); LOG_STRING(item.strVarMacroName));
                } else if constexpr (std::is_same_v<T, Command>) {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); LOG_STRING("       CMD:"); LOG_STRING(item.strPlugin + "." + item.strCommand); LOG_STRING(item.strParams));
                } else if constexpr (std::is_same_v<T, Condition>) {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); LOG_STRING(" CONDITION:"); LOG_STRING(item.strCondition); LOG_STRING("LBL:"); LOG_STRING(item.strLabelName));
                } else if constexpr (std::is_same_v<T, Label>) {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); LOG_STRING("     LABEL:"); LOG_STRING(item.strLabelName));
                } else if constexpr (std::is_same_v<T, RepeatTimes>) {
                    const std::string strCapture = item.strVarMacroName.empty() ? "" : ("-> $" + item.strVarMacroName);
                    const std::string strRange   = item.begin.strExpr + ".." + item.end.strExpr + " step " + item.step.strExpr;
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); LOG_STRING("  REPEAT_N:"); LOG_STRING(item.strLabel); LOG_STRING(strRange); LOG_STRING(strCapture));
                } else if constexpr (std::is_same_v<T, RepeatUntil>) {
                    const std::string strCapture = item.strVarMacroName.empty() ? "" : ("-> $" + item.strVarMacroName);
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); LOG_STRING("  REPEAT_U:"); LOG_STRING(item.strLabel); LOG_STRING("until ["); LOG_STRING(item.strCondition); LOG_STRING("]"); LOG_STRING(strCapture));
                } else if constexpr (std::is_same_v<T, RepeatEnd>) {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); LOG_STRING("END_REPEAT:"); LOG_STRING(item.strLabel));
                } else if constexpr (std::is_same_v<T, LoopBreak>) {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); LOG_STRING("     BREAK:"); LOG_STRING(item.strLabel));
                } else if constexpr (std::is_same_v<T, LoopContinue>) {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); LOG_STRING("  CONTINUE:"); LOG_STRING(item.strLabel));
                } else if constexpr (std::is_same_v<T, PrintStatement>) {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); LOG_STRING("     PRINT:"); LOG_STRING(item.strText.empty() ? "<none>" : item.strText));
                } else if constexpr (std::is_same_v<T, DelayStatement>) {
                    const std::string strUnit = (item.eUnit == DelayUnit::US)  ? "us"  :(item.eUnit == DelayUnit::MS)  ? "ms"  : "sec";
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); LOG_STRING("     DELAY:"); LOG_STRING(std::to_string(item.szValue)); LOG_STRING(strUnit));
                } else if constexpr (std::is_same_v<T, BreakpointStatement>) {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); LOG_STRING("BREAKPOINT:"); LOG_STRING(item.strLabelTpl.empty() ? "<none>" : item.strLabelTpl));
                } else if constexpr (std::is_same_v<T, VarMacroInit>) {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); LOG_STRING("  VAR_INIT:"); LOG_STRING(item.strName); LOG_STRING("="); LOG_STRING(item.strValueTpl.empty() ? "<none>" : item.strValueTpl));
                } else if constexpr (std::is_same_v<T, FormatStatement>) {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); LOG_STRING("    FORMAT:"); LOG_STRING(item.strName); LOG_STRING("<-["); LOG_STRING(item.strInputTpl); LOG_STRING("]|["); LOG_STRING(item.strFormatTpl); LOG_STRING("]"));
                } else if constexpr (std::is_same_v<T, MathStatement>) {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); LOG_STRING("      MATH:"); LOG_STRING(item.strName); LOG_STRING("= eval["); LOG_STRING(item.strExprTpl); LOG_STRING("]"));
                } else if constexpr (std::is_same_v<T, StreamStatement>) {
                    std::ostringstream oss;
                    for (size_t k = 0; k < item.vFields.size(); ++k) {
                        if (k > 0) oss << " ";
                        oss << item.vFields[k].strOffsetTpl << ":" << item.vFields[k].strLengthTpl << ":" << item.vFields[k].strValueTpl;
                    }
                    const char* pszKind = item.bByteMode ? "BYTESTREAM:" : " BITSTREAM:";
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(lineNr.data()); LOG_STRING(pszKind); LOG_STRING(item.strName); LOG_STRING("= ["); LOG_STRING(oss.str()); LOG_STRING("]"));
                }
            }, data.command);
        });
    }

    return true;

} // m_ListStatements()
