#ifndef U_GUI_NOTIFY_HPP
#define U_GUI_NOTIFY_HPP

/**
 * @file uGuiNotify.hpp
 * @brief GUI mode notification layer.
 *
 * When the interpreter is launched from the Qt front-end (--gui flag or
 * SCRIPT_GUI_MODE env var), g_gui_mode is set to true in main() before any
 * interpreter code runs.  All notify functions below check this flag first,
 * so the entire mechanism is zero-cost in normal CLI use.
 *
 * Protocol (one line per event, LF-terminated, written directly to stdout):
 *
 *   GUI:EXEC_MAIN:<lineNo>      highlight line <lineNo> in w1 (1-based)
 *   GUI:EXEC_COMM:<lineNo>      highlight line <lineNo> in w2
 *   GUI:ERROR_MAIN:<lineNo>     mark validation-error line <lineNo> in w1
 *   GUI:ERROR_COMM:<lineNo>     mark validation-error line <lineNo> in w2
 *   GUI:LOAD_COMM:<path>        load file <path> into w2
 *   GUI:CLEAR_COMM              clear w2 (comm script finished)
 *   GUI:LOG:<message>           append <message> to w3 (no ANSI codes)
 *   GUI:SHELL_RUN               open shell terminal panel (w4), enter terminal mode
 *   GUI:SHELL_EXIT              close shell terminal panel, resume main script
 *
 * Shell session handshake (SHELL.RUN plugin command):
 *
 *   The plugin emits GUI:SHELL_RUN before calling Microshell::Run(), then
 *   blocks.  When the user exits the shell, the plugin emits GUI:SHELL_EXIT
 *   and waits for the single token "SHELL_DONE\n" on its own stdin before
 *   returning.  The GUI writes SHELL_DONE after collapsing the terminal panel,
 *   so the main script only resumes once the UI is back in its normal state.
 *
 * Call obligations (ALL four structural events are required for correct GUI
 * behaviour — omitting any one breaks the execution bar):
 *
 *   CommScriptClient::execute(), real-exec path only:
 *     1. gui_notify_load_comm(scriptPath)   ← BEFORE runScript()
 *     2. [runScript() calls CommScriptCommandInterpreter::interpretCommand()
 *        which calls gui_notify_exec_comm() for every command line]
 *     3. gui_notify_clear_comm()            ← AFTER  runScript()
 *
 *   ScriptInterpreter::m_executeCommand():
 *     gui_notify_exec_main(lineNo)          ← for every main-script line
 *
 * stdout MUST be unbuffered when in GUI mode — main() calls
 *   setbuf(stdout, nullptr)
 * before launching the ScriptClient, so every printf/fflush pair here
 * reaches the QProcess pipe immediately.
 */

#include <cstdio>
#include <cstdlib>
#include <string>

// ---------------------------------------------------------------------------
// Global mode flag
// Written once in main() (before interpreter threads start), read-only after.
// Declared inline (C++17) so every TU that includes this header shares the
// same instance without needing a separate .cpp definition.
//
// IMPORTANT — DSO visibility:
//   This inline variable is per-DSO.  Code compiled into the main executable
//   (e.g. ScriptInterpreter) sees the instance that main() sets to true.
//   Code instantiated inside a plugin shared library (.so/.dll) gets its own
//   copy which main() never touches, so it stays false.
//   Use gui_mode_active() instead of reading this flag directly; that helper
//   falls back to the SCRIPT_GUI_MODE environment variable so it works
//   correctly from any DSO.
// ---------------------------------------------------------------------------
inline bool g_gui_mode = false;  /**< true  → GUI front-end mode (structured stdout)
                                      false → normal CLI mode (no change to behaviour) */

// ---------------------------------------------------------------------------
// Cross-DSO GUI mode query
//
// Checks g_gui_mode first (zero-cost for the main executable where it is set
// correctly by main()).  If it is false — which happens when the caller lives
// in a plugin shared library that has its own copy of g_gui_mode — falls back
// to the SCRIPT_GUI_MODE environment variable, which is process-wide and is
// therefore visible from every DSO.
//
// The env-var result is cached in a function-local static so the getenv()
// call is made at most once per DSO, on the first notification after the
// plugin is loaded (always after main() has set the variable).
// ---------------------------------------------------------------------------
inline bool gui_mode_active() noexcept
{
    if (g_gui_mode) {
        return true;
    }
    static const bool s_from_env = (std::getenv("SCRIPT_GUI_MODE") != nullptr);
    return s_from_env;
}

// ---------------------------------------------------------------------------
// Notify: main-script line executing (→ w1 highlight)
// Called from ScriptInterpreter::m_executeCommand() for every script line.
// ---------------------------------------------------------------------------
inline void gui_notify_exec_main(int lineNo) noexcept
{
    if (!gui_mode_active()) {
        return;
    }
    std::printf("\nGUI:EXEC_MAIN:%d\n", lineNo);
    std::fflush(stdout);
}

// ---------------------------------------------------------------------------
// Notify: comm-script line executing (→ w2 highlight)
// Called from CommScriptInterpreter::interpretScript() for every comm-script
// line, but only during real execution (bRealExec == true).
// ---------------------------------------------------------------------------
inline void gui_notify_exec_comm(int lineNo) noexcept
{
    if (!gui_mode_active()) {
        return;
    }
    std::printf("\nGUI:EXEC_COMM:%d\n", lineNo);
    std::fflush(stdout);
}

// ---------------------------------------------------------------------------
// Notify: comm-script about to start (→ load file into w2)
// Called from CommScriptClient::execute() before runScript(), real exec only.
// ---------------------------------------------------------------------------
inline void gui_notify_load_comm(const std::string& path) noexcept
{
    if (!gui_mode_active()) {
        return;
    }
    std::printf("\nGUI:LOAD_COMM:%s\n", path.c_str());
    std::fflush(stdout);
}

// ---------------------------------------------------------------------------
// Notify: comm-script finished (→ clear w2)
// Called from CommScriptClient::execute() after runScript(), real exec only.
// ---------------------------------------------------------------------------
inline void gui_notify_clear_comm() noexcept
{
    if (!gui_mode_active()) {
        return;
    }
    std::printf("\nGUI:CLEAR_COMM\n");
    std::fflush(stdout);
}


// ---------------------------------------------------------------------------
// Notify: interactive shell session starting (→ open terminal panel w4)
//
// Call this BEFORE Microshell::Run().  The GUI will expand the terminal
// panel (m_w4), activate key routing from the front-end to the interpreter's
// stdin, and switch onProcessOutput() into terminal mode so that raw shell
// output goes to w4 instead of being parsed as GUI: protocol lines.
// ---------------------------------------------------------------------------
inline void gui_notify_shell_run() noexcept
{
    if (!gui_mode_active()) {
        return;
    }
    std::printf("\nGUI:SHELL_RUN\n");
    std::fflush(stdout);
}

// ---------------------------------------------------------------------------
// Notify: interactive shell session ended (→ collapse terminal panel w4)
//
// Call this AFTER Microshell::Run() returns (i.e. after the user has typed
// the shell exit command).  After emitting this token the caller MUST NOT
// touch stdout until this function returns — it blocks on stdin waiting for
// the single acknowledgement token "SHELL_DONE\n" from the GUI.
// This guarantees the GUI has finished collapsing w4 and stopped routing key
// bytes before the main script resumes its next command.
//
// Typical usage in the plugin (the only required call pattern):
//
//   gui_notify_shell_run();
//   pShellPtr->Run();          // blocks until user types the exit command
//   gui_notify_shell_exit();   // signals GUI then waits for ack → returns
//   // main script continues here — GUI is already in normal dispatch mode
// ---------------------------------------------------------------------------
inline void gui_notify_shell_exit() noexcept
{
    if (!gui_mode_active()) {
        return;
    }
    std::printf("\nGUI:SHELL_EXIT\n");
    std::fflush(stdout);

    // Block until the GUI acknowledges.  The Qt side writes exactly one
    // "SHELL_DONE\n" on our stdin after collapsing the terminal panel and
    // reverting onProcessOutput() to normal protocol-dispatch mode.
    char buf[64];
    while (std::fgets(buf, sizeof(buf), stdin)) {
        if (std::strncmp(buf, "SHELL_DONE", 10) == 0)
            break;
    }
}

// ---------------------------------------------------------------------------
// Notify: validation error on a core-script line (→ w1 error highlight)
// Call once per failing line during the dry-run validation phase.
// Multiple calls are allowed (one per distinct error line).
// ---------------------------------------------------------------------------
inline void gui_notify_error_main(int lineNo) noexcept
{
    if (!gui_mode_active()) {
        return;
    }
    std::printf("\nGUI:ERROR_MAIN:%d\n", lineNo);
    std::fflush(stdout);
}

// ---------------------------------------------------------------------------
// Notify: validation error on a comm-script line (→ w2 error highlight)
// Call once per failing line during the dry-run validation phase.
// Multiple calls are allowed (one per distinct error line).
// ---------------------------------------------------------------------------
inline void gui_notify_error_comm(int lineNo) noexcept
{
    if (!gui_mode_active()) {
        return;
    }
    std::printf("\nGUI:ERROR_COMM:%d\n", lineNo);
    std::fflush(stdout);
}

#endif // U_GUI_NOTIFY_HPP
