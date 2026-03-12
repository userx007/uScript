# Core Script Interpreter — README

## Overview

The **Core Script** system is a plugin-driven, sequential command interpreter designed to automate tasks through a small, structured scripting language. Scripts load shared-library plugins, define constant and variable macros, dispatch commands to those plugins, and optionally perform conditional flow control via `IF/GOTO/LABEL` constructs.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                          ScriptClient                               │
│  (single entry-point: constructs the pipeline, calls execute())     │
└──────────────────────────────┬──────────────────────────────────────┘
                               │ owns
                               ▼
┌─────────────────────────────────────────────────────────────────────┐
│                        ScriptRunner<ScriptEntriesType>              │
│  Orchestrates the three pipeline stages in sequence:                │
│    1. read  → 2. validate  → 3. interpret                           │
└────────┬────────────────────┬─────────────────────────┬─────────────┘
         │                    │                         │
  owns   │         owns       │                   owns  │
         ▼                    ▼                         ▼
┌──────────────┐   ┌────────────────────┐   ┌─────────────────────────┐
│ ScriptReader │   │  ScriptValidator   │   │   ScriptInterpreter     │
│              │   │                    │   │                         │
│ Reads lines  │   │ Tokenises each     │   │ Two-pass execution:     │
│ from file,   │   │ line via           │   │  pass 1 – dry-run       │
│ strips:      │   │ ScriptCommand-     │   │    (arg validation)     │
│ • comments   │   │ Validator, then    │   │  pass 2 – real exec     │
│ • blank lines│   │ builds Parsed IR:  │   │                         │
│ • block      │   │  vPlugins[]        │   │ Manages plugins:        │
│   comments   │   │  mapMacros{}       │   │  loadPlugin()           │
└──────────────┘   │  vCommands[]       │   │  initPlugin()           │
                   │                    │   │  enablePlugin()         │
                   │ Cross-validates:   │   │  dispatchCommand()      │
                   │  • Condition/Label │   │                         │
                   │  • Plugin deps     │   │ Shell interface:        │
                   └────────┬───────────┘   │  executeCmd()  (REPL)   │
                            │               │  loadPlugin()           │
                            ▼               └─────────────────────────┘
                   ┌─────────────────┐
                   │ ScriptCommand-  │
                   │ Validator       │
                   │                 │
                   │ Pure regex-     │
                   │ based lexer:    │
                   │ classifies each │
                   │ line into a     │
                   │ Token enum      │
                   └─────────────────┘
```

### Data Flow

```
 Script file (.script)
        │
        ▼
  ScriptReader::readScript()
  ──────────────────────────
  • Open file line by line
  • Trim whitespace
  • Skip blank / # comment lines
  • Handle --- ... !-- block comments
  • Strip trailing inline comments
        │
        │  vector<string>  (raw cleaned lines)
        ▼
  ScriptValidator::validateScript()
  ──────────────────────────────────
  For each line:
    1. replaceMacros()  ← expand already-known $CMACROs
    2. ScriptCommandValidator::validateCommand()  ← classify → Token
    3. m_preprocessScriptStatements()  ← build IR objects
       LOAD_PLUGIN  → PluginDataType  pushed to  vPlugins[]
       CONSTANT_MACRO → entry in  mapMacros{}
       VARIABLE_MACRO → MacroCommand  pushed to  vCommands[]
       COMMAND      → Command       pushed to  vCommands[]
       IF_GOTO_LABEL→ Condition     pushed to  vCommands[]
       LABEL        → Label         pushed to  vCommands[]
  After all lines:
    m_validateConditions()  ← every GOTO has a matching LABEL; no dupes
    m_validatePlugins()     ← every used plugin is declared; warn unused
        │
        │  ScriptEntriesType { vPlugins, mapMacros, vCommands }
        ▼
  ScriptInterpreter::interpretScript()
  ─────────────────────────────────────
  1. m_loadPlugins()        ← dlopen each plugin, call getParams()/setParams()
  2. m_crossCheckCommands() ← each Command.strCommand ∈ plugin.vstrPluginCommands
  3. m_initPlugins()        ← call doInit()  (privileged plugins get 'this')
  4. m_executeCommands(dry) ← dispatch all commands with bEnabled=false
                              (plugins validate args without side-effects)
  5. m_enablePlugins()      ← call doEnable() on all plugins
  6. m_executeCommands(real)← full execution with macro substitution & timing
```

---

## Internal Intermediate Representation (IR)

After validation the script is represented as `ScriptEntriesType`:

```
ScriptEntriesType
├── vPlugins  : vector<PluginDataType>
│     strPluginName          (upper-cased)
│     strPluginVersRule      ( "<", "<=", ">", ">=", "==" )
│     strPluginVersRequested ( "v1.2.3.4" )
│     shptrPluginEntryPoint  (IPlugin*)
│     hLibHandle             (void*  dlopen handle)
│     sGetParams             (name, version, supported commands list)
│     sSetParams             (logger, .ini settings map)
│
├── mapMacros : unordered_map<string,string>
│     "MACRO_NAME" → "literal value"    (constant macros only)
│
└── vCommands : vector<ScriptCommandType>   (variant)
      ┌─ Command      { strPlugin, strCommand, strParams }
      ├─ MacroCommand { strPlugin, strCommand, strParams,
      │                 strVarMacroName, strVarMacroValue }
      ├─ Condition    { strCondition, strLabelName }
      └─ Label        { strLabelName }
```

---

## Token Classification (Lexer)

`ScriptCommandValidator` uses **ordered regex matching** — the first pattern that matches wins:

| Priority | Token            | Regex pattern (simplified)                              |
|----------|------------------|---------------------------------------------------------|
| 1        | `LOAD_PLUGIN`    | `^LOAD_PLUGIN\s+NAME(\s+(op)\s+v\d+\.\d+\.\d+\.\d+)?$` |
| 2        | `CONSTANT_MACRO` | `^[A-Za-z_]\w*\s*:=\s*\S.*$`                           |
| 3        | `VARIABLE_MACRO` | `^[A-Za-z_]\w*\s*\?=\s*PLUGIN\.COMMAND.*$`             |
| 4        | `COMMAND`        | `^PLUGIN\.COMMAND\s*.*$`  (all-upper plugin & command)  |
| 5        | `IF_GOTO_LABEL`  | `^(IF\s+expr\s+)?GOTO\s+label$`                        |
| 6        | `LABEL`          | `^LABEL\s+label$`                                       |
| —        | `INVALID`        | (none of the above)                                     |

> Plugin and command names must be **UPPER_CASE** with at least two characters, connected by `.`.
> Label and macro names follow standard identifier rules (`[A-Za-z_][A-Za-z0-9_]*`).

---

## Supported Syntax Reference

### 1. Comments

```
# This is a line comment — the whole line is ignored

---
   This is a block comment.
   Multiple lines, all ignored.
   Nesting is NOT supported.
!--

PLUGIN.COMMAND arg   # inline comment after a command
```

### 2. Plugin Loading — `LOAD_PLUGIN`

```
LOAD_PLUGIN  <PLUGIN_NAME>  [<op>  v<major>.<minor>.<patch>.<build>]
```

- `PLUGIN_NAME` is upper-case; maps to a shared library on disk.
- Optional version constraint: operator is one of `<`, `<=`, `>`, `>=`, `==`.
- Duplicate declarations are rejected.
- Settings are pulled from a `.ini` file section matching the plugin name.

```
LOAD_PLUGIN  SERIAL
LOAD_PLUGIN  GPIO
LOAD_PLUGIN  UPDATER  >= v2.0.0.0
LOAD_PLUGIN  SENSOR   == v1.4.2.0
```

### 3. Constant Macros — `:=`

```
<MACRO_NAME>  :=  <value>
```

- Defined **once**; re-declaration is an error.
- Expanded at **validation time** using `$MACRO_NAME` anywhere in subsequent lines.
- Value is a literal string (anything after `:=` until end-of-line, trimmed).

```
BAUD_RATE   := 115200
DEVICE_PATH := /dev/ttyUSB0
TIMEOUT_MS  := 3000

SERIAL.OPEN  $DEVICE_PATH $BAUD_RATE
```

### 4. Variable Macros — `?=`

```
<macro_name>  ?=  <PLUGIN>.<COMMAND>  [params]
```

- Captures the **return value** of the plugin command into `macro_name`.
- Evaluated at **execution time**; the captured value is updated each run.
- Referenced in subsequent commands as `$macro_name` (lower-case allowed).
- If the macro name is re-used, the most recently assigned value wins (reverse
  scan of `vCommands`).

```
fw_version  ?=  UPDATER.GET_VERSION
device_id   ?=  SENSOR.READ_ID

# use the captured values in later commands
UPDATER.FLASH_IF_NEEDED  $fw_version
LOG.PRINT  device=$device_id fw=$fw_version
```

### 5. Commands — `PLUGIN.COMMAND`

```
<PLUGIN>.<COMMAND>  [params]
```

- `PLUGIN` and `COMMAND` are **fully upper-case** identifiers.
- `params` is an optional free-form string passed verbatim to the plugin's
  `doDispatch()` handler after macro substitution.
- Validated twice: once dry-run (arg validation), once for real.

```
SERIAL.OPEN    /dev/ttyUSB0 115200
GPIO.SET_HIGH  PIN_RESET
GPIO.DELAY_MS  50
GPIO.SET_LOW   PIN_RESET
SERIAL.CLOSE
```

### 6. Conditional Flow — `IF … GOTO` / `GOTO`

```
IF  <boolean-expression>  GOTO  <label>
GOTO  <label>
LABEL <label>
```

- **Forward-only** jumps: `GOTO` must appear **before** its `LABEL` in the file.
- Every `GOTO` must have a matching `LABEL`; every `LABEL` must have a preceding `GOTO`. Both are validated statically.
- Duplicate `LABEL` names are rejected.
- When a condition evaluates to `true`, all commands between the `IF GOTO` and the
  `LABEL` are **skipped** (including nested commands, other conditions, etc.).
- `GOTO label` without `IF` is an **unconditional** jump (condition defaults to `true`).
- Boolean expressions support the full `BoolExprEvaluator` syntax.

```
# Unconditional skip
GOTO  skip_section
  PLUGIN.COMMAND_A   # never executed
  PLUGIN.COMMAND_B   # never executed
LABEL skip_section

# Conditional skip based on variable macro result
fw_ver  ?=  UPDATER.GET_VERSION
IF  $fw_ver == "2.0.0"  GOTO  already_up_to_date
UPDATER.FLASH   firmware.bin
LABEL already_up_to_date

# Multi-condition example
status  ?=  SENSOR.READ_STATUS
IF  $status != "OK"  GOTO  error_handler
SENSOR.PROCESS
GOTO  done
LABEL error_handler
LOG.PRINT error
LABEL done
```

---

## Execution Phases (Two-Pass Model)

```
Pass 1 — Dry Run (bRealExec = false)
  For each command in vCommands:
    • Plugin is found, doDispatch() called with bEnabled=false
    • Plugin validates arguments without side-effects
    • MacroCommand result NOT captured
    • IF/GOTO/LABEL NOT evaluated (skipped)
    → Failure here aborts before any real I/O

Pass 2 — Real Execution (bRealExec = true)
  m_enablePlugins() → doEnable() on all plugins
  For each command in vCommands:
    1. m_replaceVariableMacros(params)
       Regex scans for $name patterns, reverse-scans vCommands for last
       MacroCommand with matching strVarMacroName, substitutes the value.
       Loops until no more substitutions (supports chained macros).
    2. Plugin.doDispatch(command, params)
       On success, if MacroCommand: capture getData() → strVarMacroValue,
       call resetData().
    3. utime::delay_ms(m_szDelay)   (inter-command delay from .ini)
    4. Condition: evaluate strCondition via BoolExprEvaluator.
       If true → set m_strSkipUntilLabel = label name (start skipping).
    5. Label: if m_strSkipUntilLabel matches → clear it (stop skipping).
```

---

## Plugin Interface

Each plugin is a shared library exposing two C symbols:

```cpp
extern "C" IPlugin* script_plugin_entry();   // SCRIPT_PLUGIN_ENTRY_POINT_NAME
extern "C" void     script_plugin_exit(IPlugin*); // SCRIPT_PLUGIN_EXIT_POINT_NAME
```

`IPlugin` provides:
- `getParams(PluginDataGet*)` — reports version string and supported command names
- `setParams(PluginDataSet*)` — receives logger + `.ini` settings
- `doInit(IScriptInterpreterShell*)` — one-time init; privileged plugins receive the interpreter shell pointer, allowing them to call `loadPlugin()`, `executeCmd()`, etc.
- `doEnable()` — called once before real execution
- `doDispatch(command, params)` — called per command; returns bool
- `getData()` / `resetData()` — used by variable macros to capture return values
- `isPrivileged()` — if true, `doInit` receives the shell pointer

---

## Shell Interface (REPL / Dynamic Use)

`ScriptInterpreter` exposes methods for external/dynamic use:

| Method | Description |
|--------|-------------|
| `loadPlugin(name, initEnable)` | Dynamically load a plugin at runtime |
| `executeCmd(string)` | Parse and execute a single command string inline |
| `listMacrosPlugins()` | Log all macros (constant + variable) and loaded plugins |
| `listCommands()` | Log all commands in the IR |

`executeCmd()` performs full macro expansion then dispatches through the same
validation/execution path as the script runner, and stores any captured variable
macro values in the shell's own `m_ShellVarMacros` map.

---

## Configuration (.ini file)

The interpreter reads a section from a `.ini` file at construction time:

```ini
[SCRIPT]
CMD_EXEC_DELAY = 100        ; inter-command delay in ms

[SERIAL]
port    = /dev/ttyUSB0
baud    = 115200

[GPIO]
chip    = /dev/gpiochip0
```

The `[SCRIPT]` section sets global interpreter parameters. Each plugin section is
resolved by `IniCfgLoader` and forwarded to the plugin via `setParams()`.

---

## Complete Example Script

```
# =============================================================================
# Example: Firmware update sequence
# =============================================================================

/*
  Pre-conditions:
    - Device is powered on and connected via USB serial.
    - Firmware binary is present at the configured path.
*/

# --- Plugin declarations ------------------------------------------------------
LOAD_PLUGIN  SERIAL   >= v1.0.0.0
LOAD_PLUGIN  GPIO
LOAD_PLUGIN  UPDATER  == v3.1.0.0
LOAD_PLUGIN  LOG

# --- Constant macros ----------------------------------------------------------
DEVICE        := /dev/ttyUSB0
BAUD          := 115200
FW_FILE       := /opt/fw/firmware_v3.bin
RESET_PIN     := 17
BOOT_PIN      := 18

# --- Open communication channel -----------------------------------------------
SERIAL.OPEN   $DEVICE $BAUD

# --- Read current firmware version --------------------------------------------
fw_current  ?=  UPDATER.GET_VERSION

# --- Conditionally skip flashing if already at target version -----------------
IF  $fw_current == "3.1.0"  GOTO  version_ok

LOG.PRINT     Flashing new firmware: $FW_FILE
GPIO.SET_HIGH $BOOT_PIN
GPIO.SET_LOW  $RESET_PIN
GPIO.DELAY_MS 100
GPIO.SET_HIGH $RESET_PIN

UPDATER.FLASH $FW_FILE

GPIO.SET_LOW  $BOOT_PIN
GPIO.SET_LOW  $RESET_PIN
GPIO.DELAY_MS 50
GPIO.SET_HIGH $RESET_PIN

# Verify after flashing
fw_after  ?=  UPDATER.GET_VERSION
IF  $fw_after == "3.1.0"  GOTO  version_ok

LOG.PRINT     Update FAILED. Version still: $fw_after
GOTO  cleanup

LABEL version_ok
LOG.PRINT  Firmware OK: $fw_current

LABEL cleanup
SERIAL.CLOSE
```

---

## Validation Rules Summary

| Rule | Description |
|------|-------------|
| Duplicate constant macro | Error — each `NAME :=` can appear only once |
| Missing plugin | Error — a command references a plugin not declared with `LOAD_PLUGIN` |
| Unused plugin | Warning — a `LOAD_PLUGIN` declared but no command uses it |
| Unknown command | Error — plugin's `vstrPluginCommands` does not contain the command name |
| `GOTO` without `LABEL` | Error — every jump target must be defined |
| `LABEL` without `GOTO` | Error — orphan labels are rejected |
| Duplicate `LABEL` | Error — label names must be unique |
| Backward `GOTO` | Error — jumps must be forward-only (GOTO index < LABEL index) |
| Nested block comment | Error — `/*` inside `/*` is not supported |

---

## Component Summary

| Component | File | Role |
|-----------|------|------|
| `ScriptClient` | `uScriptClient.hpp` | Facade: constructs full pipeline, exposes `execute()` |
| `ScriptRunner` | `uScriptRunner.hpp` | Orchestrates read → validate → interpret |
| `ScriptReader` | `uScriptReader.hpp` | File I/O, comment stripping |
| `ScriptValidator` | `uScriptValidator.cpp/.hpp` | Tokenises lines, builds IR, static validation |
| `ScriptCommandValidator` | `uScriptCommandValidator.hpp` | Regex-based single-line lexer |
| `ScriptInterpreter` | `uScriptInterpreter.cpp/.hpp` | Two-pass plugin loader and command executor |
| `ScriptDataTypes` | `uScriptDataTypes.hpp` | IR types: `Command`, `MacroCommand`, `Condition`, `Label`, `Token` enum |
