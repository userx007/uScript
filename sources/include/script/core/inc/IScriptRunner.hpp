#ifndef I_SCRIPT_RUNNER_HPP
#define I_SCRIPT_RUNNER_HPP

class IScriptRunner
{
    public:

        virtual bool runScript(const char *pstrCallCtx, bool bRealExec, bool bUseDryRun) = 0;

        virtual ~IScriptRunner() = default;
};

#endif // I_SCRIPT_RUNNER_HPP
