#ifndef ISCRIPTINTERPRETER_HPP
#define ISCRIPTINTERPRETER_HPP

#include <string>

struct ScriptEntries;

class IScriptInterpreter
{
public:

    virtual bool interpretScript(ScriptEntries& sScriptEntries) = 0;

    virtual ~IScriptInterpreter() = default;

};

#endif // ISCRIPTINTERPRETER_HPP
