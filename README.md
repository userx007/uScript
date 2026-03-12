# uScript — Scripting & Automation Framework

## Overview

**uScript** is a C++ scripting and hardware-automation framework built around two complementary script interpreters and a plugin ecosystem that abstracts real hardware interfaces. It is designed to drive embedded-systems testing, hardware bring-up, and protocol-level automation with a clean, readable scripting syntax and a strongly layered, interface-driven architecture.

The framework ships as a standalone executable plus a set of independently loadable shared-library plugins (`.so` / `.dll`). Scripts are plain text files processed at runtime with no pre-compilation step.

```
┌──────────────────────────────────────────────────────────────────┐
│                        uScriptMainApp                            │
│          (CLI: --script path  --inicfg path)                     │
└────────────────────────────┬─────────────────────────────────────┘
                             │
              ┌──────────────┴────────────────┐
              │                               │
              ▼                               ▼
  ┌────────────────────────┐    ┌─────────────────────────────────┐
  │   Core Script System   │    │      Comm Script System         │
  │  (plugin-driven        │    │   (driver-driven send/receive   │
  │   general automation)  │    │    protocol sequencing)         │
  └────────────┬───────────┘    └──────────────┬──────────────────┘
               │                               │
               ▼                               ▼
  ┌────────────────────────┐    ┌──────────────────────────────────┐
  │  Plugin Ecosystem      │    │  ICommDriver implementations     │
  │  (hardware adapters,   │    │  (CH347, FTDI, BusPirate, etc.)  │
  │   utilities, shell...) │    └──────────────────────────────────┘
  └────────────────────────┘
               ▲
               │ shared
  ┌────────────────────────┐
  │  Shared Infrastructure │
  │  ScriptReader          │
  │  ScriptRunner          │
  │  IniCfgLoader          │
  │  uSharedConfig.hpp     │
  └────────────────────────┘
```

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

| Concept | Syntax | Description |
|---------|--------|-------------|
| Load plugin | `LOAD_PLUGIN NAME [op vX.Y.Z.W]` | Register a shared-library plugin |
| Constant macro | `NAME := value` | Compile-time text substitution |
| Command | `PLUGIN.COMMAND [params]` | Dispatch a command to a plugin |
| Variable macro | `name ?= PLUGIN.COMMAND [params]` | Capture a command's return value |
| Conditional jump | `IF expr GOTO label` | Skip forward to `LABEL` if expr is true |
| Unconditional jump | `GOTO label` | Always skip to `LABEL` |
| Label | `LABEL name` | Jump target |
| Line comment | `# …` | Ignored |
| Block comment | `---` … `!--` | Multi-line ignore region |

### Architecture Summary

The pipeline consists of five collaborating classes:

```
ScriptClient  →  ScriptRunner  →  ScriptReader
                              →  ScriptValidator  →  ScriptCommandValidator
                              →  ScriptInterpreter
```

`ScriptValidator` tokenises every line (via a regex-based lexer) and builds an in-memory IR (`ScriptEntriesType`) containing the plugin list, constant-macro map, and a `std::variant` command sequence. `ScriptInterpreter` loads the plugins via `dlopen`, performs a dry-run pass (argument validation without side effects), enables the plugins, then executes the real pass with full variable-macro substitution and inter-command timing.

The interpreter also exposes a **shell interface** (`IScriptInterpreterShell`) enabling privileged plugins to load new plugins or execute ad-hoc commands at runtime.

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

Header-only (`uScriptReader.hpp`). Reads a script file line by line, stripping:
- `#` line comments (and inline trailing `# …`)
- `---` / `!--` block comment regions (non-nestable)
- Leading and trailing whitespace

Returns a `vector<string>` of clean lines to the validator.

### `ScriptRunner<TScriptEntries>`

Header-only template (`uScriptRunner.hpp`). Orchestrates the three pipeline stages: `readScript → validateScript → interpretScript`. `CommScriptRunner` extends it to carry a typed reference to the comm interpreter.

### `uSharedConfig.hpp`

The single source of truth for all string constants and magic values used across the framework:

| Category | Examples |
|----------|---------|
| File defaults | `script.txt`, `uscript.ini` |
| Comment markers | `#`, `---`, `!--` |
| Separators | `:=`, `?=`, `.`, ` ` (space) |
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
| `PRINT fmt args` | Formatted string output (captured by `?=`) |
| `FORMAT fmt args` | String formatting (result via `?=`) |
| `MATH expr` | Arithmetic evaluation (result via `?=`) |
| `VALIDATE expr` | Assert that a boolean expression is true |
| `EVAL_VECT` | Compare two space-separated value vectors |
| `EVAL_BOEXPR` | Evaluate a full boolean expression |
| `EVAL_BOARRAY` | Evaluate a boolean expression across a value array |
| `FAIL` | Unconditionally fail the script |
| `RETURN` | Immediately succeed and exit the script |

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
| `IScriptReader` | `include/script/core/inc/` | File / source reader abstraction |
| `IScriptValidator<T>` | `include/script/core/inc/` | Script validation stage |
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

# Wait for device to enumerate
UARTMON.START
new_port  ?=  UARTMON.WAIT_INSERT  10000
UARTMON.STOP

# Configure and probe the UART
UART.CONFIG  p:$new_port  b:115200  r:3000  w:3000

# Use Comm Script for the serial handshake
UART.SCRIPT  handshake.txt

# Open SPI and flash the firmware
CH347.SPI    open  clock=15000000  mode=0
CH347.SPI    script  flash_sequence.txt
CH347.SPI    close
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

### Script Interpretors

| Document | Scope |
|----------|-------|
|Core      |[README.md](sources/src/script/core/README.md)|
|Comm      |[README.md](sources/src/script/comm/README.md)|

### Plugins 

| Document | Scope |
|----------|-------|
|CORE      |[README.md](sources/src/plugin/core_plugin/docs/README.md)|
|SHELL     |[README.md](sources/src/plugin/shell_plugin/docs/README.md)|
|UART      |[README.md](sources/src/plugin/uart_plugin/docs/README.md)|
|UARTMON   |[README.md](sources/src/plugin/uartmon_plugin/docs/README.md)|
|BUSPIRATE |[README.md](sources/src/plugin/buspirate_plugin/docs/README.md)|
|CH347     |[README.md](sources/src/plugin/ch347_plugin/docs/README.md)|
|CP2112    |[README.md](sources/src/plugin/cp2112_plugin/docs/README.md)|
|FT232H    |[README.md](sources/src/plugin/ftdi232h_plugin/docs/README.md)|
|FT2232    |[README.md](sources/src/plugin/ftdi2232_plugin/docs/README.md)|
|FT245     |[README.md](sources/src/plugin/ftdi245_plugin/docs/README.md)|
|FT4232    |[README.md](sources/src/plugin/ftdi4232_plugin/docs/README.md)|
|HYDRABUS  |[README.md](sources/src/plugin/hydrabus_plugin/docs/README.md)|

