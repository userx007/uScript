#ifndef SCRIPTDATATYPES_HPP
#define SCRIPTDATATYPES_HPP

#include <string>
#include <vector>
#include <variant>
#include <unordered_map>

/////////////////////////////////////////////////////////////////////////////////
//                               DATATYPES                                     //
/////////////////////////////////////////////////////////////////////////////////


// forward declaration
struct PluginDataType;

// Tokens type
enum class Token {
    LOAD_PLUGIN,
    CONSTANT_MACRO,
    VARIABLE_MACRO,
    COMMAND,
    IF_GOTO_LABEL,
    LABEL,
    INVALID
};

struct MacroCommand {
    std::string strPlugin;
    std::string strCommand;
    std::string strParams;
    std::string strVarMacroName;
    std::string strVarMacroValue;
};

struct Command {
    std::string strPlugin;
    std::string strCommand;
    std::string strParams;
};

struct Condition {
    std::string strCondition;
    std::string strLabelName;
};

struct Label {
    std::string strLabelName;
};

using ScriptCommandType   = typename std::variant<MacroCommand, Command, Condition, Label>;
using CommandsStorageType = typename std::vector<ScriptCommandType>;
using MacroStorageType    = typename std::unordered_map<std::string, std::string>;
using PluginStorageType   = typename std::vector<PluginDataType>;

struct ScriptEntries {
    PluginStorageType   vPlugins;
    MacroStorageType    mapMacros;
    CommandsStorageType vCommands;
};

using ScriptEntriesType = ScriptEntries;

/////////////////////////////////////////////////////////////////////////////////
//                 DATATYPES LOGGING SUPPORT (type to string)                  //
/////////////////////////////////////////////////////////////////////////////////

inline const std::string& getTokenTypeName(Token type)
{
    switch(type)
    {
        case Token::LOAD_PLUGIN:    { static const std::string name = "LOAD_PLUGIN";    return name; }
        case Token::CONSTANT_MACRO: { static const std::string name = "CONSTANT_MACRO"; return name; }
        case Token::VARIABLE_MACRO: { static const std::string name = "VARIABLE_MACRO"; return name; }
        case Token::COMMAND:        { static const std::string name = "COMMAND";        return name; }
        case Token::IF_GOTO_LABEL:  { static const std::string name = "IF_GOTO_LABEL";  return name; }
        case Token::LABEL:          { static const std::string name = "LABEL";          return name; }
        case Token::INVALID:        { static const std::string name = "INVALID";        return name; }
        default:                    { static const std::string name = "UNKNOWN";        return name; }
    }
}

#endif // SCRIPTDATATYPES_HPP