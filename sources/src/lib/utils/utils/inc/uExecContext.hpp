#ifndef U_EXEC_CONTEXT_HPP
#define U_EXEC_CONTEXT_HPP

#include <string>
#include <filesystem>
#include <stop_token>
#include <thread>
#include <chrono>
#include <atomic>

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
//
// That per-line poll is not enough on its own, though: it only ever runs
// between commands, so a thread parked inside a single blocking driver call
// (the "very long timeout" case named above — including now-legitimate
// infinite timeouts) never reaches it. setStopFlagFilePath() therefore also
// starts a small watcher thread that polls the flag file independently and,
// the moment it appears, calls request_stop() on a stop_source shared via
// getStopToken(). That token is what actually reaches
// ICommDriver::tout_read()/tout_write() (see PluginInterface::doDispatch() ->
// ucmdexec::generic_cmd() -> CommScriptCommandInterpreter), so a wedged
// blocking read/write can be woken up directly instead of only being reaped
// by the GUI's hard-kill fallback.
/////////////////////////////////////////////////////////////////////////////////

namespace uexec {

namespace detail {
    inline std::string t_strStopFlagPath;

    // Backs getStopToken(). Requested from the watcher thread below the
    // moment the flag file appears — independent of whatever the
    // interpreter's own script-execution loop happens to be doing, which is
    // the whole point: a thread wedged inside a single blocking driver call
    // (e.g. a READ with an infinite timeout) never returns to its own
    // per-line isStopRequested() poll on its own, so something outside that
    // loop has to notice and flip this token instead.
    inline std::stop_source t_stopSource;

    // Started once by setStopFlagFilePath() (a no-op if never called, i.e.
    // running standalone from the CLI without the GUI). Polls the flag file
    // on its own cadence and calls request_stop() the instant it appears,
    // then exits — a std::jthread so it's automatically joined at process
    // teardown via its own destructor.
    inline std::jthread t_watcherThread;
}

/**
 * \brief Called once at startup with the path from the SCRIPT_STOP_FLAG_FILE
 *        environment variable (see uScriptMainApp.cpp). An empty path (e.g.
 *        running the interpreter standalone, without the GUI) disables the
 *        check: isStopRequested() then always returns false, and no watcher
 *        thread is started (getStopToken() still returns a valid but
 *        never-requested token, so callers need no special-casing either way).
 */
inline void setStopFlagFilePath(const std::string& strPath)
{
    detail::t_strStopFlagPath = strPath;

    if (strPath.empty()) {
        return;
    }

    detail::t_watcherThread = std::jthread([](std::stop_token selfTok) {
        // 150ms: fast enough that a STOP press feels immediate, cheap enough
        // (a single stat() per tick) not to matter for the process lifetime
        // of a script run.
        while (!selfTok.stop_requested()) {
            std::error_code ec;
            if (std::filesystem::exists(detail::t_strStopFlagPath, ec) && !ec) {
                detail::t_stopSource.request_stop();
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
    });
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
    return detail::t_stopSource.stop_requested();
}

/**
 * \brief Cooperative cancellation token for the whole script run, requested
 *        the moment the stop-flag file appears (see setStopFlagFilePath()'s
 *        watcher thread) rather than only being noticed at the next
 *        per-line poll. Pass this into PluginInterface::doDispatch() (and
 *        anywhere else that ultimately reaches a blocking driver call) so a
 *        thread parked inside ICommDriver::tout_read()/tout_write() can be
 *        woken up promptly instead of only after its own timeout elapses.
 *        Running standalone (no SCRIPT_STOP_FLAG_FILE set) returns a valid
 *        token whose stop_requested() is simply always false.
 */
inline std::stop_token getStopToken(void)
{
    return detail::t_stopSource.get_token();
}

} // namespace uexec

#endif /* U_EXEC_CONTEXT_HPP */
