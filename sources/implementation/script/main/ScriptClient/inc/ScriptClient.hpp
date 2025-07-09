#ifndef SCRIPTCLIENT_HPP
#define SCRIPTCLIENT_HPP


#include "ScriptRunner.hpp"
#include "ScriptReader.hpp"
#include "ScriptValidator.hpp"
#include "ScriptInterpreter.hpp"
#include "ScriptItemValidator.hpp"
#include "ScriptDataTypes.hpp"

#include "uTimer.hpp"

#include <string>
#include <memory>


class ScriptClient
{
    public:

        explicit ScriptClient(const std::string& strScriptPathName, const std::string& strIniPathName)
            : m_shpScriptRunner (std::make_shared<ScriptRunner<ScriptEntriesType>>
                                    (
                                        std::make_shared<ScriptReader>(strScriptPathName),
                                        std::make_shared<ScriptValidator>(std::make_shared<ScriptItemValidator>()),
                                        std::make_shared<ScriptInterpreter>(strIniPathName)
                                    )
                                )
        {}

        bool execute()
        {
            utime::Timer timer("MAIN SCRIPT");
            return m_shpScriptRunner->runScript();
        }

    private:

        std::shared_ptr<ScriptRunner<ScriptEntriesType>> m_shpScriptRunner;

};


#endif // SCRIPTCLIENT_HPP
