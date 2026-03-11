# Shell Plugin

A C++ shared-library plugin that launches an interactive **Microshell** session from within the host script framework. The session gives the operator direct, terminal-level access to the script interpreter's loaded plugins, declared macros, and command set — without stopping or restarting the host process. The shell blocks until the operator exits, then the script resumes normally.

**Version:** 1.0.0.0

---

## Table of Contents

1. [Overview](#overview)
2. [Project Structure](#project-structure)
3. [Architecture](#architecture)
   - [Plugin Lifecycle](#plugin-lifecycle)
   - [Command Dispatch Model](#command-dispatch-model)
   - [The uShell Layer](#the-ushell-layer)
   - [Connection to the Script Interpreter](#connection-to-the-script-interpreter)
4. [Building](#building)
5. [Command Reference](#command-reference)
   - [INFO](#info)
   - [RUN](#run)
6. [Interactive Shell Reference](#interactive-shell-reference)
   - [Built-in Commands](#built-in-commands)
   - [The `.` Shortcut — Script Execution Bridge](#the--shortcut--script-execution-bridge)
   - [The `..` Form — Loading Script Plugins](#the--form--loading-script-plugins)
   - [Macro Declaration via `.`](#macro-declaration-via-)
   - [Summary of `.` Sub-forms](#summary-of--sub-forms)
   - [The `/` Shortcut](#the--shortcut)
7. [Shell Features](#shell-features)
   - [Autocomplete](#autocomplete)
   - [Command History](#command-history)
   - [Edit Mode](#edit-mode)
   - [Smart Prompt](#smart-prompt)
   - [Colors](#colors)
8. [Writing Shell Plugins (uShell Plugin Extension Model)](#writing-shell-plugins-ushell-plugin-extension-model)
   - [Command Definition](#command-definition)
   - [Parameter Type System](#parameter-type-system)
   - [Shortcut Handlers](#shortcut-handlers)
9. [Fault-Tolerant and Privileged Modes](#fault-tolerant-and-privileged-modes)
10. [Error Handling and Return Values](#error-handling-and-return-values)

---

## Overview

The plugin loads as a dynamic shared library (`.so` / `.dll`). The host application calls the exported C entry points `pluginEntry()` / `pluginExit()` to create and destroy the plugin object. During `doInit()`, the plugin stores the host's opaque `pvUserData` pointer — a reference to the live `IScriptInterpreterShell` instance. When `SHELL.RUN` is called, this pointer is passed into the Microshell engine, giving the interactive session full access to the script runtime that is already running.

All commands follow the pattern:

```
<PLUGIN>.<COMMAND> [arguments]
```

For example:

```
SHELL.RUN
```

Inside the running shell, the operator uses native shell commands (`list`, `pload`) and the `.` shortcut to reach the script layer.

---

## Project Structure

```
shell_plugin/
├── CMakeLists.txt                          # Top-level build
├── shell_plugin/
│   ├── CMakeLists.txt
│   ├── inc/
│   │   └── shell_plugin.hpp                # Plugin class definition
│   └── src/
│       └── shell_plugin.cpp                # Entry points, RUN and INFO handlers
└── uShell/
    ├── ushell_core/
    │   ├── ushell_core/
    │   │   ├── inc/ushell_core.h           # Microshell class — Run(), getShellSharedPtr()
    │   │   └── src/ushell_core.cpp         # Core engine: parsing, dispatch, terminal I/O
    │   ├── ushell_core_config/
    │   │   ├── inc/ushell_core_settings.h  # Feature flags and compile-time config
    │   │   ├── inc/ushell_core_datatypes.h # Core structs: uShellInst_s, command_s, shortcut_s
    │   │   └── inc/ushell_core_datatypes.cfg / ushell_core_prompt.cfg
    │   ├── ushell_core_terminal/           # Platform terminal RAII (Linux / Windows)
    │   └── ushell_core_utils/              # hexlify / unhexlify utilities
    ├── ushell_settings/
    │   └── inc/ushell_core_settings.h      # Master feature-flag header
    └── ushell_user/
        ├── ushell_user_root/               # Root shell instance (built-in commands + shortcuts)
        │   ├── inc/ushell_root_commands.cfg
        │   ├── inc/ushell_root_shortcuts.cfg
        │   └── src/ushell_root_usercode.cpp / ushell_root_interface.cpp
        ├── ushell_user_plugins/            # Loadable shell plugin instances
        │   ├── template_plugin/            # Copy-paste starting point for new plugins
        │   └── test_plugin/                # Example with all parameter types demonstrated
        └── ushell_user_utils/
            └── ushell_logger/              # Shell-side logging header
```

---

## Architecture

### Plugin Lifecycle

```
pluginEntry()           → creates ShellPlugin instance
  setParams()           → loads INI values (none specific to this plugin)
  doInit(pvUserData)    → stores pvUserData (IScriptInterpreterShell*) for later use
  doEnable()            → enables real execution (without this, RUN validates args only)
  doDispatch("RUN", "") → calls m_Shell_RUN → launches blocking Microshell session
  doCleanup()           → marks plugin as uninitialized and disabled
pluginExit(ptr)         → deletes the ShellPlugin instance
```

The critical step is `doInit(pvUserData)`: the host framework passes in its live script interpreter pointer, which the plugin stores as `m_pvUserData`. This pointer is forwarded to `uShellPluginEntry(m_pvUserData)` when the shell starts, making the entire script runtime (loaded plugins, macros, commands) accessible from inside the interactive session.

### Command Dispatch Model

Commands are registered via a single-level `std::map` (`m_mapCmds`) populated in the constructor through an X-macro expansion:

```cpp
#define SHELL_PLUGIN_COMMANDS_CONFIG_TABLE    \
SHELL_PLUGIN_CMD_RECORD( INFO               ) \
SHELL_PLUGIN_CMD_RECORD( RUN                )
```

### The uShell Layer

The interactive session is powered by **Microshell** (`ushell_core`), a feature-rich embedded shell engine. It is configured at compile time via `ushell_core_settings.h`. The settings enabled for this build are:

| Feature | Setting | Status |
|---|---|---|
| Multiple shell instances (nested plugins) | `uSHELL_SUPPORTS_MULTIPLE_INSTANCES` | ✓ enabled |
| External user data (script bridge) | `uSHELL_SUPPORTS_EXTERNAL_USER_DATA` | ✓ enabled |
| Command history | `uSHELL_IMPLEMENTS_HISTORY` | ✓ enabled |
| Persistent history (file) | `uSHELL_IMPLEMENTS_SAVE_HISTORY` | ✓ enabled |
| Tab autocomplete | `uSHELL_IMPLEMENTS_AUTOCOMPLETE` | ✓ enabled |
| Inline edit mode | `uSHELL_IMPLEMENTS_EDITMODE` | ✓ enabled |
| Smart prompt (status indicators) | `uSHELL_IMPLEMENTS_SMART_PROMPT` | ✓ enabled |
| Command help system | `uSHELL_IMPLEMENTS_COMMAND_HELP` | ✓ enabled |
| User shortcut keys | `uSHELL_IMPLEMENTS_USER_SHORTCUTS` | ✓ enabled |
| ANSI color output | `uSHELL_SUPPORTS_COLORS` | ✓ enabled |
| Shell exit (`#q`) | `uSHELL_IMPLEMENTS_SHELL_EXIT` | ✓ enabled |

The `Microshell::getShellSharedPtr()` factory creates a new shell instance bound to a `uShellInst_s` structure (function table, shortcut table, prompt, dispatcher). Each nested `pload` call creates its own instance in a separate `Microshell` object, making plugin sessions fully re-entrant.

A `TerminalRAII` guard is instantiated before `pShellPtr->Run()` — it configures the terminal for raw key input on entry and restores the original terminal settings on exit, regardless of how the session ends.

### Connection to the Script Interpreter

The `pvUserData` stored during `doInit()` is a pointer to the host's `IScriptInterpreterShell<ScriptEntriesType>` instance. The root shell's user code casts it back through `pvLocalUserData` and calls three methods on it:

| Shell action | Script interpreter method called |
|---|---|
| `.l` — list macros and plugins | `pScript->listMacrosPlugins()` |
| `.c` — list script commands | `pScript->listCommands()` |
| `.. <name>` — load a script plugin | `pScript->loadPlugin(name, true)` |
| `. <command>` — execute a command | `pScript->executeCmd(command)` |

This bridge is what makes the shell a live window into the running script session — not a separate interpreter, but a direct caller into the same command dispatcher used by the script engine itself.

---

## Building

The plugin is built as a CMake shared library. The `uShell` subtree is compiled as part of the same build. Required external dependencies are `uSharedConfig`, `uIPlugin`, `uPluginOps`, `uPluginLoader`, and `IScriptInterpreterShell`.

```bash
mkdir build && cd build
cmake ..
make shell_plugin
```

The output is `libshell_plugin.so` (Linux) or `shell_plugin.dll` (Windows).

---

## Command Reference

### INFO

Prints version information and a usage summary to the logger. Takes **no arguments** and works even if `doInit()` was not yet called.

```
SHELL.INFO
```

**Example output:**
```
SHELL      | Vers: 1.0.0.0
SHELL      | Description: launch an interactive shell session
SHELL      |
SHELL      | RUN : start an interactive shell session (blocks until the user exits)
SHELL      |   Usage: SHELL.RUN
```

---

### RUN

Launches the interactive Microshell session. The call **blocks** until the user exits the shell (with `#q`). The host script resumes from the next line after `SHELL.RUN` returns.

```
SHELL.RUN
```

This command takes no arguments. On entry, the terminal is switched to raw mode. On exit, the terminal is restored to its previous state.

---

## Interactive Shell Reference

Once `SHELL.RUN` is active, the operator types commands at the prompt. The prompt format reflects the current state of autocomplete, history, and edit mode (smart prompt).

### Built-in Commands

These commands are always available in the root shell:

| Command | Arguments | Description |
|---|---|---|
| `list` | — | Lists all available script plugins and shell plugins found in their respective directories |
| `pload <name>` | plugin name | Loads the named **shell plugin** (from `SHELL_PLUGINS_PATH`) and opens a nested interactive session for it. The nested session blocks until the user exits with `#q`, then returns to the parent session |

**Example:**
```
root> list
root> pload my_device_plugin
my_device_plugin> ...
my_device_plugin> #q
root>
```

### The `.` Shortcut — Script Execution Bridge

The `.` key is registered as a **user shortcut** in the root shell. When a line begins with `.`, the text that follows is intercepted before the normal command parser and routed through the script interpreter bridge. This is the primary mechanism for reaching the full script runtime from within the shell.

**General form:**
```
root> .<args>
```

The shortcut handler inspects `<args>` and dispatches to one of several sub-forms:

### The `..` Form — Loading Script Plugins

When the first character after `.` is another `.`, the remainder (after optional whitespace) is treated as a **script plugin name** to load via `IScriptInterpreterShell::loadPlugin()`.

```
root> .. <plugin_name>
```

This loads the named plugin into the **script interpreter** — the same plugin registry used by the host script. After loading, the plugin's commands become available to subsequent `. <command>` calls within the same shell session.

```
root> .. UART
[..] loading plugin [UART]

root> . UART.CONFIG p:/dev/ttyUSB0 b:115200
[.] executing [UART.CONFIG p:/dev/ttyUSB0 b:115200]

root> . UART.CMD > "AT\r\n" | "OK"
[.] executing [UART.CMD > "AT\r\n" | "OK"]
```

### Macro Declaration via `.`

The `.` shortcut also handles **macro declaration**, matching the same syntax used by the script engine:

**Constant macro** (`:=` assignment): the macro name is uppercased automatically up to the `:` character.

```
root> . my_port := /dev/ttyUSB0
→ declares: MY_PORT := /dev/ttyUSB0
```

**Volatile macro** (`?=` assignment, value captured from a command's return data): the macro name is uppercased up to the `.` before the command name.

```
root> . detected_port ?= UARTMON.WAIT_INSERT 5000
→ declares: DETECTED_PORT ?= UARTMON.WAIT_INSERT 5000
```

For all other `.` forms, the argument string is uppercased up to the first space (preserving arguments in their original case), then passed to `IScriptInterpreterShell::executeCmd()`.

### Summary of `.` Sub-forms

| Input | Action |
|---|---|
| `.h` | Print the shortcut help summary |
| `.l` | List all script macros and loaded plugins (`listMacrosPlugins()`) |
| `.c` | List all script commands (`listCommands()`) |
| `.. <name>` | Load a script plugin by name (`loadPlugin(name, true)`) |
| `. <name> := <value>` | Declare a constant macro in the script engine |
| `. <name> ?= <CMD.subcmd>` | Declare a volatile macro (value from command return data) |
| `. <CMD.subcmd> [args]` | Execute any script command directly (`executeCmd(command)`) |

**Quick reference example session:**
```
root> .h
        [.h] help
        [.l] list script macros and plugins
        [.c] list script commands
        [.arg] execute the command provided as argument
        [..arg] load the plugin provided as argument

root> .l
  [macros]   MY_PORT := /dev/ttyUSB0
  [plugins]  UART, UARTMON

root> .c
  UART.CONFIG, UART.CMD, UART.SCRIPT, UART.INFO
  UARTMON.START, UARTMON.STOP, ...

root> . UART.INFO
[.] executing [UART.INFO]
...

root> #q
```

### The `/` Shortcut

The `/` shortcut is registered but not implemented in the root shell. It logs a warning and returns. It is available as a hook for future extension.

---

## Shell Features

### Autocomplete

Tab-completion is active by default (`uSHELL_INIT_AUTOCOMPL_MODE = true`). Pressing `Tab` once completes a unique prefix; pressing `Tab` again when multiple matches exist cycles through them. The autocomplete engine maintains an index array over the command table and reloads on demand (`uSHELL_AUTOCOMPL_RELOAD = true`).

### Command History

The shell maintains an in-memory circular history buffer of 256 bytes. On platforms that support file I/O (Linux, MinGW, MSVC), history is persisted to a file named after the prompt (enabling separate history files per plugin). Arrow-up / arrow-down navigate the history. The history is initialized on (`uSHELL_INIT_HISTORY_MODE = true`).

Parameter limits per command:

| Type | Token | Max params |
|---|---|---|
| 64-bit unsigned | `l` | 1 |
| 32-bit unsigned | `i` | 5 |
| String (`char*`) | `s` | 5 |
| Boolean | `o` | 1 |

### Edit Mode

The shell supports in-line cursor movement and editing (insert, delete, backspace, Ctrl+Home/End shortcuts). Edit mode is active by default when autocomplete and history are both disabled; when those features are active, pressing `Insert` toggles edit mode.

### Smart Prompt

The prompt uses ANSI color codes (bright cyan) and encodes the active state of autocomplete (`A`), history (`H`), and edit mode (`E`) as indicator characters. The prompt string is set to `"root"` for the main session and to the loaded plugin's name for nested `pload` sessions.

### Colors

All log levels map to distinct ANSI color codes:

| Level | Color |
|---|---|
| ERROR | Bright Red |
| WARNING | Magenta |
| INFO | Bright White |
| VERBOSE | Yellow |
| DEBUG | Bright Blue |
| SUCCESS | Bright Green |
| Prompt | Bright Cyan |

Colors are compiled out when `uSHELL_SUPPORTS_COLORS = 0`.

---

## Writing Shell Plugins (uShell Plugin Extension Model)

The `ushell_user_plugins/template_plugin/` directory provides a ready-to-copy scaffold for adding new interactive commands to a nested shell session loaded via `pload`. A shell plugin differs from a script plugin: it exposes C functions that the Microshell engine calls directly by parsing typed arguments from the command line.

### Command Definition

Commands are declared in a `.cfg` file using the `uSHELL_COMMAND` macro:

```c
uSHELL_COMMAND( <function_name>, <param_pattern>, "<help string>" )
```

The `<param_pattern>` label corresponds to a `uSHELL_COMMAND_PARAMS_PATTERN` block that defines the actual C function signature via a typedef. The X-macro system generates all prototypes, function-pointer union members, and the dispatch enum automatically.

**Example (`ushell_plugin_commands.cfg`):**
```c
uSHELL_COMMAND_PARAMS_PATTERN(v)
#define v_params  void
uSHELL_COMMAND(vtest,   v,   "void test function")

uSHELL_COMMAND_PARAMS_PATTERN(i)
#define i_params  num32_t
uSHELL_COMMAND(itest,   i,   "i test function")

uSHELL_COMMAND_PARAMS_PATTERN(is)
#define is_params num32_t, str_t*
uSHELL_COMMAND(istest,  is,  "is test function")
```

The corresponding C function implementations go in `ushell_plugin_usercode.cpp`:

```cpp
int vtest(void)         { /* ... */ return 0; }
int itest(uint32_t i)   { /* ... */ return 0; }
int istest(uint32_t i, char *s) { /* ... */ return 0; }
```

### Parameter Type System

The shell parses typed arguments from the raw input string before calling the function. Supported types (enabled per `ushell_core_settings.h`):

| Type token | C type | Description |
|---|---|---|
| `v` | `void` | No parameters |
| `i` | `num32_t` (uint32_t) | 32-bit unsigned integer |
| `l` | `num64_t` (uint64_t) | 64-bit unsigned integer |
| `s` | `str_t*` (char*) | String (quoted strings with spaces supported) |
| `o` | `bool` | Boolean |

Patterns can be combined: `ii`, `is`, `ss`, `lio`, etc. Up to 5 strings and 5 integers can be passed per command (configurable via `uSHELL_MAX_PARAMS_*`).

### Shortcut Handlers

Each plugin can register its own `.` and `/` shortcut handlers in `ushell_plugin_shortcuts.cfg`. The handlers are C functions named `uShellUserHandleShortcut_Dot` and `uShellUserHandleShortcut_Slash`. In the `template_plugin` these are stubs; in the root shell they are fully implemented to provide the script interpreter bridge described above.

To add a new shortcut symbol, add an entry to the shortcuts config and implement the handler:

```c
// in ushell_plugin_shortcuts.cfg:
uSHELL_USER_SHORTCUT('@', At, "\t@ : my custom shortcut\n\r")

// in ushell_plugin_usercode.cpp:
void uShellUserHandleShortcut_At(const char *pstrArgs) {
    // custom logic
}
```

---

## Fault-Tolerant and Privileged Modes

- **Fault-tolerant mode** (`setFaultTolerant()` / `isFaultTolerant()`): when set, the host framework continues executing subsequent commands even if `SHELL.RUN` returns `false`. In practice this is unlikely since `RUN` always returns `true` after the session ends; the flag is most relevant if initialization failed.
- **Privileged mode** (`isPrivileged()`): always returns `false`. Reserved for future use in the plugin framework.

---

## Error Handling and Return Values

Every command handler returns `bool`:
- `true` — command executed successfully, or argument validation passed in disabled (dry-run) mode.
- `false` — unexpected arguments were passed to `INFO` or `RUN`.

`RUN` returns `true` after a normal session exit (user typed `#q`). If `getShellSharedPtr()` returns `nullptr` (e.g., the shell instance could not be created), `RUN` still returns `true` after the failed attempt — the session simply does not start. Errors during the session itself are handled internally by the Microshell engine and logged via `uSHELL_LOG`.
