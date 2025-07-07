#ifndef SCRIPTDATATYPES_HPP
#define SCRIPTDATATYPES_HPP

#include <string>
#include <vector>
#include <variant>
#include <unordered_map>


// forward declaration
struct PluginDataType;


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

#endif // SCRIPTDATATYPES_HPP