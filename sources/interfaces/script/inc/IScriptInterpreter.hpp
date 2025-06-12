#ifndef ISCRIPTINTERPRETER_HPP
#define ISCRIPTINTERPRETER_HPP

#include <string>

struct ScriptEntries;

class IScriptInterpreter
{
public:

    virtual bool interpretScript(ScriptEntries& sScriptEntries) = 0;

    virtual bool listItems() = 0;
    virtual bool loadPlugin(const std::string& strPluginName) = 0;
    virtual bool executeCmd(const std::string& strCommand) = 0;

    virtual ~IScriptInterpreter() = default;

};

#endif // ISCRIPTINTERPRETER_HPP
