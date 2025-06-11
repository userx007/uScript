#ifndef SCRIPTINTERPRETER_HPP
#define SCRIPTINTERPRETER_HPP

#include "IScriptInterpreter.hpp"
#include "IScriptDataTypes.hpp"
#include "IPluginDataTypes.hpp"
#include "uIniParserEx.hpp"

#include <string>

class ScriptInterpreter : public IScriptInterpreter
{

public:

    bool interpretScript(ScriptEntriesType& sScriptEntries) override;

private:

    bool m_executeScript() noexcept;
    bool m_loadPlugins () noexcept;
    bool m_crossCheckCommands() noexcept;
    bool m_initPlugins() noexcept;
    void m_enablePlugins() noexcept;
    void m_replaceVariableMacros(std::string& input);
    bool m_executeCommands() noexcept;
    bool m_retrieveSettings() noexcept;

    ScriptEntriesType *m_sScriptEntries = nullptr;
    std::string m_strSkipUntilLabel;
    IniParserEx m_IniParser;
    bool m_bIniConfigAvailable = true;

};

#endif // SCRIPTINTERPRETER_HPP
