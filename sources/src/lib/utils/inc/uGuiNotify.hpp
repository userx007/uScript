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
 *   GUI:LOAD_COMM:<path>        load file <path> into w2
 *   GUI:CLEAR_COMM              clear w2 (comm script finished)
 *   GUI:LOG:<message>           append <message> to w3 (no ANSI codes)
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

#endif // U_GUI_NOTIFY_HPP
