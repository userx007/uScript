# uScript — Scripting & Automation Framework

## Overview

**uScript** is a C++ scripting and hardware-automation framework built around two complementary script interpreters and a plugin ecosystem that abstracts real hardware interfaces. It is designed to drive embedded-systems testing, hardware bring-up, and protocol-level automation with a clean, readable scripting syntax and a strongly layered, interface-driven architecture.

The framework ships as a standalone executable plus a set of independently loadable shared-library plugins (`.so` / `.dll`). Scripts are plain text files processed at runtime with no pre-compilation step.

![Deployment](documentation/deployment.png)

---

## Script syntax reference table

| Syntax Element | Usage |
|---|---|
| `# comment` | Line comment — discards everything from `#` to end of line |
| `---` … `!--` | Block comment — discards all lines between the delimiters; nesting not supported |
| `LOAD_PLUGIN <NAME>` | Loads a plugin by name; maps to a shared library on disk |
| `LOAD_PLUGIN <NAME> <op> v<major>.<minor>.<patch>.<build>` | Loads a plugin with a version constraint (`<`, `<=`, `>`, `>=`, `==`) |
| `NAME := <value>` | Constant macro — defined once at validation time; referenced as `$NAME`; cannot be reassigned |
| `NAME [= <elem0>, <elem1>, …` | Array macro — declares an ordered list of strings; elements accessed at runtime via `$NAME.$index` |
| `NAME [= <elem0>, \` *(continued on next line)* | Multi-line array declaration — trailing `\` joins the next line; line number recorded at first physical line |
| `"elem, with comma"` | Array element quoting — required when an element contains a comma; quotes are stripped from the stored value |
| `$ARRAY.$indexmacro` | Array element access — `$indexmacro` is resolved to an integer, then used as the array subscript |
| `name ?= PLUGIN.COMMAND [params]` | Variable macro (plugin form) — captures a plugin command's return value at execution time |
| `name ?= <value>` | Variable macro (direct init) — assigns a literal string at execution time; `$macros` in the value are expanded when the line executes |
| `PLUGIN.COMMAND [params]` | Plugin command — `PLUGIN` and `COMMAND` must be fully upper-case; `$macros` in params are expanded before dispatch; failure aborts the script |
| `PRINT [text]` | Prints text to the log at INFO level; `$macros` expanded at execution time; bare `PRINT` outputs a blank line |
| `DELAY <value> <unit>` | Pauses execution; `<value>` must be a positive integer ≥ 1; `<unit>` is `us` (microseconds), `ms` (milliseconds), or `sec` (seconds) |
| `name ?= FORMAT <input> \| <pattern>` | Tokenises `<input>` by whitespace and substitutes `%0`, `%1`, `%2`, … placeholders in `<pattern>`; result stored in `name` |
| `name ?= MATH <expression>` | Evaluates a floating-point arithmetic expression; stores result as string in `name`; supports operators, functions, and built-in constants |
| `BREAKPOINT [label]` | Suspends execution and waits for a keypress; pressing `a`/`A` prompts for abort, any other key continues |
| `GOTO <label>` | Unconditional forward jump to the named `LABEL` |
| `IF <condition> GOTO <label>` | Conditional forward jump; condition may be a plain boolean expression or an `EVAL` expression |
| `LABEL <label>` | Jump target for `GOTO` or `IF … GOTO`; must follow its corresponding `GOTO` in the file |
| `EVAL <lhs> <op>[:<TYPE>] <rhs>` | Typed scalar comparison; returns `"TRUE"` or `"FALSE"`; used in `?=` assignment, `IF … GOTO`, and `REPEAT … UNTIL` |
| `:<TYPE>` type hint | Suffix on an `EVAL` operator to specify comparison type: `:STR` (string), `:NUM` (float), `:VER` (version), `:BOOL` (boolean) |
| `&&` / `\|\|` in EVAL | Compound EVAL logic; `&&` binds tighter than `\|\|`; short-circuit evaluation applied |
| `REPEAT <label> <N>` … `END_REPEAT <label>` | Counted loop — executes body exactly `N` times; `N` may be a literal or `$macro` |
| `REPEAT <label> UNTIL <condition>` … `END_REPEAT <label>` | Conditional loop (do-while) — body executes at least once; condition evaluated after each iteration |
| `name ?= REPEAT <label> …` | Loop index capture — `$name` holds the 0-based iteration index; scoped to the loop body only |
| `BREAK <label>` | Exits the named enclosing loop immediately; execution resumes after its `END_REPEAT` |
| `CONTINUE <label>` | Skips the rest of the current loop body and resumes at the `END_REPEAT` of the named loop |
| `$name` | Macro reference — resolved at runtime in priority order: loop index → script-level `?=` variable → shell macro |

---

## Communication scripts

Some of the plugins used for communication purposes (e.g., those supporting UART, SPI, or I²C communication) can run their own scripts to enable fast interaction with the device.
These scripts are simpler than the main uscript scripts and are optimized for basic communication operations such as:

- send
- receive
- send/receive
- receive/send
- delays between commands

This allows efficient low-level communication sequences when interacting with hardware devices.<br>
[Communication scripts description](sources/src/script/comm/README.md)<br>

---

## SHELL plugin

A special plugin called SHELL allows the main script to enter an interactive command-line interface where the user can manually:

- load plugins
- call plugin commands (which may also trigger communication scripts if the plugin supports them)
- execute commands implemented directly in the shell
- load shell-specific plugins that provide specialized command sets<br>
[Shell plugin description](sources/src/plugin/shell_plugin//README.md)<br>

Because of this architecture, the system is highly flexible and can be adapted to a wide range of testing and hardware interaction scenarios.

---

## Create New Plugins

Creating a new plugin is very simple. Just execute the script `sources/src/plugin/create_plugin.sh`
with one argument: the name of the plugin to create.

```bash
./create_plugin.sh YOUR_PLUGIN_NAME
```

After this, the new plugin can be built.
However, to install it, you need to manually copy the following line into the main `CMakeLists.txt`:

```cmake
install(TARGETS YOUR_PLUGIN_NAME_plugin ${LIB_INSTALL_TYPE} DESTINATION ${INSTALL_PLUGIN_DIR})
```

---

## Build Process

If the required tools are installed:

* C++ compiler for Linux builds
* MinGW for Windows builds on Linux (`sudo apt update` followed by `sudo apt install mingw-w64)`)

simply run:

```bash
# Build Linux application and plugins
./linux_build.sh

# Build Windows application and plugins
./windows_build.sh
```

Alternatively, Visual Studio can be used to build Windows applications on Windows OS.

---

## Full documentation

[General description](documentation/GENERAL_DESCRIPTION.md)<br>
[Scripting Language Reference](documentation/SCRIPTING_LANGUAGE_REFERENCE.md)<br>
[Scripting Language Tutorial](documentation/SCRIPTING_LANGUAGE_TUTORIAL.md)<br>
[Math Comand Reference Manual](documentation/MATH_COMMAND_REFERENCE.md)<br>


### Plugins documentation

[BUSPIRATE](sources/src/plugin/buspirate_plugin/docs/README.md)<br>
[CH347](sources/src/plugin/ch347_plugin/docs/README.md)<br>
[CP2112](sources/src/plugin/cp2112_plugin/docs/README.md)<br>
[FTDI2232](sources/src/plugin/ftdi2232_plugin/docs/README.md)<br>
[FTDI232H](sources/src/plugin/ftdi232h_plugin/docs/README.md)<br>
[FTDI245](sources/src/plugin/ftdi245_plugin/docs/README.md)<br>
[FTDI4232](sources/src/plugin/ftdi4232_plugin/docs/README.md)<br>
[HYDRABUS](sources/src/plugin/hydrabus_plugin/docs/README.md)<br>
[SHELL](sources/src/plugin/shell_plugin/docs/README.md)<br>
[UARTMON](sources/src/plugin/uartmon_plugin/docs/README.md)<br>
[UART](sources/src/plugin/uart_plugin/docs/README.md)<br>

