#ifndef U_SCRIPT_CLIENT_HPP
#define U_SCRIPT_CLIENT_HPP

#include "uScriptCommandValidator.hpp"
#include "uScriptDataTypes.hpp"
#include "uScriptInterpreter.hpp"
#include "uScriptReader.hpp"
#include "uScriptRunner.hpp"
#include "uScriptValidator.hpp"
#include "uTimer.hpp"

#include <filesystem>
#include <memory>
#include <string>

class ScriptClient {
    public:
        explicit ScriptClient(const std::string &strScriptPathName, IniCfgLoader &&loader)
            : m_shpScriptRunner(std::make_shared<ScriptRunner<ScriptEntriesType>>(
                  std::make_shared<ScriptReader>(strScriptPathName),
                  std::make_shared<ScriptValidator>(std::make_shared<ScriptCommandValidator>()),
                  std::make_shared<ScriptInterpreter>(
                      std::move(loader),
                      std::filesystem::absolute(strScriptPathName)
                          .parent_path()
                          .string())))
        {
        }

        bool execute(bool bRealExec)
        {
            static const char *pstrCtx = "CORE script";
            utime::Timer timer(pstrCtx);
            return m_shpScriptRunner->runScript(pstrCtx, bRealExec, true /*bUseDryRun*/);
        }

    private:
        std::shared_ptr<ScriptRunner<ScriptEntriesType>> m_shpScriptRunner;
};

#endif // U_SCRIPT_CLIENT_HPP
