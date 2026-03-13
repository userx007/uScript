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
    REPEAT,         // REPEAT <label> <count>  |  REPEAT <label> UNTIL <condition>
    END_REPEAT,     // END_REPEAT <label>
    INVALID
};

struct ScriptRawLine {
    int         iLineNumber = 0;
    std::string strContent;
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

struct RepeatTimes {
    std::string strLabel;
    int         iCount;         // number of iterations (>= 1)
};

struct RepeatUntil {
    std::string strLabel;
    std::string strCondition;   // raw expression (may contain $macros, expanded at run time)
};

struct RepeatEnd {
    std::string strLabel;
};

using ScriptCommandType = std::variant<MacroCommand, Command, Condition, Label,
                                       RepeatTimes, RepeatUntil, RepeatEnd>;

struct ScriptLine {
    int               iSourceLine = 0;
    ScriptCommandType command;
};

using CommandsStorageType = std::vector<ScriptLine>;
using MacroStorageType    = std::unordered_map<std::string, std::string>;
using PluginStorageType   = std::vector<PluginDataType>;

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
        case Token::REPEAT:         { static const std::string name = "REPEAT";         return name; }
        case Token::END_REPEAT:     { static const std::string name = "END_REPEAT";     return name; }
        case Token::INVALID:        { static const std::string name = "INVALID";        return name; }
        default:                    { static const std::string name = "UNKNOWN";        return name; }
    }
}

#endif // SCRIPTDATATYPES_HPP