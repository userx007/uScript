#ifndef U_SCRIPT_INTERPRETER_HPP
#define U_SCRIPT_INTERPRETER_HPP

#include "uSharedConfig.hpp"
#include "uScriptDataTypes.hpp"

#include "IScriptInterpreterShell.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"

#include "uIniCfgLoader.hpp"
#include "uPluginLoader.hpp"
#include "uBoolEvaluator.hpp"
#include "uExprEvaluator.hpp"
#include "uNumeric.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <chrono>
#include <mutex>
#include <atomic>
#include <memory>

class ScriptInterpreter : public IScriptInterpreterShell<ScriptEntriesType>
{

public:

    explicit ScriptInterpreter(IniCfgLoader&& loader, std::string strScriptDir = {})
                : m_IniCfgLoader(std::move(loader))
                , m_strScriptDir(std::move(strScriptDir))
                , m_PluginLoader(PluginPathGenerator(
                                    executableDir() + "/" + SCRIPT_PLUGINS_PATH, 
                                    PLUGIN_PREFIX, 
                                    SCRIPT_PLUGIN_EXTENSION),
                                 PluginEntryPointResolver(
                                    SCRIPT_PLUGIN_ENTRY_POINT_NAME, 
                                    SCRIPT_PLUGIN_EXIT_POINT_NAME))
    {
        if (m_IniCfgLoader.loadSection(SCRIPT_INI_SECTION_NAME)) {
            m_IniCfgLoader.getNumFromIni (SCRIPT_INI_CMD_EXEC_DELAY, m_szDelay);
        }
    }

    bool interpretScript(ScriptEntriesType& sScriptEntries, bool bRealExec);
	
    bool listMacrosPlugins();
    bool listCommands();
    bool loadPlugin(const std::string& strPluginName, bool bInitEnable);
    bool executeCmd(const std::string& strCommand);

private:

    // -------------------------------------------------------------------------
    // Reason for the current forward-skip (all three share m_strSkipUntilLabel
    // as the target name; the reason controls which node type clears the skip).
    //
    //   GOTO          — cleared by LABEL  whose strLabelName matches
    //   CONTINUE_LOOP — cleared by END_REPEAT whose strLabel matches;
    //                   normal loop-back logic then runs as usual
    //   BREAK_LOOP    — cleared by END_REPEAT whose strLabel matches;
    //                   the LoopState is popped with no loop-back
    //
    // Intermediate END_REPEAT nodes encountered during CONTINUE/BREAK skip
    // (those belonging to inner loops being unwound) are popped transparently.
    // -------------------------------------------------------------------------
    enum class SkipReason { NONE, GOTO, CONTINUE_LOOP, BREAK_LOOP };

    // -------------------------------------------------------------------------
    // Runtime state for a single active loop.
    //
    // mapLoopMacros holds all variable macros that are scoped to this loop —
    // currently the single iteration-index macro (if strVarMacroName is set).
    // The map is destroyed automatically when this LoopState is popped from
    // m_loopStateStack, giving C-style block scope semantics: a macro declared
    // inside a loop is invisible once the loop exits.  An inner-loop macro with
    // the same name shadows an outer-loop macro for the duration of the inner
    // loop, then the outer value becomes visible again on pop.
    // -------------------------------------------------------------------------
    struct LoopState {
        std::string  strLabel;          // loop label (matches the REPEAT node)
        size_t       szBeginIndex;      // index in vCommands of the REPEAT node
        bool         bIsUntil;          // true → REPEAT UNTIL  |  false → REPEAT range
        std::string  strCondition;      // REPEAT UNTIL: raw condition template (may hold $macros)
        std::string  strVarMacroName;   // name of the iteration-value macro ("" = no capture)
        uint64_t     uIterationCount;   // 0-based iteration counter (logging / REPEAT UNTIL capture)

        // REPEAT range state — meaningful only when bIsUntil == false.
        // The loop walks [begin, end) by step; bRangeIsInteger selects which
        // of the integer/double pairs below holds the live current/end/step.
        bool         bRangeIsInteger;   // true → use ll* fields; false → use d* fields
        long long    llCurrent, llEnd, llStep;
        double       dCurrent,  dEnd,  dStep;

        // Macros scoped to this loop iteration.  Lifetime == enclosing LoopState.
        std::unordered_map<std::string, std::string> mapLoopMacros;

        // -------------------------------------------------------------------
        // DELAY time anchor (see DelayStatement handling in interpretScript()).
        //
        // Set once, when the loop is entered (REPEAT_TIMES/REPEAT_UNTIL push,
        // never reset on loop-back), so it stays fixed for the loop's entire
        // lifetime. Every DELAY statement executed anywhere in this loop's
        // body (including nested non-loop constructs like IF) adds its
        // requested duration to delayAccumulatedUs and then sleeps only
        // until (delayAnchorTime + delayAccumulatedUs) is reached, instead
        // of blindly sleeping the raw requested duration. This is the usual
        // "phase accumulator" pattern for periodic timing: any overhead
        // spent elsewhere in the iteration (plugin I/O, logging, command
        // dispatch, ...) is automatically subtracted from the next DELAY's
        // actual sleep, so the loop's long-run average period matches what
        // the script asked for instead of drifting later every iteration.
        // If the loop is already running behind schedule, the DELAY is
        // skipped entirely (never a negative sleep) — the accumulator is
        // not "given back", so later DELAYs still target the correct
        // absolute time instead of trying to catch up in one jump.
        // -------------------------------------------------------------------
        std::chrono::steady_clock::time_point delayAnchorTime;
        int64_t      llDelayAccumulatedUs = 0;
    };

    // Resolved begin/end/step of a REPEAT range, after macro expansion.
    // bIsInteger selects which of the integer/double triples is authoritative;
    // both are always populated (double mirrors the integer value) so callers
    // may use whichever is convenient without re-checking the flag everywhere.
    struct ResolvedRepeatRange {
        bool      bIsInteger;
        long long llBegin, llEnd, llStep;
        double    dBegin,  dEnd,  dStep;
    };

    bool m_loadPlugin(PluginDataType& command, bool bInitEnable);
    bool m_loadPlugins () noexcept;
    // Scan vCommands for PLUGIN:N references whose base PLUGIN is loaded but
    // the instance is not yet registered; create a fresh entry for each one.
    void m_autoInstantiatePlugins() noexcept;
    bool m_crossCheckCommands() noexcept;
    bool m_initPlugins() noexcept;
    bool m_enablePlugins() noexcept;
    // Expands every $macro reference in-place (regular macros, $NAME.$idx /
    // $NAME.N array element access, $NAME.SIZE array size access).
    // Returns false only when a CONSTANT array index ($NAME.N form) is out
    // of range or otherwise unparsable — a script-authoring error, which is
    // fatal and must stop execution. A variable index ($NAME.$idx) that is
    // out of range is logged but non-fatal: this still returns true, with
    // the reference left unexpanded.
    bool m_replaceVariableMacros(std::string& input);
    bool m_retrieveScriptSettings() noexcept;
    bool m_executeScript() noexcept;

    // Executes a BITSTREAM/BYTESTREAM statement: expands every field's
    // $macros, resolves offset/length/value, packs them into a byte buffer,
    // applies the optional REVERSE_BIT/REVERSE_BYTE post-processor, and
    // returns the hexlified result. Shared by both the BITSTREAM_STMT and
    // BYTESTREAM_STMT cases of the main execution visitor (see
    // StreamStatement::bByteMode) and (indirectly, via that same visitor
    // path through m_dispatchShellLine) by executeCmd()'s interactive form.
    // Returns false and logs a reason on any resolution/range/overlap error.
    bool m_buildStreamStatement(const StreamStatement& command, const std::string& lineNr,
                                 std::string& strResultHex) noexcept;

    // Shared END_REPEAT logic (decrement/condition/loop-back).
    // Called from the normal END_REPEAT path and from the CONTINUE path.
    void m_runEndRepeat(size_t& iIndex, bool& bRetVal) noexcept;

    // Build per-plugin O(1) command-set lookup used by m_crossCheckCommands.
    // Maps plugin name → unordered_set of supported command names.
    void m_buildPluginCommandIndex() noexcept;

    // iIndex is the current position in vCommands; loop constructs may modify it
    // to implement backward jumps.
    bool m_executeCommand(ScriptLine& data, bool bRealExec, size_t& iIndex) noexcept;
    bool m_executeCommands(bool bRealExec) noexcept;
    bool m_pluginIsLoaded(const std::string& strPluginName) noexcept;

    // Unified condition evaluator — handles both plain boolean expressions
    // (delegated to BoolExprEvaluator) and EVAL-prefixed typed comparisons
    // (delegated to EvalExprEvaluator).  Returns true and sets result on
    // success; returns false and logs on any parse / evaluation error.
    bool m_evaluateCondition(const std::string& strCondition, bool& result) noexcept;

    // Shell-command helpers used by executeCmd().
    // m_dispatchShellLine: wraps a variant into a shell-origin ScriptLine
    //   (iLineNumber = 0) and immediately executes it (bRealExec = true).
    // m_mirrorToShellVarMacros: copies a named runtime variable into the
    //   shell-scope map so its value persists across executeCmd() calls.
    bool m_dispatchShellLine(decltype(ScriptLine::command) variant);
    void m_mirrorToShellVarMacros(const std::string& strName);

    // -------------------------------------------------------------------------
    // Thread-safe accessors for m_RuntimeVarMacros.
    //
    // m_RuntimeVarMacros used to be touched only from the main interpreter
    // thread (one write per executed assignment-type token, immediately
    // followed - never concurrently - by reads during macro expansion of
    // later lines). That invariant no longer holds once a threaded
    // variable-capture command (MacroCommand with bThreaded=true, i.e.
    // "VAL ?= PLUGIN.CMD args &") is running: a background jthread now keeps
    // writing VAL's entry in a loop while the main thread concurrently reads
    // (and writes, for its own unrelated macros) the very same
    // unordered_map. Concurrent insert/rehash and lookup on the same
    // unordered_map is undefined behaviour, so every access - reads and
    // writes alike, threaded command or not - goes through these two helpers
    // and m_runtimeVarMutex.
    // -------------------------------------------------------------------------
    void m_setRuntimeVarMacro(const std::string& strName, std::string strValue);
    // Returns {true, value} if found, {false, {}} otherwise.
    std::pair<bool, std::string> m_getRuntimeVarMacro(const std::string& strName);
    mutable std::mutex m_runtimeVarMutex;

    // Loop iteration-index helpers used by RepeatTimes, RepeatUntil, and
    // m_runEndRepeat to keep the optional capture-variable in sync.
    // m_initLoopIterIndex:    writes "0" into the loop's own macro scope on
    //                         first entry.  No-op if strVarMacroName is empty.
    // m_advanceLoopIterIndex: increments the counter and updates the scope
    //                         macro.  No-op if strVarMacroName is empty.
    void m_initLoopIterIndex(LoopState& state) noexcept;
    void m_advanceLoopIterIndex(LoopState& state) noexcept;

    // Resolves a RepeatTimes node's begin/end/step (expanding any deferred
    // "$macroname" bounds) into a concrete integer or double range.
    // Returns false (and logs) if a deferred bound fails to parse as a number
    // or if the resolved step is exactly zero.
    bool m_resolveRepeatRange(const RepeatTimes& rep, ResolvedRepeatRange& out) noexcept;
    
    // plugin loading helper
    std::string executableDir();

    // -------------------------------------------------------------------------
    // Thread management (std::jthread + std::stop_token, C++20)
    //
    // Each launched "&" command gets a ThreadEntry containing:
    //   thread  — the jthread itself (auto-joins and requests stop on destruction)
    //   done    — atomic flag set true by the thread lambda just before returning;
    //             used by m_harvestFinishedThreads() to prune completed entries
    //             without blocking.
    //
    // m_busyPlugins tracks which plugin instances currently have a live thread.
    // Attempting to launch a second thread for the same instance is rejected so
    // that plugin code does not need to be re-entrant.
    // -------------------------------------------------------------------------
    struct ThreadEntry {
        std::jthread                       thread;
        std::shared_ptr<std::atomic<bool>> done;
    };

    // Remove entries whose "done" flag is true (thread has already returned).
    // Called before each new thread launch to keep the vector compact.
    // Must be called with m_threadsMutex held.
    void m_harvestFinishedThreads() noexcept;

    // Signal stop on all active threads, then join each one with a configurable
    // timeout.  Threads that honour stop_token will exit promptly; others are
    // still joined (join() blocks until they return naturally).
    // Called automatically at the end of interpretScript() after the last
    // script command has executed.
    void m_joinAllThreads() noexcept;

    std::vector<ThreadEntry>         m_threads;
    std::mutex                       m_threadsMutex;

    // Plugin instances that currently have an active "&" thread.
    // Key: strPluginName.  Protected by m_threadsMutex.
    std::unordered_set<std::string>  m_busyPlugins;

    // members initialized in the initialization list
    IniCfgLoader m_IniCfgLoader;
    std::string  m_strScriptDir;   // directory of the main script — default for ARTEFACTS_PATH
    BoolExprEvaluator m_beEvaluator;
    EvalExprEvaluator m_evalExprEvaluator;
    PluginLoaderFunctor<PluginInterface> m_PluginLoader;

    // members (internals)
    bool m_bIniConfigAvailable = true;
    size_t m_szDelay = 0U;
    ScriptEntriesType *m_sScriptEntries = nullptr;
    std::string m_strSkipUntilLabel;
    SkipReason  m_eSkipReason = SkipReason::NONE;

    // Runtime loop-state stack implemented as a vector so that
    // m_replaceVariableMacros can walk it from innermost to outermost scope.
    // back() == top of stack; push_back/pop_back maintain LIFO order.
    std::vector<LoopState> m_loopStateStack;

    // Runtime variable macro values: populated as each MacroCommand dispatches
    // successfully or when a VarMacroInit node executes.
    // Keyed by strVarMacroName; value is the string returned by getData().
    // Using a dedicated map rather than a field inside the IR structs gives
    // correct last-EXECUTED semantics: when the same macro name appears multiple
    // times in the script the map always holds the value most recently written
    // at runtime.
    std::unordered_map<std::string, std::string> m_RuntimeVarMacros;

    // Per-plugin command set index: plugin name → set of supported command names.
    // Built once in m_crossCheckCommands for O(1) membership tests.
    std::unordered_map<std::string,
                       std::unordered_set<std::string>> m_pluginCmdIndex;

    // Variable macros created by the shell (executeCmd / shell plugin).
    // These have script-wide lifetime, distinct from loop-scoped macros above.
    std::unordered_map<std::string, std::string> m_ShellVarMacros;

    // Persistent variable map shared across all MATH statements in the script.
    // Allows intra-expression assignments (e.g. MATH x = 5 + 3) to be visible
    // in subsequent MATH evaluations as plain identifiers.
    // Calculator built-in constants (pi, e, tau, phi, inf, nan) are seeded on
    // first use by Calculator's constructor via try_emplace.
    std::unordered_map<std::string, double> m_mathVars;
};

#endif // U_SCRIPT_INTERPRETER_HPP
