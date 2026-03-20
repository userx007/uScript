#ifndef U_SCRIPT_CLIENT_HPP
#define U_SCRIPT_CLIENT_HPP


#include "uScriptRunner.hpp"
#include "uScriptReader.hpp"
#include "uScriptValidator.hpp"
#include "uScriptInterpreter.hpp"
#include "uScriptCommandValidator.hpp"
#include "uScriptDataTypes.hpp"

#include "uTimer.hpp"

#include <string>
#include <memory>


class ScriptClient
{
    public:

        explicit ScriptClient(const std::string& strScriptPathName, IniCfgLoader&& loader)
            : m_shpScriptRunner (std::make_shared<ScriptRunner<ScriptEntriesType>> (
                                        std::make_shared<ScriptReader>(strScriptPathName),
                                        std::make_shared<ScriptValidator>(std::make_shared<ScriptCommandValidator>()),
                                        std::make_shared<ScriptInterpreter>(std::move(loader))
                                    )
                                )
        {}

        bool execute(bool bRealExec) {
            static const char *pstrCtx = "CORE script";
            utime::Timer timer(pstrCtx);            
            return m_shpScriptRunner->runScript(pstrCtx, bRealExec);
        }

    private:

        std::shared_ptr<ScriptRunner<ScriptEntriesType>> m_shpScriptRunner;

};


#endif // U_SCRIPT_CLIENT_HPP
