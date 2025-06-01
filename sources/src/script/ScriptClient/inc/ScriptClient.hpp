#ifndef SCRIPTCLIENT_HPP
#define SCRIPTCLIENT_HPP

#include "ScriptRunner.hpp"
#include "ScriptReader.hpp"
#include "ScriptValidator.hpp"
#include "ScriptInterpreter.hpp"
#include "ItemValidator.hpp"
#include "uTimer.hpp"

#include <string>
#include <memory>


class ScriptClient
{
public:

    ScriptClient(const std::string& strScriptPathName)
        : m_shpScriptRunner(std::make_shared<ScriptRunner>(
                                std::make_shared<ScriptReader>(strScriptPathName),
                                std::make_shared<ScriptValidator>(std::make_shared<ItemValidator>()),
                                std::make_shared<ScriptInterpreter>()))
    {}

    bool execute()
    {
        Timer timer("SCRIPT");
        return m_shpScriptRunner->runScript();
    }

private:

    std::shared_ptr<ScriptRunner> m_shpScriptRunner;

};


#endif // SCRIPTCLIENT_HPP
