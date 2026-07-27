#ifndef U_EXEC_CONTEXT_HPP
#define U_EXEC_CONTEXT_HPP

#include <string>
#include <filesystem>

/////////////////////////////////////////////////////////////////////////////////
//                                  RATIONALE                                  //
/////////////////////////////////////////////////////////////////////////////////
//
// ScriptInterpreter::interpretScript(sScriptEntries, bRealExec) has a dry-run
// pass (bRealExec == false) that walks every command purely to validate it -
// it must not perform any real hardware I/O.  For plugin commands it still
// calls PluginInterface::doDispatch() (see uScriptInterpreter.cpp, the
// "only for validation purposes" branch of m_executeCommand), so that
// argument/grammar validation (e.g. CommScriptCommandValidator::
// validateCommand() for a *_CMD command) still runs during dry-run.
//
// The problem: PluginInterface::doDispatch()'s signature - and every plugin
// command handler's signature behind it, via PluginOperations.hpp's
// MFP<T> = bool (T::*)(const std::string&, std::stop_token) const and the
// PluginCommandsMap it is stored in - is fixed and shared by every command
// of every plugin. There is no parameter slot to carry "this call is a
// dry-run" from the core interpreter down into, say, a *_CMD handler's call
// to ucmdexec::generic_cmd() and from there into
// CommScriptCommandInterpreter::interpretCommand(). Adding one would mean
// changing that shared function-pointer type and every single plugin command
// handler in the codebase, not just the CMD ones this feature cares about.
//
// This thread-local flag is the narrow, non-invasive alternative: the core
// interpreter sets it for the duration of its one validation-pass
// doDispatch() call (see DryRunScope below); generic_cmd() reads it via
// isDryRun() and passes the correct value into interpretCommand()'s
// (previously always-true, effectively dead) bRealExec parameter, which now
// actually stops one step short of the real send/receive interface when
// dry-running - see CommScriptCommandInterpreter::interpretCommand().
//
// Being thread_local means it needs no locking and is automatically correct
// for background command threads too: a freshly created std::jthread gets
// its own thread-local storage, defaulting to false, regardless of what the
// main thread's flag is doing - so real (non-dry-run) execution, sequential
// or threaded, is unaffected by this mechanism entirely.
/////////////////////////////////////////////////////////////////////////////////

namespace uexec {

namespace detail {
    inline thread_local bool t_bDryRun = false;
}

/**
 * \brief Returns true if the calling thread is currently inside a
 *        dry-run/validation-only dispatch (see DryRunScope).
 *
 *        A plugin command that performs real I/O should check this and, if
 *        true, stop just short of the actual send/receive interface: it may
 *        still validate argument syntax, and may even open/configure the
 *        underlying driver, but must not transmit or wait to receive real
 *        bytes.
 */
inline bool isDryRun(void)
{
    return detail::t_bDryRun;
}

/**
 * \brief RAII scope guard: sets the current thread's dry-run flag for its
 *        lifetime and restores the previous value on destruction (including
 *        on early return or exception unwind from the guarded call), so
 *        nested use is always safe.
 */
class DryRunScope
{
public:
    explicit DryRunScope(bool bDryRun)
        : m_bPrev(detail::t_bDryRun)
    {
        detail::t_bDryRun = bDryRun;
    }

    ~DryRunScope()
    {
        detail::t_bDryRun = m_bPrev;
    }

    DryRunScope(const DryRunScope&) = delete;
    DryRunScope& operator=(const DryRunScope&) = delete;

private:
    bool m_bPrev;
};

} // namespace uexec

/////////////////////////////////////////////////////////////////////////////////
//                                  RATIONALE                                  //
/////////////////////////////////////////////////////////////////////////////////
//
// Pressing "Stop" in the Qt front-end used to go straight to QProcess::kill()
// (SIGKILL) on the child process running the interpreter — an unconditional,
// external, all-or-nothing stop with no chance for the interpreter to notice
// and wind down on its own: no clean log message, no request_stop() on
// background command threads (see uScriptInterpreter.cpp's threaded dispatch),
// nothing.
//
// This is the graceful alternative the GUI now tries FIRST (see MainWindow::
// terminateProcess()): before killing, it creates a small marker file whose
// path was handed to the child at launch via the SCRIPT_STOP_FLAG_FILE
// environment variable (see uScriptMainApp.cpp), then gives the interpreter a
// short grace period to notice and exit on its own before still falling back
// to kill() as an unconditional safety net (e.g. if the interpreter is wedged
// in a single blocking driver call with a very long timeout).
//
// A plain file (checked with a cheap std::filesystem::exists() stat(), not a
// dedicated stdin/IPC channel) was chosen deliberately: it works identically
// on every supported OS, needs no signal handler (avoiding async-signal-
// safety concerns entirely) and, importantly, cannot collide with the
// existing terminal-passthrough ("#q" shell-exit) traffic that already flows
// over the child's actual stdin/stdout.
//
// The interpreter polls isStopRequested() once per top-level pass of its
// script-execution loop (ScriptInterpreter::m_executeCommands) - which, since
// REPEAT/END_REPEAT are implemented as index-jumps within that very same loop
// rather than a separate nested loop, means every REPEAT iteration is covered
// by that one check with no extra plumbing.
/////////////////////////////////////////////////////////////////////////////////

namespace uexec {

namespace detail {
    inline std::string t_strStopFlagPath;
}

/**
 * \brief Called once at startup with the path from the SCRIPT_STOP_FLAG_FILE
 *        environment variable (see uScriptMainApp.cpp). An empty path (e.g.
 *        running the interpreter standalone, without the GUI) disables the
 *        check: isStopRequested() then always returns false.
 */
inline void setStopFlagFilePath(const std::string& strPath)
{
    detail::t_strStopFlagPath = strPath;
}

/**
 * \brief Cheap (stat()-based) check for whether a graceful stop has been
 *        requested. Meant to be polled once per top-level script-loop
 *        iteration.
 */
inline bool isStopRequested(void)
{
    if (detail::t_strStopFlagPath.empty()) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::exists(detail::t_strStopFlagPath, ec) && !ec;
}

} // namespace uexec

#endif /* U_EXEC_CONTEXT_HPP */
