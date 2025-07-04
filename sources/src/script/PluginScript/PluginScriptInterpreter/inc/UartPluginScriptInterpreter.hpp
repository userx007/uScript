#ifndef PLUGINSCRIPTINTERPRETER_HPP
#define PLUGINSCRIPTINTERPRETER_HPP

class UartPluginScriptInterpreter : public IScriptInterpreter
{

    public:

        UartPluginScriptInterpreter (const std::string& strIniPathName)
        {}

        bool interpretScript(ScriptEntriesType& sScriptEntries) override
        {
            return true;
        }
};

#endif // PLUGINSCRIPTINTERPRETER_HPP