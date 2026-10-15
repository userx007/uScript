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

> The diagram above shows the original USB/serial-bridge plugin family in full
> for illustration; it omits, for space, the equally numerous CAN (`kvcan`,
> `pcan`, `slcan`), network (`tcpip`, `udp`, `mqtt`, `enc28J60`, `lan8720`,
> `w5500`, `raweth`), and kernel/USB-bridge I2C/SPI (`ki2c`, `kspi`,
> `dspki2c`, `dspkspi`, `ch341`) plugins, which plug into the same `IPlugin`
> interface alongside it. See [Available Plugins](#available-plugins) below
> for the complete, current list.

**Layers (top → bottom):**

| Layer | Components |
|---|---|
| **Entry Point** | `uScriptMainApp` — parses CLI, loads INI, configures logging, runs the validation pass then the execution pass |
| **Config** | `IniCfgLoader` / `uIniParser` / `uSharedConfig` — shared settings |
| **Script Core** | `ScriptClient` → `ScriptRunner` → `Reader` + `Validator` + `Interpreter` |
| **Comm Scripts** | Extends core with comm-specific runner, validator & interpreter |
| **Plugin Interface** | `IPlugin` — abstract contract all plugins implement, loaded dynamically |
| **Plugins** | One plugin per device or protocol: USB/serial bridges (BusPirate, Hydrabus, CH347, CH341, CP2112, FTDI×4, UART), CAN (KVCAN, KCAN, PCAN, SLCAN, Candlelight, SYSTEC, Vector), network (TCP/IP, UDP, WebSocket, MQTT, Modbus TCP, gRPC, DDS, DDS_TYPED, ENC28J60, LAN8720, W5500, RawEth, Vector Ethernet), fieldbus (PROFIBUS), kernel/USB I2C & SPI (KI2C, KSPI, DSPKI2C, DSPKSPI)… |
| **Plugin common code** | `plugins/common` — `uCommandExec.hpp` (shared `CMD` / `SCRIPT` / `CYCLIC` bodies used by every comm-driver plugin) and `uPluginSettings.hpp` (ini/`CONFIG` binding) |
| **Protocol libraries** | `libs/protocols` — pure protocol logic with no I/O: CAN transport protocols (ISO-TP, J1939, CANopen SDO, NMEA 2000 fast-packet), Modbus, MQTT, gRPC, PROFIBUS FDL, DDS IDL |
| **Shell Plugin** | Wraps `uShell` — full interactive shell with its own user plugin system |
| **Drivers** | `libs/drivers` — low-level OS/hardware/network wrappers (Linux/Windows) implementing `ICommDriver`, called by the plugins |
| **Utils** | `libs/utils` — cross-cutting: Logger, Timer, ArgParser, INI parser, string/hex helpers, expression/boolean/`MATH` evaluators, plugin loader, GUI notification layer |
| **GUI front-end** | `scriptgui` — optional Qt 6 application (`ScriptFrontend`) that edits, runs and visualises scripts by driving the `uscript` executable |
| **Testers** | `testers` — loopback bridges, a Modbus TCP test server and other tools for exercising plugins without hardware |

---

## Repository Structure

```
sources/
├── CMakeLists.txt, cmake/        top-level build (C++20; GCC/Clang, MinGW-w64 and MSVC)
├── include/                      public abstract interfaces
│   ├── driver/inc/               ICommDriver.hpp, ICommDumpProtocol.hpp, DdsTypePluginAbi.h
│   ├── plugin/inc/               IPlugin.hpp, IPluginDataTypes.hpp
│   └── script/
│       ├── core/inc/             IScript*.hpp               — core interpreter interfaces
│       ├── comm/inc/             ICommScript*.hpp           — comm interpreter interfaces
│       └── shell/inc/            IScriptInterpreterShell.hpp
└── src/
    ├── app/src/                  uScriptMainApp.cpp         — executable entry point (`uscript`)
    ├── config/inc/               uSharedConfig.hpp, uSharedScriptRegex.hpp — global constants / grammar fragments
    ├── scripts/
    │   ├── core/                 Core Script interpreter, validator, data types, client
    │   ├── comm/                 Comm Script interpreter, validator, data types, client
    │   └── shared/               reader (INCLUDE), runner, syntax (statement grammar), volatile macro store
    ├── libs/
    │   ├── drivers/              ICommDriver implementations (one directory per transport/device)
    │   ├── protocols/            cantp, dds, grpc, modbus, mqtt, profibus
    │   ├── parsers/kvparser/     key=value parser used by CONFIG commands
    │   ├── plugin_ops/inc/       PluginExport.hpp, PluginOperations.hpp, PluginSetup.hpp — plugin scaffolding
    │   └── utils/inc/            logger, timer, ini loader, string/hex/file helpers, evaluators, plugin loader …
    ├── plugins/                  one directory per plugin (inc/, src/, docs/) + common/ and template_plugin/
    ├── scriptgui/                Qt 6 GUI front-end (ScriptFrontend)
    └── testers/                  loopback / test-server tools and helper scripts
```

**Drivers** (`libs/drivers`): `candlelight`, `ch341`, `ch347`, `cp2112`, `dds`, `dds_typed`, `digispark` (I2C/SPI bridges), `enc28j60`, `ftdi2xx` (FT232H/FT2232/FT4232/FT245), `grpc`, `hydrabus`, `kcan`, `ki2c`, `kspi`, `kvcan`, `lan8720`, `modbus`, `mqtt`, `pcan`, `profibus`, `raweth`, `slcan`, `systec`, `tcpip`, `uart`, `udp`, `vector`, `w5500`, `websocket`.

**Plugins** (`src/plugins`, each built as `lib<name>_plugin.so` / `.dll`): `buspirate`, `candlelight`, `ch341`, `ch347`, `cp2112`, `dds`, `dds_typed`, `dspki2c`, `dspkspi`, `enc28J60`, `ftdi2232`, `ftdi232h`, `ftdi245`, `ftdi4232`, `grpc`, `hydrabus`, `kcan`, `ki2c`, `kspi`, `kvcan`, `lan8720`, `modbus`, `mqtt`, `pcan`, `profibus`, `raweth`, `shell`, `slcan`, `systec`, `tcpip`, `uart`, `uartmon`, `udp`, `vector`, `vector_eth`, `w5500`, `websocket` — plus `template_plugin` (skeleton for new plugins; `create_plugin.sh` generates one) and `common` (shared code, not a plugin).

---

## Application Entry Point

The framework is launched from `uScriptMainApp.cpp` as a command-line tool:

```
uscript  [-s | --script <path>]  [-c | --inicfg <path>]  [-l | --loglevel <N>]
         defaults: script.txt   config.ini
```

| Option | Default | Meaning |
|--------|---------|---------|
| `-s`, `--script` | `script.txt` | Core script to run |
| `-c`, `--inicfg` | `config.ini` | INI configuration file |
| `-l`, `--loglevel` | *(from ini)* | Console log severity (`0`=WERBOSE, `1`=VERBOSE … `7`=FIXED); overrides `LOG_SEVERITY_CONSOLE` from the ini |

At startup the app:
1. Parses the CLI arguments.
2. Loads the INI file and applies the `[LOGGING]` section (severity, file output, colours, date, thread id).
3. Constructs a `ScriptClient` with the script path and the pre-loaded `IniCfgLoader`.
4. Runs the script in **two passes** — `execute(false)` (read → validate → dry-run) and, only if that succeeds, `execute(true)` (real execution).
5. Exits with `0` on success or `1` on any failure.

Two environment variables are used by the GUI front-end and are normally unset when running from a terminal:

| Variable | Effect |
|----------|--------|
| `SCRIPT_GUI_MODE` | Switches on the `GUI:` line protocol on stdout (execution position, errors, comm dumps…) and unbuffers stdout |
| `SCRIPT_STOP_FLAG_FILE` | Path of a marker file; when it appears the interpreter stops gracefully between top-level statements |

### Global Configuration — `config.ini`

```ini
[LOGGING]
LOG_SEVERITY_CONSOLE  = 3       ; console threshold (see --loglevel)
LOG_SEVERITY_FILE     = 4       ; log-file threshold
LOG_FILE_ENABLED      = true
LOG_CONSOLE_COLORED   = true
LOG_INCLUDE_DATE      = false
LOG_INCLUDE_THREAD_ID = false

[SCRIPT]
CMD_EXEC_DELAY        = 50      ; ms pause after every real plugin command (default 0)

[UART]                          ; one section per plugin (or per instance: [UART:1])
FAULT_TOLERANT        = false   ; true: a failing command is logged but does not abort the script
PRIVILEGED            = false   ; true: plugin gets a handle to the interpreter
ARTEFACTS_PATH        =         ; base directory for file operands; default = the script's directory
RAW_RESULT            = false   ; CMD returns raw bytes instead of hex (comm-driver plugins)
CYCLIC_CACHED         = true    ; CYCLIC: parse entries once (true) or re-resolve every tick (false)
; ...plus the plugin's own keys, forwarded to its setParams()
```

A `[COMMON]` section is also loaded and handed to the script client; it currently carries no keys of its own.

Plugins are loaded from the `splugins/` directory next to the executable as `lib<name>_plugin.so` (`lib<name>_plugin.dll` on Windows, `<name>` in lower case); `iplugins/` holds the shell's own plugins (`lib<name>_iplugin.so`).

---

## Core Script Interpreter

The **Core Script Interpreter** is the main automation engine. It reads a text script, validates it statically, then executes it with a two-pass model: a dry-run for argument validation and a real pass for actual execution.

### Key Concepts at a Glance


#### Loading plugins

| Concept | Syntax | Description |
|---------|--------|-------------|
| Load plugin | `LOAD_PLUGIN NAME [op vX.Y.Z.W]` | Register a shared-library plugin |
| Load plugin instance | `LOAD_PLUGIN NAME:N [op vX.Y.Z.W]` | Register an independent, additional instance of the same plugin type (e.g. two UART ports); auto-instantiated on first use if not declared explicitly |



#### Macros (variables)

| Concept | Syntax | Description |
|---------|--------|-------------|
| Constant macro | `NAME := value` | Validation-time text substitution |
| Variable macro | `name ?= PLUGIN.COMMAND [params]` | Capture a command's return value at runtime |
| Array macro | `NAME [= e0, e1, e2` | Ordered list of strings; elements accessed via `$NAME.$index` (variable), `$NAME.N` (constant), or `$NAME.SIZE` (element count) |



#### Commands 

| Concept | Syntax | Description |
|---------|--------|-------------|
| Command | `PLUGIN.COMMAND [params]` | Dispatch a command to a plugin |
| Command to an instance | `PLUGIN:N.COMMAND [params]` | Dispatch to a specific loaded instance of that plugin type |
| Threaded command | `PLUGIN.COMMAND [params] &` | Run the command in a background thread; the script continues immediately (one thread per plugin instance) |
| Threaded capture | `name ?= PLUGIN.COMMAND [params] &` | Background thread that keeps refreshing the *volatile macro* `$name`; stoppable with `KILL name` |



#### Jumps

| Concept | Syntax | Description |
|---------|--------|-------------|
| Conditional jump | `IF expr GOTO label` | Skip forward to `LABEL` if expr is true |
| Unconditional jump | `GOTO label` | Always skip to `LABEL` |
| Label | `LABEL name` | Jump target for `GOTO` |



#### Loops

| Concept | Syntax | Description |
|---------|--------|-------------|
| Counted loop | `REPEAT label N` … `END_REPEAT label` | Execute body for every integer in `[0, N)` |
| Ranged loop | `REPEAT label begin, end[, step]` … `END_REPEAT label` | Execute body for every value in `[begin, end)`, stepping by `step` (default `1`, may be negative or floating-point) |
| Counted/ranged loop + index | `idx ?= REPEAT label …` … `END_REPEAT label` | As above; `$idx` holds the current range value inside the body |
| Conditional loop | `REPEAT label UNTIL cond` … `END_REPEAT label` | Execute body until `cond` is TRUE (do-while: body runs at least once) |
| Conditional loop + index | `idx ?= REPEAT label UNTIL cond` … `END_REPEAT label` | As above; `$idx` counts iterations from 0 |
| Break loop | `BREAK label` | Exit the named enclosing loop immediately |
| Continue loop | `CONTINUE label` | Skip to `END_REPEAT` of the named enclosing loop |



#### Native statements (no plugin required)

| Concept | Syntax | Description |
|---------|--------|-------------|
| Print | `PRINT [text]` | Log a line, expanding `$macros` |
| Delay | `DELAY value unit` | Pause execution (`us`, `ms`, or `sec`) |
| Format | `name ?= FORMAT items \| pattern` | Build a string from `%N`-indexed values |
| Math | `name ?= MATH expression [\| HEX…]` | Evaluate an arithmetic/boolean/ternary expression; the optional `\| HEX_<width>_<LE\|BE>` / `HEX_FLOAT_*` / `HEX_DOUBLE_*` suffix renders the result as fixed-width hex |
| Bit/byte packing | `name ?= BITSTREAM off:len:val ...` / `BYTESTREAM byte_off:len:val ...` | Pack numeric fields into a hex-encoded byte buffer |
| Bit/byte extraction | `name ?= hex_source \| BITSTREAMVAL bit_off:size` / `BYTESTREAMVAL byte_off:bit_off:size` | Read one field back out of a hex buffer (inverse of the row above) |
| Bit/byte extraction (array) | `name [= hex_source \| BITSTREAMVAL bit_off1:size1 ...` / `BYTESTREAMVAL byte_off1:bit_off1:size1 ...` | Same as above, but read one-or-more fields in a single statement into an array macro |
| Breakpoint | `BREAKPOINT [label]` | Pause and wait for operator input |
| Generator | `name ?= GENERATOR count unit begin:end:step[:k] \| WAVEFORM [\| HEX…]` (or an element list / `$array` instead of the range) | Start a background signal generator (`SAWTOOTH`/`LINEAR`, `TRIANGLE`, `SINE`, `SQUARE`, `EXP`, `LOG`, `RANDOM`) that writes a new value into `$name` every tick; re-running it for the same name restarts it |
| Kill | `KILL name` / `result ?= KILL name` | Stop the background thread or generator that keeps the volatile macro `name` alive (`name ?= PLUGIN.CMD args &` or `name ?= GENERATOR …`); `result` receives the number of threads stopped. The only way to stop one early — there is no `GENERATOR STOP` / `STOP ALL` |

See [`SCRIPTING_LANGUAGE_REFERENCE.md`](SCRIPTING_LANGUAGE_REFERENCE.md) for the complete, authoritative syntax of every native statement, macro form, and validation rule.



#### Multiline declarations

| Concept | Syntax | Description |
|---------|--------|-------------|
| Line continuation | trailing `\` | Join the physical line with the next; used to split long array declarations |
| Include | `INCLUDE "path"` | Splice another script file in (path relative to the including file); included lines are placed ahead of the including file's own lines; circular or duplicate includes are rejected |



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

Array element access — three forms: `$NAME.$index` (variable index, resolved
through another macro), `$NAME.N` (a constant index literal), and
`$NAME.SIZE` (element count). See `SCRIPTING_LANGUAGE_REFERENCE.md` §4 for the
fatal-vs-non-fatal out-of-range distinction between the constant and variable
forms.

```
# $i is a loop index macro; PORTS[i] is retrieved at runtime
i  ?=  REPEAT  open_ports  $PORTS.SIZE
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

**Emulating `IF / ELSIF / ELSE`:** there's no dedicated keyword for it, but
the pattern is a straightforward extension of `IF … GOTO` — guard each
branch with its **negated** condition and end every branch but the last with
an unconditional jump to a shared end label:

```
status  ?=  SENSOR.READ_STATUS

IF  EVAL  $status != "OK"    GOTO  not_ok
    LOG.PRINT  branch: OK
    GOTO  end_if
LABEL  not_ok

IF  EVAL  $status != "WARN"  GOTO  not_warn
    LOG.PRINT  branch: WARN
    GOTO  end_if
LABEL  not_warn

# else
LOG.PRINT  branch: ERROR

LABEL  end_if
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

For array access, `$NAME.$index` and `$NAME.N` both look `NAME` up in
`mapArrayMacros`; the former resolves `index` through the same three-tier
chain above, while the latter uses a literal decimal baked into the script
text (and aborts execution if out of range, vs. a logged-and-continued error
for the variable form). `$NAME.SIZE` resolves to the element count. If the
retrieved element itself contains further `$macro` references, those are
resolved too, on every access — an array element is not a frozen literal.

Constant macros (`:=`) and the array declaration structure itself (`[=`, i.e.
which names are arrays and how many elements each has) are fixed at
**validation time**; only an individual element's `$macro` content, if any, is
re-resolved at runtime, on each access. The one exception is
`name [= hex_source | BITSTREAMVAL/BYTESTREAMVAL ...` (see
[`SCRIPTING_LANGUAGE_REFERENCE.md`](SCRIPTING_LANGUAGE_REFERENCE.md) §8.8):
its element *count* is still fixed at validation time, but the element
*values* are computed when the statement executes, like any other native
statement — not frozen at validation time the way a literal `[=` list is.

### Architecture Summary

The pipeline consists of five collaborating classes:

```
ScriptClient  →  ScriptRunner  →  ScriptReader
                              →  ScriptValidator  →  ScriptCommandValidator
                              →  ScriptInterpreter
```

`ScriptReader` expands `INCLUDE` directives, strips comments and joins continued lines, and produces a `vector<ScriptRawLine>` — each entry carries the **1-based source line number** alongside the content string, enabling every downstream component and any future frontend to map compiled IR nodes back to their exact file position. Multi-line `\` continuations are resolved in the reader; the logical line retains the line number of its first physical line.

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
- A **runtime variable-macro map** (`unordered_map<string, string>`) holding the most recently *executed* value of every `?=` macro. It is guarded by a mutex because background threads (threaded captures, `GENERATOR`) write to it concurrently with the main thread.
- **Background-thread tables** — `std::jthread`s for `&` commands (one per plugin instance) and for generators (one per destination name, restart-on-collision), stopped by `KILL` or joined automatically at the end of the script.
- A **loop-anchored `DELAY` schedule** per `REPEAT`, so `DELAY` inside a loop compensates for the time spent elsewhere in the iteration (no cumulative drift).
- An **O(1) plugin command set index** (`unordered_map<string, unordered_set<string>>`) built once in `m_crossCheckCommands`.

`ScriptClient` drives the pipeline twice: `execute(false)` reads, validates and dry-runs; `execute(true)` runs for real. The statement grammar lives in `scripts/shared/syntax/uScriptSyntax.hpp` (shared with the GUI highlighter through `uSharedScriptRegex.hpp`); volatile macros are tracked in `scripts/shared/volatilemacrostore`.

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
| `INCLUDE` target missing/unreadable, circular, or included twice | Error |
| `MATH` `\| HEX…` suffix invalid (unsupported form, `HEX_8` with endianness, wider width without `_LE`/`_BE`) | Error |
| `GENERATOR` malformed, zero interval, bad numeric literal, `k` with a waveform other than `EXP`/`LOG`, array source with `SINE`/`SQUARE`/`EXP`/`LOG`, non-positive-integer `SQUARE` step, bad `HEX` encoding, or destination equal to a constant macro | Error |
| Second `&` command on a plugin instance that already has a live thread | Error (at run time) |
| `KILL` target is missing, is not a plain macro name (e.g. `KILL UART.CYCLIC`), or is not the destination of an earlier `name ?= PLUGIN.CMD … &` / `name ?= GENERATOR …` | Error |

📄 **Full documentation:** [README.md](sources/src/scripts/core/README.md)

---

## Comm Script Interpreter

The **Comm Script Interpreter** is a companion system focused exclusively on protocol-level send/receive sequencing over any `ICommDriver`-derived transport. It shares the `ScriptReader` infrastructure with the Core system but uses a completely different syntax and has no plugin machinery of its own.

### Key Concepts at a Glance

| Symbol | Meaning | Example |
|--------|---------|---------|
| `>` | Send, then optionally receive | `> 'AT\r\n' \| T'OK'` |
| `<` | Receive, then optionally send | `< T'login:' \| 'admin\r\n'` |
| `!` | Delay (`us`, `ms`, `sec`) | `! 200 ms` |
| `@` | Print a message to the log at INFO (no I/O) | `@ Starting update` |
| `NAME := value` | Constant macro, used as `$NAME` | `BAUD := 115200` |
| `~ param` | Optional channel/address override passed to the driver | `> H'AA' \| H'06' ~ 0x123` |

Data in each expression is annotated with a **decorator** that determines encoding and matching strategy. Decorators are **single-quoted**: a one-letter prefix directly followed by `'…'`.

| Decorator | Type | Send | Receive |
|-----------|------|:----:|:-------:|
| `'…'` | Delimited string | ✓ | ✓ (exact compare) |
| *(none)* | Raw string | ✓ | ✓ |
| `H'…'` | Hex byte stream | ✓ | ✓ (exact compare) |
| `R'…'` | Regex pattern | — | ✓ (match) |
| `T'…'` | String token | — | ✓ (wait until found) |
| `X'…'` | Hex token | — | ✓ (wait until found) |
| `L'…'` | Newline-delimited line | ✓ | ✓ |
| `S'…'` | Exact byte count | — | ✓ |
| `F'…'` | File (chunked): `F'path'`, `F'path,chunk'`; receive: `F'path,size[,chunk]'` | ✓ | ✓ (to file) |

- **Escapes** in strings: `\r` (0x0D), `\n` (0x0A) and `\\` (one backslash) are expanded; any other backslash is kept literally.
- **“Receive whatever arrives”**: a bare `<`, or `> expr |` with nothing after the pipe. The read ends when data arrives or the read timeout elapses; a timeout with no data is a normal, empty result.
- **`~ xtra`**: text after an unquoted `~` is an *xtra parameter* handed to the driver's read/write (for example a CAN id, an I2C address, a destination). `~ p` applies to both operations; `~ p1 | p2` gives the send side `p1` and the receive side `p2` (either may be empty = driver default).
- A `|` or `~` inside a `"…"` span is data, not a separator.
- Comm script constants use `:=`; `INCLUDE` and `\` line continuation work as in Core scripts because the same `ScriptReader` is used.

> **Quoting note.** Earlier documentation showed `H"…"` / `"…"` (double-quote) decorators. The current grammar (`uSharedConfig.hpp`, `uCommScriptCommandValidator.hpp`) uses single quotes, which also lets JSON bodies with `"` pass through unchanged (e.g. `GRPC.CMD > 'CALL svc/Method {"a":1}'`).

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

📄 **Full documentation:** [README.md](sources/src/scripts/comm/README.md)

---

## Shared Infrastructure

Both interpreter systems share the following components.

### `ScriptReader`

Header-only (`uScriptReader.hpp`). Reads a script file line by line and produces a `vector<ScriptRawLine>`. Each `ScriptRawLine` carries:
- `iLineNumber` — the **1-based line number** in the file the line came from, preserved through comment stripping, continuation joining and `INCLUDE` expansion.
- `strContent` — the cleaned line content.

The reader:
- strips `#` line comments (and inline trailing `# …`) and `---` / `!--` block comments (non-nestable);
- trims leading and trailing whitespace and drops empty lines;
- joins **continued lines** — a line ending in `\` is joined with the next physical line (repeatedly); the logical line keeps the line number of the first physical line;
- expands **`INCLUDE "path"`** directives recursively: the path is resolved relative to the including file, included lines are placed *ahead of* the including file's own lines, and circular or duplicate includes are reported as errors.

### `ScriptRunner<TScriptEntries>`

Header-only template (`uScriptRunner.hpp`). Orchestrates the pipeline stages. `runScript(ctx, bRealExec, bUseDryRun)` performs **read → validate → dry interpret** when `bRealExec` is `false`, and only **interpret** when it is `true` (the stored, already-validated IR is reused). `CommScriptRunner` extends it to carry a typed reference to the comm interpreter.

### Syntax and macro helpers (`scripts/shared`)

- `syntax/uScriptSyntax.hpp` — the single definition of the Core statement grammar (`usyntax::m_is…` predicates). Its regex fragments come from `config/inc/uSharedScriptRegex.hpp`, which the GUI highlighter shares so both sides recognise the same syntax.
- `volatilemacrostore/` — `uvolatile::VolatileMacroStore`, the registry through which `*.CYCLIC` re-resolves `$volatile` references written by background threads.

### `uSharedConfig.hpp`

The single source of truth for all string constants and magic values used across the framework:

| Category | Examples |
|----------|---------|
| File defaults | `script.txt`, `config.ini` |
| Comment markers / keywords | `#`, `---`, `!--`, `INCLUDE`, `TRUE` / `FALSE` |
| Separators | `:=`, `?=`, `[=`, `.`, ` ` (space); threaded marker `&` |
| Macro marker | `$` |
| Plugin paths and names | `splugins/`, `iplugins/`, `lib` prefix, `_plugin.so` / `_plugin.dll`, `_iplugin.so`; entry points `pluginEntry` / `pluginExit` (`uShellPluginEntry` / `uShellPluginExit` for shell plugins) |
| Ini sections and keys | `COMMON`, `SCRIPT` (`CMD_EXEC_DELAY`), `LOGGING` (`LOG_SEVERITY_CONSOLE`, `LOG_SEVERITY_FILE`, `LOG_INCLUDE_DATE`, `LOG_CONSOLE_COLORED`, `LOG_FILE_ENABLED`, `LOG_INCLUDE_THREAD_ID`), plugin keys `FAULT_TOLERANT`, `PRIVILEGED` |
| Comm decorator prefixes | `F'`, `R'`, `H'`, `T'`, `X'`, `L'`, `S'`, plain `'` (end marker `'`) |
| Time units | `us`, `ms`, `sec` |
| Default sizes | receive buffer 1024 B, file chunk 1024 B |

### `IniCfgLoader`

Loads `config.ini` (or the `-c` path) at startup. `[LOGGING]` controls logging, `[SCRIPT]` supplies `CMD_EXEC_DELAY`. Every other section name is matched to a plugin (or plugin instance, `[UART:1]`) and its key-value pairs are forwarded to that plugin's `setParams()`. `ARTEFACTS_PATH` is filled in with (or resolved against) the main script's directory.

---

## Plugin System

Plugins are independently compiled shared libraries placed in the `splugins/` directory next to the executable, named `lib<name>_plugin.so` / `lib<name>_plugin.dll` (`<name>` in lower case; an instance suffix such as `UART:1` maps to the same library). The interpreter discovers them by name, loads them with `dlopen` / `LoadLibrary`, and resolves two C entry points:

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

`doDispatch(cmd, params, stop_token)` receives a `std::stop_token`. For a threaded command (trailing `&`) it is a live token the plugin must poll if it can run indefinitely; for a normal call it is a default token that never fires.

### Common command set of the comm-driver plugins

Plugins whose transport implements `ICommDriver` (UART, KI2C/KSPI, DSPKI2C/DSPKSPI, CH341, KVCAN, KCAN, PCAN, SLCAN, Candlelight, SYSTEC, Vector, TCP/IP, UDP, WebSocket, MQTT, Modbus, gRPC, DDS, DDS_TYPED, PROFIBUS, RawEth, ENC28J60, LAN8720, W5500 …) share one implementation of the main commands (`plugins/common/commandexec`), so they behave identically:

| Command | Syntax | Behaviour |
|---------|--------|-----------|
| `INFO` | `PLUGIN.INFO` | Print version, commands, usage and the plugin's ini keys |
| `CONFIG` | `PLUGIN.CONFIG key=value …` | Change a subset of the settings at runtime (omitted keys keep their value). Common tokens: `raw=` (`RAW_RESULT`), `cached=` (`CYCLIC_CACHED`) plus transport-specific ones |
| `CMD` | `PLUGIN.CMD > send \| expect` / `< expect \| send` | One Comm Script expression against the plugin's driver. With `name ?= PLUGIN.CMD …` the bytes **received** are captured (hexlified; raw with `raw=1`) |
| `SCRIPT` | `PLUGIN.SCRIPT file.txt [\|delay]` | Run a Comm Script file over the same connection; relative paths resolve against `ARTEFACTS_PATH` |
| `CYCLIC` | `PLUGIN.CYCLIC time_ms:cmd [, time_ms:cmd …] [&]` | Send entries periodically (see below) |

**`CYCLIC`.** Each `cmd` is a complete `CMD` argument. All entries share one master tick (the gcd of their periods); each is sent when its period has elapsed, on a drift-free absolute-time schedule. A comma inside a `'…'`/`"…"` value does not end an entry. Without `&` it runs one *super-period* (the lcm of the periods) and returns; with `&` it runs until stopped (`KILL`, or end of script). `CYCLIC_CACHED = true` (default) parses each entry once per session; `false` re-resolves `$volatile` macros inside entries on every tick, so an entry can follow a value that a `GENERATOR` or another threaded command keeps updating.

```
UART.CYCLIC  100:> H'AABBCCDD', 250:> 'ping\r\n'           # one 500 ms pattern, then returns
tick  ?=  UART.CYCLIC  1000:> 'hello'  &                    # forever, in the background
KILL  tick
```

Plugins whose transport has no `CMD`/`SCRIPT`/`CYCLIC` (the multi-protocol adapters such as BUSPIRATE, CH347, CP2112, FTDI, HYDRABUS) expose their own per-protocol command sets, described below.

---

## Available Plugins

> **Note:** `PRINT`, `DELAY`, `MESSAGE`/`PRINT`, `BREAKPOINT`, `FORMAT`, `MATH`,
> `BITSTREAM`/`BYTESTREAM`, `BITSTREAMVAL`/`BYTESTREAMVAL` (including their
> array forms), `GENERATOR`, `EVAL`, and `KILL` are **native language statements**
> handled directly by the interpreter — there is no `CORE` plugin to load for
> them, and no `LOAD_PLUGIN` line is needed. See
> [`SCRIPTING_LANGUAGE_REFERENCE.md`](SCRIPTING_LANGUAGE_REFERENCE.md) for
> their full syntax. Every plugin below, by contrast, does require an explicit
> `LOAD_PLUGIN` (or an auto-instantiated `NAME:N`) before its commands can be used.
>
> `EVAL` string operands that contain spaces must be double-quoted (e.g.
> `EVAL "Hello World" == "Hello World"`), and any text left over after the
> last operand/type hint that isn't `&&`/`||` now makes the whole `EVAL`
> fail rather than being silently ignored — see
> [`SCRIPTING_LANGUAGE_REFERENCE.md` §10](SCRIPTING_LANGUAGE_REFERENCE.md#10-eval-expression-evaluator).

### `SHELL` — Interactive Shell Session
Launches an interactive **Microshell** terminal from within a running script. The shell is **privileged**: it receives a live reference to the interpreter and can load plugins, list macros, and dispatch commands in real time. Script execution resumes normally when the operator exits the shell.

| Commands | Purpose |
|----------|---------| 
| `INFO` | Print plugin info |
| `RUN` | Block and launch the interactive shell session |

Inside the shell, the `.` shortcut bridges to the script layer (e.g. `. PLUGINNAME.COMMAND args`, `.. PLUGINNAME` to load a plugin).

📄 **Full documentation:** [README.md](sources/src/plugins/shell_plugin/docs/README.md)


---

### `UART` — Serial Port (with Comm Script Support)
Drives a UART serial port. Supports inline command expressions using the same Comm Script decorator syntax, or delegates to a full `.txt` Comm Script file.

| Commands | Purpose |
|----------|---------| 
| `INFO` | Print plugin info |
| `CONFIG p=port b=baud r=rtout w=wtout s=size` | Configure the port |
| `CMD > '...' \| T'...'` | Single inline send/receive expression |
| `SCRIPT path.txt` | Execute a full Comm Script file against this port |
| `CYCLIC time:cmd, …` | Periodic sends (see [Common command set](#common-command-set-of-the-comm-driver-plugins)) |

📄 **Full documentation:** [README.md](sources/src/plugins/uart_plugin/docs/README.md)


---

### `UARTMON` — Serial Port Monitor
Background-thread monitor that detects serial port insertion and removal events. Useful as a first step in an automation sequence that must wait for a device to enumerate.

| Commands | Purpose |
|----------|---------| 
| `INFO` | Print plugin info |
| `CONFIG` | Configure the monitor (e.g. polling interval) |
| `LIST_PORTS` | Log all currently present serial ports (result via `?=`) |
| `START` | Start the background monitor thread |
| `STOP` | Stop the background monitor thread |
| `WAIT_INSERT [timeout_ms]` | Block until a new port appears (result via `?=`) |
| `WAIT_REMOVE [timeout_ms]` | Block until a port disappears |

📄 **Full documentation:** [README.md](sources/src/plugins/uartmon_plugin/docs/README.md)


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

📄 **Full documentation:** [README.md](sources/src/plugins/buspirate_plugin/docs/README.md)


---

### `CH347` — WCH CH347 USB Hi-Speed Adapter
Direct USB driver (no serial port) for the CH347 chip. All four interfaces can be **open simultaneously** on one device. Supports external Comm Script files for each interface.

| Interface | Key Commands |
|-----------|-------------|
| `SPI` | `open`, `close`, `write`, `read`, `wrrd`, `cs`, `script` |
| `I2C` | `open`, `close`, `scan`, `write`, `read`, `wrrd`, `script` |
| `GPIO` | `open`, `close`, `set`, `read` |
| `JTAG` | `open`, `close`, `write ir/dr`, `read ir/dr`, `reset` |

📄 **Full documentation:** [README.md](sources/src/plugins/ch347_plugin/docs/README.md)


---

### `CP2112` — Silicon Labs CP2112 USB-HID Bridge
USB-HID based I²C/SMBus and GPIO controller. All communication goes through 64-byte HID reports; the plugin handles chunking transparently.

| Interface | Key Commands |
|-----------|-------------|
| `I2C` | `open`, `close`, `scan`, `write`, `read`, `wrrd`, `script` |
| `GPIO` | `open`, `close`, `set`, `read`, `dir` |

📄 **Full documentation:** [README.md](sources/src/plugins/cp2112_plugin/docs/README.md)


---

### `FT232H` — FTDI FT232H Single-Channel Hi-Speed Adapter
Single MPSSE channel; SPI, I2C, and GPIO share the channel and cannot be open simultaneously. UART operates in VCP mode, exclusive with MPSSE modes.

| Interface | Key Commands |
|-----------|-------------|
| `SPI` | `open`, `close`, `write`, `read`, `wrrd`, `cs`, `script` |
| `I2C` | `open`, `close`, `scan`, `write`, `read`, `wrrd`, `script` |
| `GPIO` | `open`, `close`, `set low/high`, `read` |
| `UART` | `open`, `close`, `cfg`, `script` |

📄 **Full documentation:** [README.md](sources/src/plugins/ftdi232h_plugin/docs/README.md)


---

### `FT2232` — FTDI FT2232H / FT2232D Dual-Channel Adapter
Two hardware variants in one binary selected by `variant=H|D`. The H variant provides two MPSSE channels (A and B) with a 60 MHz clock; the D variant has one MPSSE channel and a UART on channel B with a 6 MHz clock.

📄 **Full documentation:** [README.md](sources/src/plugins/ftdi2232_plugin/docs/README.md)


---

### `FT245` — FTDI FT245 Parallel FIFO Adapter
USB parallel FIFO bridge (no serial engine). Two variants: `BM` (async + sync FIFO) and `R` (async FIFO only). FIFO and GPIO are **mutually exclusive**.

| Interface | Key Commands |
|-----------|-------------|
| `FIFO` | `open`, `close`, `write`, `read` |
| `GPIO` | `open`, `close`, `set`, `read` |

📄 **Full documentation:** [README.md](sources/src/plugins/ftdi245_plugin/docs/README.md)


---

### `FT4232` — FTDI FT4232H Quad-Channel Adapter
Four independent channels; A and B are MPSSE (SPI, I2C, GPIO), C and D are async UART. All four can operate simultaneously on separate USB handles.

📄 **Full documentation:** [README.md](sources/src/plugins/ftdi4232_plugin/docs/README.md)


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

📄 **Full documentation:** [README.md](sources/src/plugins/hydrabus_plugin/docs/README.md)


---

### `CH341` — WCH CH341 USB-to-Serial Adapter
Simple USB-to-serial bridge, configured like a UART.

| Commands | Purpose |
|----------|---------|
| `INFO` | Print plugin info |
| `CONFIG p=port b=baudrate r=read_tout w=write_tout s=recv_bufsize` | (Re)configure at runtime |
| `CMD > '...' \| T'...'` | Single inline send/receive expression |
| `SCRIPT path.txt` | Execute a full Comm Script file |

📄 **Documentation:** no separate README in this snapshot — `PLUGIN.INFO` prints the full usage and ini keys.


---

### `KI2C` / `KSPI` — Linux Kernel I2C / SPI
Drive an I2C or SPI device directly through the Linux kernel device nodes
(`/dev/i2c-N`, `/dev/spidevN.N`) — no USB bridge chip involved.

| Commands | Purpose |
|----------|---------|
| `INFO` | Print plugin info |
| `CONFIG ...` | (Re)configure device path, address/mode, timeouts, buffer size at runtime |
| `CMD > '...' \| T'...'` | Single inline send/receive expression |
| `SCRIPT path.txt` | Execute a full Comm Script file |

📄 **Full documentation:** [README.md](sources/src/plugins/ki2c_plugin/docs/README.md) · [README.md](sources/src/plugins/kspi_plugin/docs/README.md)


---

### `DSPKI2C` / `DSPKSPI` — USB-Bridge I2C / SPI
Drive an I2C or SPI device through a VID/PID-addressed USB bridge (as opposed
to `KI2C`/`KSPI`'s direct kernel device-node access).

| Commands | Purpose |
|----------|---------|
| `INFO` | Print plugin info |
| `CONFIG v=vid p=pid ...` | (Re)configure VID/PID, address/mode, timeouts, buffer size at runtime |
| `SCAN` | *(DSPKI2C only)* Scan the I2C bus for responding addresses |
| `CMD > '...' \| T'...'` | Single inline send/receive expression |
| `SCRIPT path.txt` | Execute a full Comm Script file |

📄 **Documentation:** no separate README in this snapshot — `PLUGIN.INFO` prints the full usage and ini keys.


---

### `KVCAN` / `KCAN` / `PCAN` / `SLCAN` / `CANDLELIGHT` / `SYSTEC` / `VECTOR` — CAN Bus Adapters
CAN transports sharing a common command shape:

| Plugin | Transport |
|--------|-----------|
| `KVCAN` | Linux SocketCAN (CAN / CAN FD), including virtual `vcan0`; the interface is configured outside the plugin |
| `KCAN` | SocketCAN for *real* adapters — like `KVCAN` **plus** setting the bit rate / sample point / CAN FD data rate over rtnetlink, listen-only/loopback/one-shot modes, and `UP` `DOWN` `STATUS` `RESTART` (BUS-OFF recovery); needs `CAP_NET_ADMIN` only for changes |
| `PCAN` | PEAK-System PCAN-Basic SDK |
| `SLCAN` | Serial-line CAN adapters (SLCAN/CANable-style) |
| `CANDLELIGHT` | Native-USB gs_usb / Candlelight adapters (CANable, candleLight-fw), opened on demand over libusb |
| `SYSTEC` | SYS TEC USB-CANmodul via the `systec_can.ko` SocketCAN driver; adds `HWCTRL` (sysfs hardware control); classic CAN only |
| `VECTOR` | Vector Informatik XL-API interfaces (VN16xx, VN89xx, …) on Windows and Linux; `DEVICES` enumerates channels (`hw=`/`serial=`/`name=`) |

Frames larger than one CAN/CAN FD frame can use a **multi-frame transport protocol** selected with the `CAN_TP_PROTOCOL` ini key (`none`, `isotp`, `j1939`, `canopen`, `nmea2000`; tunables `TP_*`, `J1939_*`, `CANOPEN_*`), implemented in `libs/protocols/cantp`.

| Commands | Purpose |
|----------|---------|
| `INFO` | Print plugin info |
| `CONFIG i=iface x=tx_id r=read_tout w=write_tout s=recv_bufsize ...` | (Re)configure channel/id/timeouts at runtime |
| `FILTER id:mask [id:mask ...]` | Install RX acceptance filters |
| `CYCLIC time:cmd, …` | Periodic frames |
| `UP` / `DOWN` / `STATUS` / `RESTART` | *(KCAN)* Link control |
| `HWCTRL` | *(SYSTEC)* Hardware control via sysfs |
| `DEVICES` | *(VECTOR, VECTOR_ETH)* List the channels the driver reports |
| `CMD > '...' \| T'...'` | Single inline send/receive expression |
| `SCRIPT path.txt` | Execute a full Comm Script file |

📄 **Full documentation:** [KVCAN](sources/src/plugins/kvcan_plugin/docs/README.md) · [KCAN](sources/src/plugins/kcan_plugin/docs/README.md) · [PCAN](sources/src/plugins/pcan_plugin/docs/README.md) · [SLCAN](sources/src/plugins/slcan_plugin/docs/README.md) · [CANDLELIGHT](sources/src/plugins/candlelight_plugin/docs/README.md) · [SYSTEC](sources/src/plugins/systec_plugin/docs/README.md) · [VECTOR](sources/src/plugins/vector_plugin/docs/README.md)


---

### `RAWETH` — Raw Ethernet Frames
Sends and receives raw Ethernet frames under a configurable EtherType,
bypassing the IP stack entirely.

| Commands | Purpose |
|----------|---------|
| `INFO` | Print plugin info |
| `CONFIG i=iface d=dest_mac t=ethertype ...` | (Re)configure interface/destination/EtherType at runtime |
| `CMD > '...' \| T'...'` | Single inline send/receive expression |

📄 **Documentation:** no separate README in this snapshot — `PLUGIN.INFO` prints the full usage and ini keys.


---

### `TCPIP` / `UDP` — Network Client Sockets
TCP and UDP client transports, both configured with the same `h=host p=port`
style CONFIG grammar as the other network plugins.

| Commands | Purpose |
|----------|---------|
| `INFO` | Print plugin info |
| `CONFIG h=host p=port r=read_tout w=write_tout s=recv_bufsize` | (Re)configure endpoint/timeouts at runtime |
| `CMD > '...' \| T'...'` | Single inline send/receive expression |
| `SCRIPT path.txt` | Execute a full Comm Script file |

📄 **Documentation:** no separate README in this snapshot — `PLUGIN.INFO` prints the full usage and ini keys.


---

### `MQTT` — MQTT Publish/Subscribe Client
Publishes to and asserts acknowledgements from an MQTT broker over a
short-lived CONNECT/CONNACK session per command.

| Commands | Purpose |
|----------|---------|
| `INFO` | Print plugin info |
| `CONFIG h=host p=port q=qos t=tls r=retain ...` | (Re)configure broker/QoS/TLS at runtime |
| `CMD topic \| payload` | Publish (and assert acks for) one message |
| `SCRIPT path.txt` | Publish a sequence of messages from a script file |

📄 **Full documentation:** [README.md](sources/src/plugins/mqtt_plugin/docs/mqtt_plugin_tutorial.md)


---

### `ENC28J60` / `LAN8720` / `W5500` — Embedded Ethernet Controllers
Three SPI/RMII Ethernet PHY/MAC controllers commonly paired with
microcontrollers, exposed with the same network-plugin CONFIG shape as
`TCPIP`/`UDP`.

| Commands | Purpose |
|----------|---------|
| `INFO` | Print plugin info |
| `CONFIG i=iface p=port ...` | (Re)configure interface/endpoint at runtime |
| `CMD > '...' \| T'...'` | Single inline send/receive expression |

📄 **Documentation:** no separate README in this snapshot — `PLUGIN.INFO` prints the full usage and ini keys.


---

### `WEBSOCKET` — WebSocket Client
RFC 6455 client. A fresh connection and handshake to `host:port/path` is opened for each `CMD` and closed when it completes; every send is one Binary frame. Subprotocol selectable.

| Commands | Purpose |
|----------|---------|
| `INFO` | Print plugin info |
| `CONFIG h=host p=port u=path o=subprotocol c=connect_tout r=read_tout w=write_tout s=recv_bufsize` | (Re)configure endpoint at runtime |
| `CMD > '...' \| '...'` | Send/receive expression |
| `SCRIPT path.txt` | Execute a Comm Script file |
| `CYCLIC time:cmd, …` | Periodic sends |

ini keys: `WS_HOST`, `WS_PORT`, `WS_PATH`, `WS_SUBPROTOCOL`, `WS_CONNECT_TIMEOUT`, `WS_READ_TIMEOUT`, `WS_WRITE_TIMEOUT`, `WS_READ_BUFFER_SIZE`, `RAW_RESULT`, `CYCLIC_CACHED`. No separate README: `WEBSOCKET.INFO` prints the full usage.


---

### `MODBUS` — Modbus TCP Master
Modbus TCP client over one persistent connection (opened on first use). One request/response per `CMD`; there is no standalone `<` receive.

| Commands | Purpose |
|----------|---------|
| `INFO` | Print plugin info |
| `CONFIG h=host p=port rt=read_tout rb=read_bufsize` | (Re)configure server and timeouts |
| `CMD > FUNCTION unit address args… [\| expected]` | One request: `READ_COILS`, `READ_DISCRETE_INPUTS`, `READ_HOLDING_REGISTERS`, `READ_INPUT_REGISTERS`, `WRITE_SINGLE_COIL`, `WRITE_SINGLE_REGISTER`, `WRITE_MULTIPLE_COILS`, `WRITE_MULTIPLE_REGISTERS` |
| `SCRIPT path.txt` | Run several `CMD`-style lines over the same connection |
| `CYCLIC time:cmd, …` | Periodic requests |

```
MODBUS.CONFIG h=plc.local p=502
MODBUS.CMD > READ_HOLDING_REGISTERS 1 100 4 | 12,34,56,78
regs  ?=  MODBUS.CMD > READ_COILS 1 0 8
MODBUS.CMD > WRITE_SINGLE_REGISTER 1 10 1234 | OK
```

Reads return comma-separated values (bits as `0`/`1`, registers decimal); writes confirm with `OK`; an exception response fails the command with `EXCEPTION:<code>`. `unit_id` is always required. A self-contained test server lives in `testers/modbus_server`.


---

### `GRPC` — gRPC Client
Calls any gRPC method (unary, server-, client- and bidirectional-streaming) using a pre-built **descriptor set** — no generated stubs. JSON request/response bodies. Builds against `grpc++` (see `plugins/grpc_plugin/README.md` for dependencies).

| Commands | Purpose |
|----------|---------|
| `CONFIG h=host p=port d=descriptor.protoset [t=tls ca= crt= key= auth=bearer] [ct= xt= rt= rb=]` | Server, TLS, descriptor set (`protoc --descriptor_set_out=… --include_imports`) and timeouts |
| `CMD > 'CALL pkg.Service/Method {json}' \| R'pattern'` | Unary call; `> 'FINISH'` half-closes a stream; `< R'.*'` reads the next streamed message |
| `SCRIPT path.txt` | Run several lines over the same channel |
| `CYCLIC time:cmd, …` | Periodic unary polling |

Because the JSON contains `"`, wrap the whole `CALL …` text in single quotes. Only `R'pattern'` or an exact `'…'` match is supported on the response.


---

### `DDS` — DDSI-RTPS Publish/Subscribe
Speaks DDSI-RTPS through an embedded Eclipse Cyclone DDS participant, interoperable with OpenDDS, RTI Connext, FastDDS. Each topic is exchanged as one opaque `string` payload, matched purely by topic name — no IDL needed (NGVA / STANAG 4754 use case). Reliable QoS, IPv6, fragmentation supported.

| Commands | Purpose |
|----------|---------|
| `CONFIG d=domain i=ip n=name r=reliable v6=1 hd=depth fr=frag …` | Participant configuration |
| `CMD > SUBSCRIBE topic` / `> PUBLISH topic payload` / `<` | Subscribe, publish, block for the next sample |
| `SCRIPT`, `CYCLIC` | As for the other comm plugins |

📄 **Full documentation:** [README.md](sources/src/plugins/dds_plugin/docs/README.md) · [tutorial](sources/src/plugins/dds_plugin/docs/dds_plugin_tutorial.md)


---

### `DDS_TYPED` — Strongly-typed DDS
Like `DDS`, but publishes/subscribes **real IDL types** loaded at runtime from a customer `.so` built with Cyclone's `idlc` (stable C ABI in `include/driver/inc/DdsTypePluginAbi.h`); swapping customers needs no rebuild.

```
DDS_TYPED.CMD > LOAD ./libcustomer1_types.so
DDS_TYPED.CMD > PUBLISH vehicle/state id=1,label=truck-07,speed=27.5
DDS_TYPED.CMD > SUBSCRIBE vehicle/state
DDS_TYPED.CYCLIC 1000:> PUBLISH vehicle/state id=1,label=truck-07,speed=27.5
```

📄 **Full documentation:** [README.md](sources/src/plugins/dds_typed_plugin/docs/README.md) · [tutorial](sources/src/plugins/dds_typed_plugin/docs/dds_typed_plugin_tutorial.md) · [communication flow](sources/src/plugins/dds_typed_plugin/docs/dds_typed_plugin_communication_flow.md)


---

### `PROFIBUS` — PROFIBUS FDL Master / Monitor
PROFIBUS layer-2 (FDL) master and passive bus monitor over RS-485/UART (opened 8E1). No DP application layer and no token-ring participation; timing is software-approximated, so read the tutorial's limitations before using it on real field devices.

| Commands | Purpose |
|----------|---------|
| `CONFIG d=device b=baud a=own_address rt=response_tout hp=high_priority rb=bufsize` | Port and timing (`b` ∈ 9600, 19200, 500000, and platform-dependent 1.5M/3M) |
| `CMD > SDN\|SDA\|SRD station [hex] [\| expected]` / `> STATUS station` / `<` | FDL services; `<` is one passive-monitor read (`line ?= PROFIBUS.CMD < &` tracks the latest telegram) |
| `SCRIPT`, `CYCLIC` | As for the other comm plugins |

📄 **Full documentation:** [tutorial](sources/src/plugins/profibus_plugin/docs/profibus_plugin_tutorial.md)


---

### `VECTOR_ETH` — Vector Ethernet Interfaces
Vector XL-API Ethernet channels (VN5610/VN5650/VN7610…), sibling of `VECTOR`. Addressing is MAC + EtherType; same `DEVICES` / `CONFIG` / `FILTER` / `CMD` / `SCRIPT` / `CYCLIC` set. Windows uses the port-based Ethernet API, Linux the Network/virtual-switch API (PHY configuration is Windows-only in practice).

📄 **Full documentation:** [README.md](sources/src/plugins/vector_eth_plugin/docs/README.md)


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
| `IScriptRunner`, `IScriptCommandInterpreter`, `IScriptCommandValidator` | `include/script/core/inc/` | Pipeline orchestration and per-command interpret/validate |
| `ICommDumpProtocol` | `include/driver/inc/ICommDumpProtocol.hpp` | Flat record format of the GUI comm-dump panel (timestamp, plugin, direction, data) |
| `DdsTypePluginAbi.h` | `include/driver/inc/` | Stable C ABI between `DdsTypedDriver` and a customer's IDL-type `.so` |

---

## Libraries

| Library | Path | Purpose |
|---------|------|---------|
| Drivers | `libs/drivers/*` | `ICommDriver` implementations: serial (`uart`), USB bridges (`ch341`, `ch347`, `cp2112`, `ftdi2xx`, `digispark`, `hydrabus`), CAN (`kvcan`, `kcan`, `pcan`, `slcan`, `candlelight`, `systec`, `vector`), network (`tcpip`, `udp`, `websocket`, `raweth`, `enc28j60`, `lan8720`, `w5500`), kernel I2C/SPI (`ki2c`, `kspi`), and protocol drivers (`mqtt`, `modbus`, `grpc`, `dds`, `dds_typed`, `profibus`) |
| CAN transport protocols | `libs/protocols/cantp` | `isotp` (ISO 15765-2), `j1939tp` (BAM and RTS/CTS), `canopensdo`, `nmea2000fp` (fast-packet), with a factory/interface layer shared by the CAN plugins |
| Protocol libraries | `libs/protocols/{modbus,mqtt,grpc,profibus,dds}` | Encode/decode logic with no I/O |
| `kvparser` | `libs/parsers/kvparser` | Parses the `key=value` argument strings of `CONFIG` commands |
| `plugin_ops` | `libs/plugin_ops` | Plugin scaffolding: export macros, the command table (`PluginOperations.hpp`), settings binding (`PluginSetup.hpp`) |
| `utils` | `libs/utils/inc` | Logger, timer, ini parser/loader, string/hex/numeric/file helpers, `uCalculator` (MATH), `uBoolEvaluator`/`uExprEvaluator` (conditions, EVAL), plugin/shared-library loader, `uExecContext` (dry-run and stop flags), `uGuiNotify`, `uCheckContinue` (BREAKPOINT), `uUartMonitor` |

---

## GUI Front-End (`scriptgui`)

`ScriptFrontend` is an optional **Qt 6** application that wraps the `uscript` executable. It provides:

- editors with syntax highlighting for Core scripts, Comm scripts and ini files;
- **RUN** / stop, with a live execution bar showing the current line, the Comm Script being executed (one tab per background thread), outlined lines for running `&` threads, and red markers on validation errors;
- a log panel and a **comm-dump panel** that lists every Rx/Tx exchange (timestamp, plugin instance, direction, data);
- an embedded terminal for the `SHELL` plugin;
- toolbar buttons for the script folder's `config.ini` and `setup.sh` / `setup.bat` (with an elevated-rights prompt when the setup script needs `sudo` or admin rights).

It launches `uscript` with `-s` / `-c`, sets `SCRIPT_GUI_MODE` and `SCRIPT_STOP_FLAG_FILE`, and parses the line protocol the interpreter prints (`GUI:EXEC_MAIN:<line>`, `GUI:EXEC_COMM_T:<tid>:<line>`, `GUI:ERROR_MAIN:<line>`, `GUI:THREAD_START/DONE:<line>`, `GUI:COMM_DUMP:<base64>`, `GUI:SHELL_RUN/EXIT`, `GUI:LOG:<text>` …; see `libs/utils/inc/uGuiNotify.hpp`). A deployment bundles the Qt libraries, `uscript`, `splugins/` and `iplugins/` next to the front-end (details in `scriptgui/README.md`).

---

## Test Tools (`testers`)

| Tool | Purpose |
|------|---------|
| `crossloopback` (`loopback`) | One tool that mirrors a UART / SocketCAN / TCP / UDP / raw-Ethernet channel back to itself, or bridges any of them to any other, with an optional delay (`loopback -i <spec> [-o <spec>] [-t ms]`) |
| `modbus_server` | Self-contained Modbus TCP server (coils, discrete inputs, holding/input registers) for `MODBUS` |
| `simpleloopback`, `cantploopback` | Simple loopbacks, including CAN transport-protocol loopbacks |
| `kcan_link`, `multicast`, `uartmon` | CAN link helper, multicast sender/receiver, UART monitor demo |
| `scripts/*.sh` (and `kscripts/`) | Setup and run helpers: virtual CAN, PCAN/SYSTEC/Candlelight setup, `tnt` virtual serial pairs, TCP/UDP/raw-Ethernet/WebSocket loopbacks, CAN-TP scenarios |

---

## Quick-Start Example

### Core Script (`script.txt`)

```
# Load plugins
LOAD_PLUGIN  UARTMON
LOAD_PLUGIN  UART
LOAD_PLUGIN  CH347

# Constant macros
REQUIRED_FW   := 3.1.0.0
FLASH_PORTS   [=  /dev/ttyUSB0, /dev/ttyUSB1, /dev/ttyUSB2

# Wait for device to enumerate
UARTMON.START
new_port  ?=  UARTMON.WAIT_INSERT  10000
UARTMON.STOP

# Configure and probe the UART
UART.CONFIG  p=$new_port  b=115200  r=3000  w=3000

# Use Comm Script for the serial handshake
UART.SCRIPT  handshake.txt

# Verify firmware version — native EVAL, no plugin needed
fw_ver  ?=  UART.READ_LINE
IF  EVAL  $fw_ver >= $REQUIRED_FW |VER  GOTO  fw_ok
PRINT  Firmware $fw_ver too old — required $REQUIRED_FW
GOTO  abort
LABEL  fw_ok

# Flash every board in the array, using its element count as the loop bound
board  ?=  REPEAT  flash_loop  $FLASH_PORTS.SIZE
    PRINT  Flashing board $board via $FLASH_PORTS.$board
    CH347.SPI      open  clock=15000000  mode=0
    CH347.SPI      script  flash_sequence.txt
    CH347.SPI      close

    ok  ?=  CH347.SPI  verify              # replace with a real verify command
    IF  EVAL  $ok == TRUE  GOTO  flash_ok
    PRINT  Board $board flash FAILED — stopping
    BREAK  flash_loop
    LABEL  flash_ok
    PRINT  Board $board flash OK

END_REPEAT  flash_loop
GOTO  done

LABEL  abort
PRINT  Script aborted — firmware check failed

LABEL  done
```

### Comm Script (`handshake.txt`)

```
# Wait for boot banner
< R'.*Boot.*'

# Login
< T'login:'   | 'admin\r\n'
< T'Password:'| 'secret\r\n'
< '$ '

# Transfer firmware version query and validate response
> 'fw_version\r\n'   | L'v3.1.0'

! 200 ms
```

---

## Detailed Documentation Index

### Script Interpreters

| Document | Scope |
|----------|-------|
| Core | [README.md](sources/src/scripts/core/README.md) |
| Comm | [README.md](sources/src/scripts/comm/README.md) |
| Core scripting language — full syntax reference | [SCRIPTING_LANGUAGE_REFERENCE.md](SCRIPTING_LANGUAGE_REFERENCE.md) |
| Core scripting language — step-by-step tutorial | [SCRIPTING_LANGUAGE_TUTORIAL.md](SCRIPTING_LANGUAGE_TUTORIAL.md) |
| Native MATH expression syntax | [SCRIPTING_LANGUAGE_REFERENCE.md §8.4](SCRIPTING_LANGUAGE_REFERENCE.md#84-math) (`MATH_COMMAND_REFERENCE.md` is not part of this repository snapshot) |
| Script examples | [SCRIPT_EXAMPLES.md](SCRIPT_EXAMPLES.md) |

### Plugins

| Document | Scope |
|----------|-------|
| SHELL | [README.md](sources/src/plugins/shell_plugin/docs/README.md) |
| UART | [README.md](sources/src/plugins/uart_plugin/docs/README.md) |
| UARTMON | [README.md](sources/src/plugins/uartmon_plugin/docs/README.md) |
| BUSPIRATE | [I2C_scanning.md](sources/src/plugins/buspirate_plugin/docs/I2C_scanning.md) · [README.md](sources/src/plugins/buspirate_plugin/docs/README.md) |
| CH347 | [README.md](sources/src/plugins/ch347_plugin/docs/README.md) |
| CP2112 | [AT24C02.md](sources/src/plugins/cp2112_plugin/docs/AT24C02.md) · [PCA9554AD.md](sources/src/plugins/cp2112_plugin/docs/PCA9554AD.md) · [README.md](sources/src/plugins/cp2112_plugin/docs/README.md) |
| FT232H | [README.md](sources/src/plugins/ftdi232h_plugin/docs/README.md) |
| FT2232 | [README.md](sources/src/plugins/ftdi2232_plugin/docs/README.md) |
| FT245 | [README.md](sources/src/plugins/ftdi245_plugin/docs/README.md) |
| FT4232 | [README.md](sources/src/plugins/ftdi4232_plugin/docs/README.md) |
| HYDRABUS | [README.md](sources/src/plugins/hydrabus_plugin/docs/README.md) |
| KI2C | [README.md](sources/src/plugins/ki2c_plugin/docs/README.md) |
| KSPI | [README.md](sources/src/plugins/kspi_plugin/docs/README.md) |
| KVCAN | [README.md](sources/src/plugins/kvcan_plugin/docs/README.md) |
| KCAN | [INSTALL.md](sources/src/plugins/kcan_plugin/docs/INSTALL.md) · [README.md](sources/src/plugins/kcan_plugin/docs/README.md) |
| PCAN | [INSTALL.md](sources/src/plugins/pcan_plugin/docs/INSTALL.md) · [README.md](sources/src/plugins/pcan_plugin/docs/README.md) |
| SLCAN | [README.md](sources/src/plugins/slcan_plugin/docs/README.md) |
| CANDLELIGHT | [README.md](sources/src/plugins/candlelight_plugin/docs/README.md) |
| SYSTEC | [README.md](sources/src/plugins/systec_plugin/docs/README.md) |
| VECTOR | [README.md](sources/src/plugins/vector_plugin/docs/README.md) |
| VECTOR_ETH | [README.md](sources/src/plugins/vector_eth_plugin/docs/README.md) |
| DDS | [README.md](sources/src/plugins/dds_plugin/docs/README.md) · [dds_plugin_tutorial.md](sources/src/plugins/dds_plugin/docs/dds_plugin_tutorial.md) |
| DDS_TYPED | [README.md](sources/src/plugins/dds_typed_plugin/docs/README.md) · [dds_typed_plugin_communication_flow.md](sources/src/plugins/dds_typed_plugin/docs/dds_typed_plugin_communication_flow.md) · [dds_typed_plugin_tutorial.md](sources/src/plugins/dds_typed_plugin/docs/dds_typed_plugin_tutorial.md) · [gen_typed_adapter_tutorial.md](sources/src/plugins/dds_typed_plugin/docs/gen_typed_adapter_tutorial.md) |
| MQTT | [mqtt_plugin_tutorial.md](sources/src/plugins/mqtt_plugin/docs/mqtt_plugin_tutorial.md) |
| PROFIBUS | [profibus_plugin_tutorial.md](sources/src/plugins/profibus_plugin/docs/profibus_plugin_tutorial.md) |
| GRPC | [README.md](sources/src/plugins/grpc_plugin/README.md) |
| MODBUS, WEBSOCKET, CH341, DSPKI2C, DSPKSPI, RAWETH, TCPIP, UDP, ENC28J60, LAN8720, W5500 | no separate document in this snapshot — `PLUGIN.INFO` prints the full usage and ini keys |

### Other components

| Document | Scope |
|----------|-------|
| Bus Pirate I2C scan walk-through | [I2C_scanning.md](sources/src/plugins/buspirate_plugin/docs/I2C_scanning.md) |
| CP2112 device notes | [AT24C02.md](sources/src/plugins/cp2112_plugin/docs/AT24C02.md) · [PCA9554AD.md](sources/src/plugins/cp2112_plugin/docs/PCA9554AD.md) |
| ISO-TP flow diagram | [README.md](sources/src/libs/protocols/cantp/isotp/README.md) |
| GUI front-end deployment | [README.md](sources/src/scriptgui/README.md) |
| Test tools | [crossloopback](sources/src/testers/crossloopback/docs/README.md) · [modbus_server](sources/src/testers/modbus_server/docs/README.md) · [UartMonitor.md](sources/src/testers/uartmon/UartMonitor.md) |
