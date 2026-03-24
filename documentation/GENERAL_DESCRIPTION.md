# uScript — Scripting & Automation Framework

## Overview

**uScript** is a C++ scripting and hardware-automation framework built around two complementary script interpreters and a plugin ecosystem that abstracts real hardware interfaces. It is designed to drive embedded-systems testing, hardware bring-up, and protocol-level automation with a clean, readable scripting syntax and a strongly layered, interface-driven architecture.

The framework ships as a standalone executable plus a set of independently loadable shared-library plugins (`.so` / `.dll`). Scripts are plain text files processed at runtime with no pre-compilation step.




```
╔═════════════════════════════════════════════════════════════════════════════════╗
║                          uScriptMainApp                                         ║
║              (entry point: CLI args, INI load, launch)                          ║
╚══════════╤═══════════════════════════╤═════════════════════════════════╤════════╝
           │                           │                                 │
           ▼                           ▼                                 ▼
  ┌─────────────────┐       ┌────────────────────┐            ┌──────────────────┐
  │   INI Config    │       │   Script Client    │            │     Utils        │
  │  uIniCfgLoader  │──────▶│  (orchestrator)    │            │  Logger, Timer,  │
  │  uIniParser     │       └────────┬───────────┘            │  ArgParser,      │
  │  uSharedConfig  │                │                        │  File, String... │
  └─────────────────┘                │                        └──────────────────┘
                          ┌──────────┴───────────────────────┐
                          │           Script Core            │
                          │  ┌──────────┐  ┌─────────────┐   │
                          │  │  Reader  │  │  Validator  │   │
                          │  └──────────┘  └─────────────┘   │
                          │         ┌─────────────┐          │
                          │         │ Interpreter │          │
                          │         └──────┬──────┘          │
                          │                │                 │
                          │  ┌─────────────┴─────────────┐   │
                    ┌────────│      Comm Scripts         │   │
                    │     │  │  (comm-specific runner,   │   │
                    │     │  │   validator, interpreter) │   │
                    │     │  └───────────────────────────┘   │
                    │     └──────────────┬───────────────────┘
                    │                    │  loads via PluginLoader
                    │     ╔══════════════╧═══════════════════════╗
                    │     ║         Plugin Interface             ║
                    │     ║            (IPlugin)                 ║
                    │     ╚══════╤══════════════════════════╤════╝
                    │            │                          │
              ┌─────│────────────┴────────┐   ┌─────────────┴────────────────┐
              │     │ Hardware Plugins    │   │        Shell Plugin          │
              │  ┌──▼───────────────────┐ │   │  ┌────────────────────────┐  │
              │  │ buspirate_plugin     │ │   │  │      uShell            │  │
              │  │ hydrabus_plugin      │ │   │  │  ┌──────────────────┐  │  │
              │  │ ch347_plugin         │ │   │  │  │   ushell_core    │  │  │
              │  │ cp2112_plugin        │ │   │  │  │ (terminal, utils,│  │  │
              │  │ ftdi232h_plugin      │ │   │  │  │  config, keys)   │  │  │
              │  │ ftdi2232_plugin      │ │   │  │  └────────┬─────────┘  │  │
              │  │ ftdi4232_plugin      │ │   │  │           │            │  │
              │  │ ftdi245_plugin       │ │   │  │  ┌────────┴─────────┐  │  │
              │  │ uart_plugin          │ │   │  │  │  ushell_user     │  │  │
              │  │ uartmon_plugin       │ │   │  │  │ ┌──────────────┐ │  │  │
              │  └──────────┬───────────┘ │   │  │  │ │  user_root   │ │  │  │
              └─────────────┼─────────────┘   │  │  │ │  user_plugins│ │  │  │
                            │                 │  │  │ │  (template,  │ │  │  │
                            ▼                 │  │  │ │   test, ...) │ │  │  │
              ┌─────────────────────────┐     │  │  │ └──────────────┘ │  │  │
              │     HW Drivers Layer    │     │  │  └──────────────────┘  │  │
              │  ch347 / cp2112 / uart  │     │  └────────────────────────┘  │
              │  ftdi2xx (232/2232/     │     └──────────────────────────────┘
              │   4232/245) / hydrabus  │
              └─────────────────────────┘
```

**Layers (top → bottom):**

| Layer | Components |
|---|---|
| **Entry Point** | `uScriptMainApp` — parses CLI, loads INI, kicks off execution |
| **Config** | `IniCfgLoader` / `uIniParser` / `uSharedConfig` — shared settings |
| **Script Core** | `ScriptClient` → `ScriptRunner` → `Reader` + `Validator` + `Interpreter` |
| **Comm Scripts** | Extends core with comm-specific runner, validator & interpreter |
| **Plugin Interface** | `IPlugin` — abstract contract all plugins implement, loaded dynamically |
| **Hardware Plugins** | One plugin per device (BusPirate, Hydrabus, CH347, CP2112, FTDI×4, UART…) |
| **Shell Plugin** | Wraps `uShell` — full interactive shell with its own user plugin system |
| **HW Drivers** | Low-level OS/hardware wrappers (Linux/Windows) called by hardware plugins |
| **Utils** | Cross-cutting: Logger, Timer, FileReader, ArgParser, Hexdump, etc. |

---

## Repository Structure

```
sources/
├── include/
│   ├── driver/inc/          ICommDriver.hpp           — comm driver abstract interface
│   ├── plugin/inc/          IPlugin.hpp, IPluginDataTypes.hpp
│   └── script/
│       ├── core/inc/        IScript*.hpp              — core interpreter interfaces
│       ├── comm/inc/        ICommScript*.hpp          — comm interpreter interfaces
│       └── shell/inc/       IScriptInterpreterShell.hpp
└── src/
    ├── app/src/             uScriptMainApp.cpp        — executable entry point
    ├── config/inc/          uSharedConfig.hpp         — all global constants
    ├── lib/drivers/
    │   └── ch347/           CH347 native USB driver wrapper
    ├── script/
    │   ├── core/            Core Script interpreter, validator, data types
    │   ├── comm/            Comm Script interpreter, validator, data types
    │   └── shared/          ScriptReader, ScriptRunner (used by both)
    └── plugin/
        ├── template_plugin/ Boilerplate for writing new plugins
        ├── core_plugin/     Built-in utilities (eval, math, print, delay…)
        ├── shell_plugin/    Interactive shell session
        ├── uart_plugin/     Serial port send/receive with Comm Script support
        ├── uartmon_plugin/  Serial port insertion/removal monitor
        ├── buspirate_plugin/Bus Pirate: SPI, I2C, UART, 1-Wire, Raw-Wire
        ├── ch347_plugin/    WCH CH347 USB: SPI, I2C, GPIO, JTAG
        ├── cp2112_plugin/   SiLabs CP2112 USB-HID: I2C, GPIO
        ├── ftdi232h_plugin/ FTDI FT232H: SPI, I2C, GPIO, UART
        ├── ftdi2232_plugin/ FTDI FT2232H/D: SPI, I2C, GPIO, UART
        ├── ftdi245_plugin/  FTDI FT245: parallel FIFO, GPIO
        ├── ftdi4232_plugin/ FTDI FT4232H: SPI, I2C, GPIO, UART (quad)
        └── hydrabus_plugin/ HydraBus: SPI, I2C, UART, 1-Wire, SWD, NFC…
```

---

## Application Entry Point

The framework is launched from `uScriptMainApp.cpp` as a command-line tool:

```
uscript  [--script <path>]  [--inicfg <path>]
         defaults: script.txt   uscript.ini
```

At startup the app:
1. Parses CLI arguments.
2. Loads `uscript.ini` and reads the `[COMMON]` section to configure logging (severity, file output, colours, timestamps).
3. Constructs a `ScriptClient`, passing the script path and the pre-loaded `IniCfgLoader`.
4. Calls `client.execute()` and exits with `0` on success or `1` on failure.

### Global Configuration — `uscript.ini`

```ini
[COMMON]
LOG_SEVERITY_CONSOLE  = 3       ; 0=off … 5=debug
LOG_SEVERITY_FILE     = 4
LOG_FILE_ENABLED      = true
LOG_CONSOLE_COLORED   = true
LOG_INCLUDE_DATE      = false

[SCRIPT]
CMD_EXEC_DELAY        = 50      ; ms between every plugin command

[PLUGIN_NAME]
; plugin-specific key=value pairs forwarded to that plugin's setParams()
```

---

## Core Script Interpreter

The **Core Script Interpreter** is the main automation engine. It reads a text script, validates it statically, then executes it with a two-pass model: a dry-run for argument validation and a real pass for actual execution.

### Key Concepts at a Glance


#### Loading plugins

| Concept | Syntax | Description |
|---------|--------|-------------|
| Load plugin | `LOAD_PLUGIN NAME [op vX.Y.Z.W]` | Register a shared-library plugin |



#### Macros (variables)

| Concept | Syntax | Description |
|---------|--------|-------------|
| Constant macro | `NAME := value` | Validation-time text substitution |
| Variable macro | `name ?= PLUGIN.COMMAND [params]` | Capture a command's return value at runtime |
| Array macro | `NAME [= e0, e1, e2` | Ordered list of strings; elements accessed at runtime via `$NAME.$index` |



#### Commands 

| Concept | Syntax | Description |
|---------|--------|-------------|
| Command | `PLUGIN.COMMAND [params]` | Dispatch a command to a plugin |



#### Jumps

| Concept | Syntax | Description |
|---------|--------|-------------|
| Conditional jump | `IF expr GOTO label` | Skip forward to `LABEL` if expr is true |
| Unconditional jump | `GOTO label` | Always skip to `LABEL` |
| Label | `LABEL name` | Jump target for `GOTO` |



#### Loops

| Concept | Syntax | Description |
|---------|--------|-------------|
| Counted loop | `REPEAT label N` … `END_REPEAT label` | Execute body exactly N times |
| Counted loop + index | `idx ?= REPEAT label N` … `END_REPEAT label` | As above; `$idx` holds the 0-based iteration number inside the body |
| Conditional loop | `REPEAT label UNTIL cond` … `END_REPEAT label` | Execute body until `cond` is TRUE (do-while: body runs at least once) |
| Conditional loop + index | `idx ?= REPEAT label UNTIL cond` … `END_REPEAT label` | As above; `$idx` counts iterations from 0 |
| Break loop | `BREAK label` | Exit the named enclosing loop immediately |
| Continue loop | `CONTINUE label` | Skip to `END_REPEAT` of the named enclosing loop |



#### Multiline declarations

| Concept | Syntax | Description |
|---------|--------|-------------|
| Line continuation | trailing `\` | Join the physical line with the next; used to split long array declarations |



#### Comments

| Concept | Syntax | Description |
|---------|--------|-------------|
| Line comment | `# …` | Ignored |
| Block comment | `---` … `!--` | Multi-line ignore region |


---

### Syntax Reference


#### Comments

```
# This entire line is a comment
PLUGIN.COMMAND  arg    # inline comment — stripped before parsing

---
Everything between the markers is ignored.
Nesting is not supported.
!--
```

#### Plugin Loading

```
LOAD_PLUGIN  SERIAL
LOAD_PLUGIN  UPDATER  >= v2.0.0.0
LOAD_PLUGIN  SENSOR   == v1.4.2.0
```

#### Constant Macros — `:=`

Expanded at **validation time**. Zero runtime cost.

```
DEVICE  := /dev/ttyUSB0
BAUD    := 115200

SERIAL.OPEN  $DEVICE $BAUD
```

#### Array Macros — `[=`

An ordered list of string elements. Accessed at runtime with `$NAME.$index`.

```
# Single-line
PORTS  [=  /dev/ttyUSB0, /dev/ttyUSB1, /dev/ttyUSB2

# Multi-line (\ joins physical lines)
FW_IMAGES  [=  /opt/fw/board_A.bin, \
               /opt/fw/board_B.bin, \
               /opt/fw/board_C.bin

# Elements with spaces — no quoting needed
LABELS  [=  slot zero, slot one, slot two

# Elements containing commas — must be quoted with "..."
TAGS  [=  "alpha, beta", "gamma, delta", plain
```

Array element access — `$NAME.$index` is resolved as a single token:

```
# $i is a loop index macro; PORTS[i] is retrieved at runtime
i  ?=  REPEAT  open_ports  3
    SERIAL.OPEN  $PORTS.$i
END_REPEAT  open_ports
```

#### Variable Macros — `?=`

Capture the **return value** of a plugin command at execution time.

```
fw_ver  ?=  UPDATER.GET_VERSION
LOG.PRINT   version=$fw_ver
```

#### Conditional Flow — `IF` / `GOTO` / `LABEL`

Jumps are **forward-only** (GOTO must appear before its LABEL). GOTO labels and loop labels are separate namespaces.

```
result  ?=  SENSOR.READ_STATUS

IF  $result  GOTO  status_ok
LOG.PRINT  Error detected
GOTO  done
LABEL  status_ok
LOG.PRINT  All good
LABEL  done
```

#### Loops — `REPEAT` / `END_REPEAT`

**Counted loop:**

```
REPEAT  pulse  3
    GPIO.SET_HIGH  17
    GPIO.DELAY_MS  50
    GPIO.SET_LOW   17
END_REPEAT  pulse
```

**Counted loop with 0-based index capture:**

```
slot  ?=  REPEAT  flash_sensors  4
    LOG.PRINT    Configuring slot $slot
    SENSOR.SELECT   $slot
END_REPEAT  flash_sensors
# $slot is out of scope here
```

**Conditional loop (do-while — body always runs at least once):**

```
ready  ?=  SENSOR.IS_READY

REPEAT  wait_ready  UNTIL  $ready
    GPIO.DELAY_MS  200
    ready  ?=  SENSOR.IS_READY
END_REPEAT  wait_ready
```

**Conditional loop with iteration counter:**

```
attempt  ?=  REPEAT  flash_retry  UNTIL  $flash_ok
    UPDATER.FLASH   firmware.bin
    flash_ok  ?=  UPDATER.VERIFY
END_REPEAT  flash_retry
```

**Nested loops — each level needs its own unique label:**

```
bank  ?=  REPEAT  outer  3
    ch  ?=  REPEAT  inner  8
        SENSOR.CONFIGURE  $bank $ch
    END_REPEAT  inner
END_REPEAT  outer
```

#### BREAK and CONTINUE

Both keywords name the **target loop** explicitly, following Rust's labelled-loop convention. This eliminates ambiguity in nested loops.

`BREAK label` — exit the named loop immediately. All inner loops between the current position and the target are also unwound.

`CONTINUE label` — skip the rest of the current body and jump to `END_REPEAT` of the named loop. The loop's normal exit-or-loop-back logic then runs as usual.

```
slot  ?=  REPEAT  scan  8

    present  ?=  SENSOR.IS_PRESENT  $slot
    IF  $present  GOTO  slot_present
    CONTINUE  scan                     # absent — move to next slot
    LABEL  slot_present

    ok  ?=  SENSOR.RUN_SELFTEST  $slot
    IF  $ok  GOTO  slot_passed
    CONTINUE  scan                     # failed — try next slot
    LABEL  slot_passed

    SENSOR.ACTIVATE  $slot
    BREAK  scan                        # found a good slot — exit loop

END_REPEAT  scan
```

**Nested BREAK** — `BREAK outer` from inside the inner loop exits both loops:

```
bank  ?=  REPEAT  outer  3
    ch  ?=  REPEAT  inner  8

        ok  ?=  SENSOR.TEST  $bank $ch
        IF  $ok  GOTO  found
        CONTINUE  inner
        LABEL  found
        SENSOR.ACTIVATE  $bank $ch
        BREAK  outer               # exits inner AND outer

    END_REPEAT  inner
END_REPEAT  outer
```

### Macro Resolution Order

When a `$name` token is encountered at runtime, the interpreter resolves it through three tiers in priority order:

| Priority | Source | Scope |
|----------|--------|-------|
| 1 (highest) | Loop index macros — innermost active loop first | Loop body only; destroyed on `END_REPEAT` |
| 2 | Script-level variable macros (`?=` results) | Entire script; last written value wins |
| 3 (lowest) | Shell macros — set via `executeCmd()` | Script-wide |

For the `$NAME.$index` array access form, `NAME` is looked up in `mapArrayMacros` and `index` is resolved through the same three-tier chain.

Constant macros (`:=`) and array declarations (`[=`) are expanded at **validation time** and are not visible to the runtime resolver.

### Architecture Summary

The pipeline consists of five collaborating classes:

```
ScriptClient  →  ScriptRunner  →  ScriptReader
                              →  ScriptValidator  →  ScriptCommandValidator
                              →  ScriptInterpreter
```

`ScriptReader` produces a `vector<ScriptRawLine>` — each entry carries the **1-based source line number** alongside the content string, enabling every downstream component and any future frontend to map compiled IR nodes back to their exact file position. Multi-line `\` continuations are resolved in the reader; the logical line retains the line number of its first physical line.

`ScriptValidator` tokenises every line (via a regex-based lexer) and builds an in-memory IR (`ScriptEntriesType`) containing:
- `vPlugins` — plugin list with version constraints
- `mapMacros` — constant macro map (expanded immediately)
- `mapArrayMacros` — array macro map (`string → vector<string>`)
- `vCommands` — a `vector<ScriptLine>`, where each `ScriptLine` wraps a `std::variant` IR node together with its source line number

The validator performs full static analysis including: GOTO/LABEL forward-only checks, loop structure nesting and label uniqueness, BREAK/CONTINUE enclosure checks, and loop index macro name collision detection against script-level macros.

`ScriptInterpreter` loads plugins via `dlopen`, performs a dry-run pass (argument validation without side effects), enables plugins, then executes the real pass. The execution engine uses:
- An **index-based loop** (`while(i < vCommands.size())`) instead of a range-for, so `END_REPEAT` can set the index to `szBeginIndex` and the unconditional `++i` naturally lands at the first body command.
- A **`LoopState` stack** (`vector<LoopState>`) holding per-loop state including the iteration counter, condition template, scoped macro map (`mapLoopMacros`), and begin index. The vector (not `std::stack`) allows `m_replaceVariableMacros` to walk scopes from innermost to outermost for correct shadowing.
- A **`SkipReason` enum** (`NONE`, `GOTO`, `CONTINUE_LOOP`, `BREAK_LOOP`) alongside `m_strSkipUntilLabel` to distinguish which node type should clear the active skip, preventing GOTO labels from accidentally clearing a BREAK skip and vice versa.
- An **O(1) variable macro index** (`unordered_map<string, string*>`) built once per real-execution pass, replacing the O(n) reverse scan of `vCommands` on every `$macro` expansion.
- An **O(1) plugin command set index** (`unordered_map<string, unordered_set<string>>`) built once in `m_crossCheckCommands`.

The interpreter also exposes a **shell interface** (`IScriptInterpreterShell`) enabling privileged plugins to load new plugins or execute ad-hoc commands at runtime.

### Validation Rules

| Rule | Severity |
|------|----------|
| Duplicate constant macro name | Error |
| Duplicate array macro name | Error |
| Array macro name conflicts with constant macro name | Error |
| Array element list is empty | Error |
| Unterminated quote in array element list | Error |
| Plugin used but not declared with `LOAD_PLUGIN` | Error |
| `LOAD_PLUGIN` declared but no command uses it | Warning |
| Command not in plugin's supported command list | Error |
| `GOTO` without a matching `LABEL` | Error |
| `LABEL` without a preceding `GOTO` | Error |
| Duplicate `LABEL` name | Error |
| Backward `GOTO` (LABEL appears before GOTO) | Error |
| `GOTO` crossing a loop boundary | Error |
| Duplicate loop label | Error |
| Loop label conflicts with a `GOTO`/`LABEL` name | Error |
| `END_REPEAT` without matching `REPEAT` | Error |
| `END_REPEAT` label mismatch | Error |
| Unclosed loop (missing `END_REPEAT`) | Error |
| `BREAK`/`CONTINUE` used outside any loop | Error |
| `BREAK`/`CONTINUE` label does not name an enclosing loop | Error |
| Loop index macro name shadows a script-level variable macro | Error |
| Nested block comment | Error |

📄 **Full documentation:** [README.md](sources/src/script/core/README.md)

---

## Comm Script Interpreter

The **Comm Script Interpreter** is a companion system focused exclusively on protocol-level send/receive sequencing over any `ICommDriver`-derived transport. It shares the `ScriptReader` infrastructure with the Core system but uses a completely different syntax and has no plugin machinery of its own.

### Key Concepts at a Glance

| Symbol | Meaning | Example |
|--------|---------|---------| 
| `>` | Send, then optionally receive | `> "AT\r\n" \| T"OK"` |
| `<` | Receive, then optionally send | `< T"login:" \| "admin\r\n"` |
| `!` | Delay | `! 200 ms` |
| `NAME := value` | Constant macro | `BAUD := 115200` |

Data in each expression is annotated with a **decorator prefix** that determines encoding and matching strategy:

| Decorator | Type | Send | Receive |
|-----------|------|:----:|:-------:|
| `"…"` | Delimited string | ✓ | ✓ (exact compare) |
| *(none)* | Raw string | ✓ | ✓ |
| `H"…"` | Hex byte stream | ✓ | ✓ (exact compare) |
| `R"…"` | Regex pattern | — | ✓ (match) |
| `T"…"` | String token | — | ✓ (wait until found) |
| `X"…"` | Hex token | — | ✓ (wait until found) |
| `L"…"` | Newline-delimited line | ✓ | ✓ |
| `S"…"` | Exact byte count | — | ✓ |
| `F"…"` | File (chunked) | ✓ | ✓ (to file) |

### Architecture Summary

```
CommScriptClient<TDriver>  →  CommScriptRunner
                           →  ScriptReader              (shared)
                           →  CommScriptValidator       →  CommScriptCommandValidator
                           →  CommScriptInterpreter<TDriver>
                              └── CommScriptCommandInterpreter<TDriver>
                                  └── ICommDriver (tout_read / tout_write)
```

The entire system is templated on `TDriver`, keeping it driver-agnostic. `CommScriptCommandValidator` uses an `ItemParser` that determines the direction, splits the two fields on the pipe separator (respecting quoted content), classifies each field's decorator, and enforces semantic rules (e.g. you cannot *send* a regex or a SIZE specifier).

📄 **Full documentation:** [README.md](sources/src/script/comm/README.md)

---

## Shared Infrastructure

Both interpreter systems share the following components.

### `ScriptReader`

Header-only (`uScriptReader.hpp`). Reads a script file line by line and produces a `vector<ScriptRawLine>`. Each `ScriptRawLine` carries:
- `iLineNumber` — the **1-based line number** in the original file, preserved through comment stripping and continuation joining so every IR node can be traced back to its source location.
- `strContent` — the cleaned line content.

The reader strips:
- `#` line comments (and inline trailing `# …`)
- `---` / `!--` block comment regions (non-nestable)
- Leading and trailing whitespace

It also handles **line continuation**: if a line ends with `\` (after comment and whitespace stripping), the reader joins it with the following physical line. This repeats until no trailing `\` remains. The logical line retains the line number of the first physical line in the group. Line continuation is the primary mechanism for splitting long array macro declarations across multiple lines.

### `ScriptRunner<TScriptEntries>`

Header-only template (`uScriptRunner.hpp`). Orchestrates the three pipeline stages: `readScript → validateScript → interpretScript`. It now passes `vector<ScriptRawLine>` between the reader and validator stages. `CommScriptRunner` extends it to carry a typed reference to the comm interpreter.

### `uSharedConfig.hpp`

The single source of truth for all string constants and magic values used across the framework:

| Category | Examples |
|----------|---------| 
| File defaults | `script.txt`, `uscript.ini` |
| Comment markers | `#`, `---`, `!--` |
| Separators | `:=`, `?=`, `[=`, `.`, ` ` (space) |
| Macro marker | `$` |
| Plugin paths | `plugins/`, `lib`, `_plugin.so` |
| Decorator prefixes | `F"`, `R"`, `H"`, `T"`, `X"`, `L"`, `S"` |
| Time units | `us`, `ms`, `sec` |
| Default sizes | recv buffer 1024 B, chunk 1024 B |

### `IniCfgLoader`

Loads `uscript.ini` at startup. The `[COMMON]` section controls global logging. The `[SCRIPT]` section supplies `CMD_EXEC_DELAY`. Every other section name is matched to a plugin name and its key-value pairs are forwarded to that plugin's `setParams()` call.

---

## Plugin System

Plugins are independently compiled shared libraries (`.so` / `.dll`) placed in the `plugins/` directory. The interpreter discovers them by name, loads them with `dlopen` / `LoadLibrary`, and resolves two C entry points:

```cpp
extern "C" PluginInterface* pluginEntry();          // factory
extern "C" void             pluginExit(PluginInterface*);  // destructor
```

### Plugin Lifecycle

```
pluginEntry()  →  setParams()  →  doInit()  →  doEnable()
                                                    │
                          doDispatch(cmd, params) ←─┘  (repeated)
                                                    │
                          doCleanup()  ←────────────┘
pluginExit()
```

The two-pass model means `doDispatch()` is called **twice** per command: first with `isEnabled() == false` (argument validation only — no hardware side effects), and again after `doEnable()` for real execution.

### Plugin Flags

| Flag | INI key | Effect |
|------|---------|--------|
| `FAULT_TOLERANT` | `FAULT_TOLERANT=true` | A failing command logs an error but does not abort the script |
| `PRIVILEGED` | `PRIVILEGED=true` | `doInit()` receives the live `IScriptInterpreterShell*`, enabling the plugin to load new plugins or execute commands inline |

### Writing a Plugin

The `template_plugin` provides a ready-to-use skeleton. The command table is defined with an X-macro pattern, keeping the dispatch map, the method declarations, and the `getParams` command list all in sync from a single `PLUGIN_COMMANDS_CONFIG_TABLE` macro:

```cpp
#define MY_PLUGIN_COMMANDS_CONFIG_TABLE  \
MY_PLUGIN_CMD_RECORD( INFO    )          \
MY_PLUGIN_CMD_RECORD( OPEN    )          \
MY_PLUGIN_CMD_RECORD( READ    )          \
MY_PLUGIN_CMD_RECORD( WRITE   )
```

Each command handler follows the pattern:

```cpp
bool MyPlugin::m_My_WRITE(const std::string& args) const
{
    // 1. Validate args
    if (args.empty()) { ...; return false; }
    // 2. Short-circuit if dry-run
    if (!m_bIsEnabled) return true;
    // 3. Execute real action
    ...
    return true;
}
```

`getData()` / `resetData()` expose a `std::string` return channel used by variable macros (`?=`) in the Core Script.

---

## Available Plugins

### `CORE` — General-Purpose Utilities
Hardware-independent helper plugin for script orchestration. No `LOAD_PLUGIN` version constraint needed for basic use.

| Commands | Purpose |
|----------|---------| 
| `INFO` | Print plugin info |
| `DELAY ms` | Pause execution |
| `MESSAGE text` | Print a message to the log |
| `BREAKPOINT text` | Pause and wait for operator keypress |
| `PRINT fmt [| cond]` | Conditional message output |
| `FORMAT "items" | "pattern"` | String formatting (result via `?=`) |
| `MATH v1 op v2 [| HEX]` | Integer arithmetic (result via `?=`) |
| `RETURN value` | Return a string value to a variable macro |
| `VALIDATE v1 rule v2` | Assert a comparison; abort script on failure |
| `EVAL_VECT v1 rule v2` | Compare two values; return `TRUE`/`FALSE` (via `?=`) |
| `EVAL_BOEXPR expression` | Evaluate `&&` `\|\|` `!` `()` boolean expression (via `?=`) |
| `EVAL_BOARRAY items... \| AND\|OR` | Reduce a boolean array (via `?=`) |
| `FAIL [| cond]` | Unconditionally or conditionally fail the script |

📄 **Full documentation:** [README.md](sources/src/plugin/core_plugin/docs/README.md)


---

### `SHELL` — Interactive Shell Session
Launches an interactive **Microshell** terminal from within a running script. The shell is **privileged**: it receives a live reference to the interpreter and can load plugins, list macros, and dispatch commands in real time. Script execution resumes normally when the operator exits the shell.

| Commands | Purpose |
|----------|---------| 
| `INFO` | Print plugin info |
| `RUN` | Block and launch the interactive shell session |

Inside the shell, the `.` shortcut bridges to the script layer (e.g. `. PLUGINNAME.COMMAND args`, `.. PLUGINNAME` to load a plugin).

📄 **Full documentation:** [README.md](sources/src/plugin/shell_plugin/docs/README.md)


---

### `UART` — Serial Port (with Comm Script Support)
Drives a UART serial port. Supports inline command expressions using the same Comm Script decorator syntax, or delegates to a full `.txt` Comm Script file.

| Commands | Purpose |
|----------|---------| 
| `INFO` | Print plugin info |
| `CONFIG p:port b:baud r:rtout w:wtout s:size` | Configure the port |
| `CMD > "..." \| T"..."` | Single inline send/receive expression |
| `SCRIPT path.txt` | Execute a full Comm Script file against this port |

📄 **Full documentation:** [README.md](sources/src/plugin/uart_plugin/docs/README.md)


---

### `UARTMON` — Serial Port Monitor
Background-thread monitor that detects serial port insertion and removal events. Useful as a first step in an automation sequence that must wait for a device to enumerate.

| Commands | Purpose |
|----------|---------| 
| `INFO` | Print plugin info |
| `LIST_PORTS` | Log all currently present serial ports (result via `?=`) |
| `START` | Start the background monitor thread |
| `STOP` | Stop the background monitor thread |
| `WAIT_INSERT [timeout_ms]` | Block until a new port appears (result via `?=`) |
| `WAIT_REMOVE [timeout_ms]` | Block until a port disappears |

📄 **Full documentation:** [README.md](sources/src/plugin/uartmon_plugin/docs/README.md)


---

### `BUSPIRATE` — Bus Pirate Multi-Protocol Adapter
Drives the Bus Pirate hardware via its binary-mode serial protocol. Supports five protocol modes; only one mode can be active at a time. Supports external Comm Script files for complex exchanges within a mode.

| Protocol | Commands |
|----------|---------| 
| `MODE` | Switch mode: `spi`, `i2c`, `uart`, `1wire`, `rawwire`, `bbio` |
| `SPI` | `speed`, `cfg`, `cs`, `write`, `read`, `wrrd`, `script`, `per` |
| `I2C` | `speed`, `write`, `read`, `wrrd`, `scan`, `script`, `per` |
| `UART` | `cfg`, `write`, `read`, `wrrd`, `script`, `per` |
| `ONEWIRE` | `write`, `read`, `wrrd`, `scan`, `script` |
| `RAWWIRE` | `write`, `read`, `clk`, `script` |

📄 **Full documentation:** [README.md](sources/src/plugin/buspirate_plugin/docs/README.md)


---

### `CH347` — WCH CH347 USB Hi-Speed Adapter
Direct USB driver (no serial port) for the CH347 chip. All four interfaces can be **open simultaneously** on one device. Supports external Comm Script files for each interface.

| Interface | Key Commands |
|-----------|-------------|
| `SPI` | `open`, `close`, `write`, `read`, `wrrd`, `cs`, `script` |
| `I2C` | `open`, `close`, `scan`, `write`, `read`, `wrrd`, `script` |
| `GPIO` | `open`, `close`, `set`, `read` |
| `JTAG` | `open`, `close`, `write ir/dr`, `read ir/dr`, `reset` |

📄 **Full documentation:** [README.md](sources/src/plugin/ch347_plugin/docs/README.md)


---

### `CP2112` — Silicon Labs CP2112 USB-HID Bridge
USB-HID based I²C/SMBus and GPIO controller. All communication goes through 64-byte HID reports; the plugin handles chunking transparently.

| Interface | Key Commands |
|-----------|-------------|
| `I2C` | `open`, `close`, `scan`, `write`, `read`, `wrrd`, `script` |
| `GPIO` | `open`, `close`, `set`, `read`, `dir` |

📄 **Full documentation:** [README.md](sources/src/plugin/cp2112_plugin/docs/README.md)


---

### `FT232H` — FTDI FT232H Single-Channel Hi-Speed Adapter
Single MPSSE channel; SPI, I2C, and GPIO share the channel and cannot be open simultaneously. UART operates in VCP mode, exclusive with MPSSE modes.

| Interface | Key Commands |
|-----------|-------------|
| `SPI` | `open`, `close`, `write`, `read`, `wrrd`, `cs`, `script` |
| `I2C` | `open`, `close`, `scan`, `write`, `read`, `wrrd`, `script` |
| `GPIO` | `open`, `close`, `set low/high`, `read` |
| `UART` | `open`, `close`, `cfg`, `script` |

📄 **Full documentation:** [README.md](sources/src/plugin/ftdi232h_plugin/docs/README.md)


---

### `FT2232` — FTDI FT2232H / FT2232D Dual-Channel Adapter
Two hardware variants in one binary selected by `variant=H|D`. The H variant provides two MPSSE channels (A and B) with a 60 MHz clock; the D variant has one MPSSE channel and a UART on channel B with a 6 MHz clock.

📄 **Full documentation:** [README.md](sources/src/plugin/ftdi2232_plugin/docs/README.md)


---

### `FT245` — FTDI FT245 Parallel FIFO Adapter
USB parallel FIFO bridge (no serial engine). Two variants: `BM` (async + sync FIFO) and `R` (async FIFO only). FIFO and GPIO are **mutually exclusive**.

| Interface | Key Commands |
|-----------|-------------|
| `FIFO` | `open`, `close`, `write`, `read` |
| `GPIO` | `open`, `close`, `set`, `read` |

📄 **Full documentation:** [README.md](sources/src/plugin/ftdi245_plugin/docs/README.md)


---

### `FT4232` — FTDI FT4232H Quad-Channel Adapter
Four independent channels; A and B are MPSSE (SPI, I2C, GPIO), C and D are async UART. All four can operate simultaneously on separate USB handles.

📄 **Full documentation:** [README.md](sources/src/plugin/ftdi4232_plugin/docs/README.md)


---

### `HYDRABUS` — HydraBus Multi-Protocol Adapter
The most protocol-rich adapter plugin. Driven over UART, it supports ten protocol modes via the HydraHAL library. Modes are selected with `HYDRABUS.MODE <protocol>`.

| Protocols | |
|-----------|---|
| `SPI` | `I2C` |
| `UART` | `ONEWIRE` |
| `RAWWIRE` | `SWD` |
| `SMARTCARD` | `NFC` |
| `MMC` | `SDIO` |

📄 **Full documentation:** [README.md](sources/src/plugin/hydrabus_plugin/docs/README.md)


---

## Abstract Interfaces

All major extension points are defined as pure abstract C++ interfaces, enabling alternative implementations and unit testing:

| Interface | Location | Purpose |
|-----------|----------|---------| 
| `PluginInterface` | `include/plugin/inc/IPlugin.hpp` | Every plugin must implement this |
| `ICommDriver` | `include/driver/inc/ICommDriver.hpp` | Any transport used by Comm Scripts |
| `IScriptReader` | `include/script/core/inc/` | File / source reader abstraction — produces `vector<ScriptRawLine>` |
| `IScriptValidator<T>` | `include/script/core/inc/` | Script validation stage — consumes `vector<ScriptRawLine>` |
| `IScriptInterpreter<T>` | `include/script/core/inc/` | Script execution stage |
| `IScriptInterpreterShell<T>` | `include/script/shell/inc/` | Extended interpreter for privileged plugins |
| `ICommScriptInterpreter<T,D>` | `include/script/comm/inc/` | Comm-driver-aware interpreter |
| `ICommScriptCommandInterpreter<C,D>` | `include/script/comm/inc/` | Per-command Comm execution |

---

## Quick-Start Example

### Core Script (`script.txt`)

```
# Load plugins
LOAD_PLUGIN  UARTMON
LOAD_PLUGIN  UART
LOAD_PLUGIN  CH347
LOAD_PLUGIN  CORE

# Constant macros
REQUIRED_FW   := 3.1.0.0
FLASH_PORTS   [=  /dev/ttyUSB0, /dev/ttyUSB1, /dev/ttyUSB2

# Wait for device to enumerate
UARTMON.START
new_port  ?=  UARTMON.WAIT_INSERT  10000
UARTMON.STOP

# Configure and probe the UART
UART.CONFIG  p:$new_port  b:115200  r:3000  w:3000

# Use Comm Script for the serial handshake
UART.SCRIPT  handshake.txt

# Verify firmware version
fw_ver  ?=  CORE.RETURN  3.1.0.0          # replace with real read command
CORE.VALIDATE  $fw_ver  >=  $REQUIRED_FW

# Flash three boards in sequence using an array of ports
board  ?=  REPEAT  flash_loop  3
    CORE.MESSAGE   Flashing board $board via $FLASH_PORTS.$board
    CH347.SPI      open  clock=15000000  mode=0
    CH347.SPI      script  flash_sequence.txt
    CH347.SPI      close

    ok  ?=  CORE.EVAL_VECT  $board >= 0    # replace with real verify command
    IF  $ok  GOTO  flash_ok
    CORE.MESSAGE   Board $board flash FAILED — stopping
    BREAK  flash_loop
    LABEL  flash_ok
    CORE.MESSAGE   Board $board flash OK

END_REPEAT  flash_loop
```

### Comm Script (`handshake.txt`)

```
# Wait for boot banner
< R".*Boot.*"

# Login
< T"login:"   | "admin\r\n"
< T"Password:"| "secret\r\n"
< "$ "

# Transfer firmware version query and validate response
> "fw_version\r\n"   | L"v3.1.0"

! 200 ms
```

---

## Detailed Documentation Index

### Script Interpreters

| Document | Scope |
|----------|-------|
| Core | [README.md](sources/src/script/core/README.md) |
| Comm | [README.md](sources/src/script/comm/README.md) |

### Plugins

| Document | Scope |
|----------|-------|
| CORE | [README.md](sources/src/plugin/core_plugin/docs/README.md) |
| SHELL | [README.md](sources/src/plugin/shell_plugin/docs/README.md) |
| UART | [README.md](sources/src/plugin/uart_plugin/docs/README.md) |
| UARTMON | [README.md](sources/src/plugin/uartmon_plugin/docs/README.md) |
| BUSPIRATE | [README.md](sources/src/plugin/buspirate_plugin/docs/README.md) |
| CH347 | [README.md](sources/src/plugin/ch347_plugin/docs/README.md) |
| CP2112 | [README.md](sources/src/plugin/cp2112_plugin/docs/README.md) |
| FT232H | [README.md](sources/src/plugin/ftdi232h_plugin/docs/README.md) |
| FT2232 | [README.md](sources/src/plugin/ftdi2232_plugin/docs/README.md) |
| FT245 | [README.md](sources/src/plugin/ftdi245_plugin/docs/README.md) |
| FT4232 | [README.md](sources/src/plugin/ftdi4232_plugin/docs/README.md) |
| HYDRABUS | [README.md](sources/src/plugin/hydrabus_plugin/docs/README.md) |


