#ifndef U_EXEC_CONTEXT_HPP
#define U_EXEC_CONTEXT_HPP

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

#endif /* U_EXEC_CONTEXT_HPP */
