
#include "uScriptValidator.hpp"
#include "uScriptDataTypes.hpp"
#include "IPluginDataTypes.hpp"

#include "uEvaluator.hpp"
#include "uString.hpp"
#include "uLogger.hpp"

#include <string>
#include <vector>
#include <set>
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

#define LT_HDR     "S_VALIDATOR:"
#define LOG_HDR    LOG_STRING(LT_HDR)


/////////////////////////////////////////////////////////////////////////////////
//                            CLASS IMPLEMENTATION                             //
/////////////////////////////////////////////////////////////////////////////////

/*-------------------------------------------------------------------------------

-------------------------------------------------------------------------------*/

bool ScriptValidator::validateScript(std::vector<std::string>& vstrScriptLines, ScriptEntriesType& sScriptEntries)
{

    bool bRetVal = false;

    m_sScriptEntries = &sScriptEntries;

    do {

        if (false == m_validateScriptStatements(vstrScriptLines)) {
            break;
        }

        if (false == m_validateConditions()) {
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

bool ScriptValidator::m_validateScriptStatements(std::vector<std::string>& vstrScriptLines) noexcept
{
    Token token;

    return std::all_of(vstrScriptLines.begin(), vstrScriptLines.end(),
        [&](std::string& item) {
            ustring::replaceMacros(item, m_sScriptEntries->mapMacros, SCRIPT_MACRO_MARKER);
            if (!m_shpCommandValidator->validateCommand(item, token)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to validate ["); LOG_STRING(item); LOG_STRING("]"));
                return false;
            }
            return m_preprocessScriptStatements(item, token);
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
        }, command);

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

-------------------------------------------------------------------------------*/

bool ScriptValidator::m_validatePlugins () noexcept
{
    bool bRetVal = true;

    // list of plugins used by the commands (store them in a std::set because only one occurence is relevant)
    std::set<std::string> usedPlugins;
    std::for_each(m_sScriptEntries->vCommands.begin(), m_sScriptEntries->vCommands.end(),
        [&usedPlugins](const ScriptCommandType & data) {
            std::visit([&usedPlugins](const auto & item) {
                using T = std::decay_t<decltype(item)>;

                if constexpr (std::is_same_v<T, MacroCommand>) {
                    usedPlugins.insert(item.strPlugin);
                }

                if constexpr (std::is_same_v<T, Command>) {
                    usedPlugins.insert(item.strPlugin);
                }
            }, data);
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
        printSet(notusedPlugins, "Unused plugins", true /*bError*/);
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

    // vmacroname ?= plugin.command params              |  plugin     |    command    |            params                           |vmacroname   | vmacroval |
    m_sScriptEntries->vCommands.emplace_back(MacroCommand{vstrTokens[1], vstrTokens[2], (vstrTokens.size() == 4) ? vstrTokens[3] : "", vstrTokens[0], ""});
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

    // plugin.command params                      |   plugin    |   command    |             params                           |
    m_sScriptEntries->vCommands.emplace_back(Command{vstrTokens[0], vstrTokens[1], (vstrTokens.size() == 3) ? vstrTokens[2] : ""});
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
        m_sScriptEntries->vCommands.emplace_back(Condition{condition, label});
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

    // LABEL label                        | strLabelName       |
    m_sScriptEntries->vCommands.emplace_back(Label{vstrTokens[1]});
    return true;

} // m_HandleLabel()



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
        std::for_each(m_sScriptEntries->vCommands.begin(), m_sScriptEntries->vCommands.end(), [&](const ScriptCommandType & data) {
            std::visit([](const auto & item) {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<T, MacroCommand>) {
                    LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("    MCMD:"); LOG_STRING(item.strPlugin); LOG_STRING("|"); LOG_STRING(item.strCommand); LOG_STRING("|"); LOG_STRING(item.strParams); LOG_STRING("|"); LOG_STRING(item.strVarMacroName); LOG_STRING("-> ["); LOG_STRING(item.strVarMacroValue); LOG_STRING("]"));
                } else if constexpr (std::is_same_v<T, Command>) {
                    LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("     CMD:"); LOG_STRING(item.strPlugin); LOG_STRING("|"); LOG_STRING(item.strCommand); LOG_STRING("|"); LOG_STRING(item.strParams));
                } else if constexpr (std::is_same_v<T, Condition>) {
                    LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("    COND:"); LOG_STRING(item.strCondition); LOG_STRING("LBL:"); LOG_STRING(item.strLabelName));
                } else if constexpr (std::is_same_v<T, Label>) {
                    LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("     LBL:"); LOG_STRING(item.strLabelName));
                }
            }, data);
        });
    }

    return true;

} // m_ListPlugins()