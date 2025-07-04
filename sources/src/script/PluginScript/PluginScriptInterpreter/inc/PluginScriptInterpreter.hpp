#ifndef PLUGINSCRIPTINTERPRETER_HPP
#define PLUGINSCRIPTINTERPRETER_HPP

#include "IScriptInterpreter.hpp"

class PluginScriptInterpreter : public IScriptInterpreter
{

    public:

        explicit PluginScriptInterpreter (const std::string& strIniPathName)
        {}

        bool interpretScript(ScriptEntriesType& sScriptEntries, PFSEND pfsend, PFWAIT pfwait ) override
        {
            return true;
        }
};

#endif // PLUGINSCRIPTINTERPRETER_HPP