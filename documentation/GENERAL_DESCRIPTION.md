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
| **Entry Point** | `uScriptMainApp` — parses CLI, loads INI, kicks off execution |
| **Config** | `IniCfgLoader` / `uIniParser` / `uSharedConfig` — shared settings |
| **Script Core** | `ScriptClient` → `ScriptRunner` → `Reader` + `Validator` + `Interpreter` |
| **Comm Scripts** | Extends core with comm-specific runner, validator & interpreter |
| **Plugin Interface** | `IPlugin` — abstract contract all plugins implement, loaded dynamically |
| **Hardware Plugins** | One plugin per device: USB/serial bridges (BusPirate, Hydrabus, CH347, CH341, CP2112, FTDI×4, UART), CAN (KVCAN, PCAN, SLCAN), network (TCP/IP, UDP, MQTT, ENC28J60, LAN8720, W5500, RawEth), kernel/USB I2C & SPI (KI2C, KSPI, DSPKI2C, DSPKSPI)… |
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
        ├── shell_plugin/    Interactive shell session
        ├── uart_plugin/     Serial port send/receive with Comm Script support
        ├── uartmon_plugin/  Serial port insertion/removal monitor
        ├── ch341_plugin/    WCH CH341 USB-to-serial adapter
        ├── buspirate_plugin/Bus Pirate: SPI, I2C, UART, 1-Wire, Raw-Wire
        ├── ch347_plugin/    WCH CH347 USB: SPI, I2C, GPIO, JTAG
        ├── cp2112_plugin/   SiLabs CP2112 USB-HID: I2C, GPIO
        ├── ftdi232h_plugin/ FTDI FT232H: SPI, I2C, GPIO, UART
        ├── ftdi2232_plugin/ FTDI FT2232H/D: SPI, I2C, GPIO, UART
        ├── ftdi245_plugin/  FTDI FT245: parallel FIFO, GPIO
        ├── ftdi4232_plugin/ FTDI FT4232H: SPI, I2C, GPIO, UART (quad)
        ├── hydrabus_plugin/ HydraBus: SPI, I2C, UART, 1-Wire, SWD, NFC…
        ├── ki2c_plugin/     Linux kernel I2C (/dev/i2c-N)
        ├── kspi_plugin/     Linux kernel SPI (/dev/spidevN.N)
        ├── dspki2c_plugin/  USB-bridge I2C (VID/PID-addressed device)
        ├── dspkspi_plugin/  USB-bridge SPI (VID/PID-addressed device)
        ├── kvcan_plugin/    Linux SocketCAN (CAN / CAN FD)
        ├── pcan_plugin/     PEAK-System PCAN-Basic CAN adapter
        ├── slcan_plugin/    Serial-Line CAN (SLCAN/CANable-style adapters)
        ├── raweth_plugin/   Raw Ethernet frames (custom EtherType)
        ├── tcpip_plugin/    TCP client
        ├── udp_plugin/      UDP client
        ├── mqtt_plugin/     MQTT publish/subscribe client
        ├── enc28J60_plugin/ Microchip ENC28J60 SPI Ethernet controller
        ├── lan8720_plugin/  SMSC LAN8720 RMII Ethernet PHY
        └── w5500_plugin/    WIZnet W5500 SPI Ethernet controller
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
| Math | `name ?= MATH expression` | Evaluate an arithmetic/boolean/ternary expression |
| Bit/byte packing | `name ?= BITSTREAM off:len:val ...` / `BYTESTREAM byte_off:len:val ...` | Pack numeric fields into a hex-encoded byte buffer |
| Bit/byte extraction | `name ?= hex_source \| BITSTREAMVAL bit_off:size` / `BYTESTREAMVAL byte_off:bit_off:size` | Read one field back out of a hex buffer (inverse of the row above) |
| Bit/byte extraction (array) | `name [= hex_source \| BITSTREAMVAL bit_off1:size1 ...` / `BYTESTREAMVAL byte_off1:bit_off1:size1 ...` | Same as above, but read one-or-more fields in a single statement into an array macro |
| Breakpoint | `BREAKPOINT [label]` | Pause and wait for operator input |

See [`SCRIPTING_LANGUAGE_REFERENCE.md`](SCRIPTING_LANGUAGE_REFERENCE.md) for the complete, authoritative syntax of every native statement, macro form, and validation rule.



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

> **Note:** `PRINT`, `DELAY`, `MESSAGE`/`PRINT`, `BREAKPOINT`, `FORMAT`, `MATH`,
> `BITSTREAM`/`BYTESTREAM`, `BITSTREAMVAL`/`BYTESTREAMVAL` (including their
> array forms), and `EVAL` are **native language statements**
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

📄 **Full documentation:** [README.md](sources/src/plugin/shell_plugin/docs/README.md)


---

### `UART` — Serial Port (with Comm Script Support)
Drives a UART serial port. Supports inline command expressions using the same Comm Script decorator syntax, or delegates to a full `.txt` Comm Script file.

| Commands | Purpose |
|----------|---------| 
| `INFO` | Print plugin info |
| `CONFIG p=port b=baud r=rtout w=wtout s=size` | Configure the port |
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

### `CH341` — WCH CH341 USB-to-Serial Adapter
Simple USB-to-serial bridge, configured like a UART.

| Commands | Purpose |
|----------|---------|
| `INFO` | Print plugin info |
| `CONFIG p=port b=baudrate r=read_tout w=write_tout s=recv_bufsize` | (Re)configure at runtime |
| `CMD > "..." \| T"..."` | Single inline send/receive expression |
| `SCRIPT path.txt` | Execute a full Comm Script file |

📄 **Full documentation:** [README.md](sources/src/plugin/ch341_plugin/docs/README.md)


---

### `KI2C` / `KSPI` — Linux Kernel I2C / SPI
Drive an I2C or SPI device directly through the Linux kernel device nodes
(`/dev/i2c-N`, `/dev/spidevN.N`) — no USB bridge chip involved.

| Commands | Purpose |
|----------|---------|
| `INFO` | Print plugin info |
| `CONFIG ...` | (Re)configure device path, address/mode, timeouts, buffer size at runtime |
| `CMD > "..." \| T"..."` | Single inline send/receive expression |
| `SCRIPT path.txt` | Execute a full Comm Script file |

📄 **Full documentation:** [README.md](sources/src/plugin/ki2c_plugin/docs/README.md) · [README.md](sources/src/plugin/kspi_plugin/docs/README.md)


---

### `DSPKI2C` / `DSPKSPI` — USB-Bridge I2C / SPI
Drive an I2C or SPI device through a VID/PID-addressed USB bridge (as opposed
to `KI2C`/`KSPI`'s direct kernel device-node access).

| Commands | Purpose |
|----------|---------|
| `INFO` | Print plugin info |
| `CONFIG v=vid p=pid ...` | (Re)configure VID/PID, address/mode, timeouts, buffer size at runtime |
| `CMD > "..." \| T"..."` | Single inline send/receive expression |
| `SCRIPT path.txt` | Execute a full Comm Script file |

📄 **Full documentation:** [README.md](sources/src/plugin/dspki2c_plugin/docs/README.md) · [README.md](sources/src/plugin/dspkspi_plugin/docs/README.md)


---

### `KVCAN` / `PCAN` / `SLCAN` — CAN Bus Adapters
Three CAN transports sharing a common command shape: `KVCAN` drives Linux
SocketCAN (CAN / CAN FD), `PCAN` drives PEAK-System's PCAN-Basic SDK, and
`SLCAN` drives serial-line CAN adapters (SLCAN/CANable-style, with CAN FD and
ISO-TP/J1939 transport-layer support).

| Commands | Purpose |
|----------|---------|
| `INFO` | Print plugin info |
| `CONFIG i=iface x=tx_id r=read_tout w=write_tout s=recv_bufsize ...` | (Re)configure channel/id/timeouts at runtime |
| `FILTER id:mask [id:mask ...]` | Install RX acceptance filters |
| `CMD > "..." \| T"..."` | Single inline send/receive expression |
| `SCRIPT path.txt` | Execute a full Comm Script file |

📄 **Full documentation:** [README.md](sources/src/plugin/kvcan_plugin/docs/README.md) · [README.md](sources/src/plugin/pcan_plugin/docs/README.md) · [README.md](sources/src/plugin/slcan_plugin/docs/README.md)


---

### `RAWETH` — Raw Ethernet Frames
Sends and receives raw Ethernet frames under a configurable EtherType,
bypassing the IP stack entirely.

| Commands | Purpose |
|----------|---------|
| `INFO` | Print plugin info |
| `CONFIG i=iface d=dest_mac t=ethertype ...` | (Re)configure interface/destination/EtherType at runtime |
| `CMD > "..." \| T"..."` | Single inline send/receive expression |

📄 **Full documentation:** [README.md](sources/src/plugin/raweth_plugin/docs/README.md)


---

### `TCPIP` / `UDP` — Network Client Sockets
TCP and UDP client transports, both configured with the same `h=host p=port`
style CONFIG grammar as the other network plugins.

| Commands | Purpose |
|----------|---------|
| `INFO` | Print plugin info |
| `CONFIG h=host p=port r=read_tout w=write_tout s=recv_bufsize` | (Re)configure endpoint/timeouts at runtime |
| `CMD > "..." \| T"..."` | Single inline send/receive expression |
| `SCRIPT path.txt` | Execute a full Comm Script file |

📄 **Full documentation:** [README.md](sources/src/plugin/tcpip_plugin/docs/README.md) · [README.md](sources/src/plugin/udp_plugin/docs/README.md)


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

📄 **Full documentation:** [README.md](sources/src/plugin/mqtt_plugin/docs/README.md)


---

### `ENC28J60` / `LAN8720` / `W5500` — Embedded Ethernet Controllers
Three SPI/RMII Ethernet PHY/MAC controllers commonly paired with
microcontrollers, exposed with the same network-plugin CONFIG shape as
`TCPIP`/`UDP`.

| Commands | Purpose |
|----------|---------|
| `INFO` | Print plugin info |
| `CONFIG i=iface p=port ...` | (Re)configure interface/endpoint at runtime |
| `CMD > "..." \| T"..."` | Single inline send/receive expression |

📄 **Full documentation:** [README.md](sources/src/plugin/enc28J60_plugin/docs/README.md) · [README.md](sources/src/plugin/lan8720_plugin/docs/README.md) · [README.md](sources/src/plugin/w5500_plugin/docs/README.md)


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
| Core scripting language — full syntax reference | [SCRIPTING_LANGUAGE_REFERENCE.md](SCRIPTING_LANGUAGE_REFERENCE.md) |
| Core scripting language — step-by-step tutorial | [SCRIPTING_LANGUAGE_TUTORIAL.md](SCRIPTING_LANGUAGE_TUTORIAL.md) |
| Native MATH expression syntax | [MATH_COMMAND_REFERENCE.md](MATH_COMMAND_REFERENCE.md) |

### Plugins

| Document | Scope |
|----------|-------|
| SHELL | [README.md](sources/src/plugin/shell_plugin/docs/README.md) |
| UART | [README.md](sources/src/plugin/uart_plugin/docs/README.md) |
| UARTMON | [README.md](sources/src/plugin/uartmon_plugin/docs/README.md) |
| CH341 | [README.md](sources/src/plugin/ch341_plugin/docs/README.md) |
| BUSPIRATE | [README.md](sources/src/plugin/buspirate_plugin/docs/README.md) |
| CH347 | [README.md](sources/src/plugin/ch347_plugin/docs/README.md) |
| CP2112 | [README.md](sources/src/plugin/cp2112_plugin/docs/README.md) |
| FT232H | [README.md](sources/src/plugin/ftdi232h_plugin/docs/README.md) |
| FT2232 | [README.md](sources/src/plugin/ftdi2232_plugin/docs/README.md) |
| FT245 | [README.md](sources/src/plugin/ftdi245_plugin/docs/README.md) |
| FT4232 | [README.md](sources/src/plugin/ftdi4232_plugin/docs/README.md) |
| HYDRABUS | [README.md](sources/src/plugin/hydrabus_plugin/docs/README.md) |
| KI2C | [README.md](sources/src/plugin/ki2c_plugin/docs/README.md) |
| KSPI | [README.md](sources/src/plugin/kspi_plugin/docs/README.md) |
| DSPKI2C | [README.md](sources/src/plugin/dspki2c_plugin/docs/README.md) |
| DSPKSPI | [README.md](sources/src/plugin/dspkspi_plugin/docs/README.md) |
| KVCAN | [README.md](sources/src/plugin/kvcan_plugin/docs/README.md) |
| PCAN | [README.md](sources/src/plugin/pcan_plugin/docs/README.md) |
| SLCAN | [README.md](sources/src/plugin/slcan_plugin/docs/README.md) |
| RAWETH | [README.md](sources/src/plugin/raweth_plugin/docs/README.md) |
| TCPIP | [README.md](sources/src/plugin/tcpip_plugin/docs/README.md) |
| UDP | [README.md](sources/src/plugin/udp_plugin/docs/README.md) |
| MQTT | [README.md](sources/src/plugin/mqtt_plugin/docs/README.md) |
| ENC28J60 | [README.md](sources/src/plugin/enc28J60_plugin/docs/README.md) |
| LAN8720 | [README.md](sources/src/plugin/lan8720_plugin/docs/README.md) |
| W5500 | [README.md](sources/src/plugin/w5500_plugin/docs/README.md) |

