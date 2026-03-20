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

class ScriptInterpreter : public IScriptInterpreterShell<ScriptEntriesType>
{

public:

    explicit ScriptInterpreter(IniCfgLoader&& loader)
                : m_IniCfgLoader(std::move(loader))
                , m_PluginLoader(PluginPathGenerator(SCRIPT_PLUGINS_PATH, PLUGIN_PREFIX, SCRIPT_PLUGIN_EXTENSION),
                                 PluginEntryPointResolver(SCRIPT_PLUGIN_ENTRY_POINT_NAME, SCRIPT_PLUGIN_EXIT_POINT_NAME))
    {
        if (m_IniCfgLoader.loadSection(SCRIPT_INI_SECTION_NAME)) {
            m_IniCfgLoader.getNumFromIni (SCRIPT_INI_CMD_EXEC_DELAY,m_szDelay);
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
        int          iRemaining;        // REPEAT N: iterations left;  REPEAT UNTIL: unused (-1)
        bool         bIsUntil;          // true → REPEAT UNTIL  |  false → REPEAT N
        std::string  strCondition;      // REPEAT UNTIL: raw condition template (may hold $macros)
        std::string  strVarMacroName;   // name of the iteration-index macro ("" = no capture)
        uint64_t     uIterationCount;   // 0-based current iteration index

        // Macros scoped to this loop iteration.  Lifetime == enclosing LoopState.
        std::unordered_map<std::string, std::string> mapLoopMacros;
    };

    bool m_loadPlugin(PluginDataType& command, bool bInitEnable) noexcept;
    bool m_loadPlugins () noexcept;
    bool m_crossCheckCommands() noexcept;
    bool m_initPlugins() noexcept;
    void m_enablePlugins() noexcept;
    void m_replaceVariableMacros(std::string& input);
    bool m_retrieveScriptSettings() noexcept;
    bool m_executeScript() noexcept;

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

    // Loop iteration-index helpers used by RepeatTimes, RepeatUntil, and
    // m_runEndRepeat to keep the optional capture-variable in sync.
    // m_initLoopIterIndex:    writes "0" into the loop's own macro scope on
    //                         first entry.  No-op if strVarMacroName is empty.
    // m_advanceLoopIterIndex: increments the counter and updates the scope
    //                         macro.  No-op if strVarMacroName is empty.
    void m_initLoopIterIndex(LoopState& state) noexcept;
    void m_advanceLoopIterIndex(LoopState& state) noexcept;

    // members initialized in the initialization list
    IniCfgLoader m_IniCfgLoader;
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
