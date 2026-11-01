#ifndef U_SCRIPT_CLIENT_HPP
#define U_SCRIPT_CLIENT_HPP

#include "uScriptCommandValidator.hpp"
#include "uScriptDataTypes.hpp"
#include "uScriptInterpreter.hpp"
#include "uScriptReader.hpp"
#include "uScriptRunner.hpp"
#include "uLogger.hpp"
#include "uScriptValidator.hpp"
#include "uSharedScriptExt.hpp"
#include "uTimer.hpp"

#include <filesystem>
#include <memory>
#include <string>

class ScriptClient
{
    public:
        explicit ScriptClient(const std::string &strScriptPathName, IniCfgLoader &&loader)
            : m_strScriptPathName(strScriptPathName)
            , m_shpScriptRunner(std::make_shared<ScriptRunner<ScriptEntriesType>>(
                  std::make_shared<ScriptReader>(strScriptPathName),
                  std::make_shared<ScriptValidator>(std::make_shared<ScriptCommandValidator>()),
                  std::make_shared<ScriptInterpreter>(
                      std::move(loader),
                      std::filesystem::absolute(strScriptPathName)
                          .parent_path()
                          .string())))
        {
        }

        /** Dry run (@p bRealExec false): reads and validates the script without device I/O. Real run: executes it. */
        bool execute(bool bRealExec)
        {
            // Guard for every embedder (the app checks too): only .main scripts are executed.
            if (!scriptext::isMainScript(m_strScriptPathName)) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("CORE_SCRIPT_C|");
                          LOG_STRING("The script must have the " SCRIPT_EXT_MAIN " extension:");
                          LOG_STRING(m_strScriptPathName));
                return false;
            }

            static const char *pstrCtx = "CORE script";
            utime::Timer timer(pstrCtx);
            return m_shpScriptRunner->runScript(pstrCtx, bRealExec, true /*bUseDryRun*/);
        }

    private:
        std::string m_strScriptPathName; // declared first: initialised before the runner
        std::shared_ptr<ScriptRunner<ScriptEntriesType>> m_shpScriptRunner;
};

#endif // U_SCRIPT_CLIENT_HPP
