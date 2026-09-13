# FT245 Plugin

A C++ shared-library plugin that exposes the [FTDI FT245](https://ftdichip.com/products/um245r/) USB parallel FIFO adapter through a unified command dispatcher. The plugin supports two hardware variants in a single binary:

- **FT245BM / FT245RL** — Full-Speed USB 2.0 parallel FIFO; supports both **async** and **sync** FIFO modes; up to ~1 MB/s in sync mode
- **FT245R** — Full-Speed USB 2.0 parallel FIFO with integrated oscillator; **async FIFO only**

The active variant is selected per-command via `variant=BM|R` (or set globally in the INI file). It determines the USB PID searched during enumeration and which FIFO modes are permitted.

Two modules are exposed — **FIFO** and **GPIO** — and they are **mutually exclusive**: the FT245 hardware supports only one mode at a time. Close the FIFO driver before opening GPIO, and vice versa.

**Version:** 1.0.0.0  
**Requires:** C++20

> **No clock divisor:** The FT245 is a parallel FIFO bridge — there is no serial protocol engine and no configurable clock. Transfer rate is entirely governed by the USB bulk transfer engine and host bandwidth. Speed presets are not applicable; `setModuleSpeed()` returns `false` with a warning for all modules.

---

## Table of Contents

1. [Overview](#overview)
2. [Variant Comparison](#variant-comparison)
3. [Project Structure](#project-structure)
4. [Architecture](#architecture)
   - [Plugin Lifecycle](#plugin-lifecycle)
   - [Variant and FIFO Mode Selection](#variant-and-fifo-mode-selection)
   - [FIFO vs GPIO Mutual Exclusion](#fifo-vs-gpio-mutual-exclusion)
   - [Command Dispatch Model](#command-dispatch-model)
   - [Generic Template Helpers](#generic-template-helpers)
   - [Pending Configuration Structs](#pending-configuration-structs)
   - [INI Configuration Keys](#ini-configuration-keys)
5. [Building](#building)
6. [Platform Notes](#platform-notes)
7. [Command Reference](#command-reference)
   - [INFO](#info)
   - [FIFO](#fifo)
   - [GPIO](#gpio)
8. [Script Files](#script-files)
9. [Fault-Tolerant and Dry-Run Modes](#fault-tolerant-and-dry-run-modes)
10. [Error Handling and Return Values](#error-handling-and-return-values)

---

## Overview

The plugin loads as a dynamic shared library (`.so` / `.dll`). The host application calls the exported C entry points `pluginEntry()` / `pluginExit()` to create and destroy the plugin instance. Once loaded, settings are pushed via `setParams()`, the plugin is initialized with `doInit()`, enabled with `doEnable()`, and commands are dispatched with `doDispatch()`.

All commands follow the pattern:

```
<PLUGIN>.<COMMAND> [subcommand] [arguments]
```

For example:

```
# Async FIFO (FT245BM, default)
FT245.FIFO open variant=BM mode=async
FT245.FIFO write DEADBEEF
FT245.FIFO read  4
FT245.FIFO close

# Synchronous FIFO for high-throughput (FT245BM only)
FT245.FIFO open variant=BM mode=sync
FT245.FIFO wrrdf large_payload.bin
FT245.FIFO close

# FT245R async FIFO
FT245.FIFO open variant=R
FT245.FIFO script comm_test.txt
FT245.FIFO close

# GPIO bit-bang (mutually exclusive with FIFO — close FIFO first)
FT245.GPIO open variant=BM dir=0xFF val=0x00
FT245.GPIO set  0x01
FT245.GPIO read
FT245.GPIO close
```

---

## Variant Comparison

| Feature | FT245BM / FT245RL | FT245R |
|---|---|---|
| USB speed | Full-Speed (12 Mbps) | Full-Speed (12 Mbps) |
| Async FIFO | ✓ | ✓ |
| Sync FIFO | ✓ (up to ~1 MB/s) | ✗ (rejected at open time) |
| Integrated oscillator | ✗ (external crystal) | ✓ |
| Bit-bang GPIO | ✓ | ✓ |
| `variant=` value | `BM` | `R` |

The string `RL` is accepted as an alias for `BM` — `variant=RL` maps to `FT245Base::Variant::FT245BM`.

---

## Project Structure

```
ftdi245_plugin/
├── CMakeLists.txt                  # Build definition (shared library, C++20)
├── inc/
│   ├── ft245_plugin.hpp            # Main class + command tables + pending config structs
│   ├── ft245_generic.hpp           # Generic template helpers + write/read/script helpers
│   └── private/
│       ├── fifo_config.hpp         # FIFO_COMMANDS_CONFIG_TABLE
│       └── gpio_config.hpp         # GPIO_COMMANDS_CONFIG_TABLE
└── src/
    ├── ft245_plugin.cpp            # Entry points, init/cleanup, INFO, INI loading,
    │                               # parseVariant, parseFifoMode, setModuleSpeed
    ├── ft245_fifo.cpp              # FIFO sub-command implementations
    └── ft245_gpio.cpp              # GPIO sub-command implementations
```

Unlike the MPSSE-based plugins, there are no speed config tables — the FT245 has no clock divisor. The two config headers contain only command tables.

---

## Architecture

### Plugin Lifecycle

```
pluginEntry()               → creates FT245Plugin instance
  setParams()               → loads INI values (variant, fifo mode, timeouts…)
  doInit()                  → propagates INI defaults into both pending config structs;
                              logs variant and default FIFO mode; marks initialized
  doEnable()                → enables real execution (without this, commands only validate args)
  doDispatch(cmd, args)     → routes to the correct top-level or module handler
  FT245.FIFO open ...       → opens FIFO driver (FT245Sync)
  FT245.FIFO close          → closes FIFO driver
  FT245.GPIO open ...       → opens GPIO driver (FT245GPIO) — FIFO must be closed first
  FT245.GPIO close          → closes GPIO driver
  doCleanup()               → closes both drivers, resets state
pluginExit(ptr)             → deletes the FT245Plugin instance
```

`doEnable()` controls a **dry-run / argument-validation mode**: when not enabled, commands parse their arguments and return `true` without touching hardware.

`doInit()` does **not** open any hardware interface. Opening happens explicitly via the per-module `open` sub-command.

### Variant and FIFO Mode Selection

The `variant` and FIFO `mode` can be set in three ways, in decreasing priority:

1. **Per-command** — `variant=BM` / `mode=sync` in the `open` or `cfg` argument string
2. **INI file** — `VARIANT` and `FIFO_MODE` keys (loaded by `setParams`)
3. **Compiled defaults** — `FT245BM`, `Async`

The static helper `parseVariant()` accepts: `BM`, `bm`, `FT245BM`, `245BM`, `RL` (BM alias), `R`, `r`, `FT245R`, `245R`.

The static helper `parseFifoMode()` accepts: `async`, `ASYNC`, `a`, `sync`, `SYNC`, `s`.

The sync FIFO mode restriction is enforced at `open` time:

```cpp
// FT245R does not support sync mode — checked before every open
if (variant == FT245R && fifoMode == Sync) → error, return false
```

### FIFO vs GPIO Mutual Exclusion

The FT245 hardware operates in exactly one mode at a time — either FIFO mode or bit-bang GPIO mode. The plugin enforces this at the D2XX level: the `FT245Sync` and `FT245GPIO` drivers each take exclusive ownership of the device handle when opened. Attempting to open GPIO while FIFO is still active (or vice versa) will fail at the driver level.

The recommended sequence is:

```
FT245.FIFO open  ...   → use FIFO
FT245.FIFO close       → release device

FT245.GPIO open  ...   → take device in bit-bang mode
FT245.GPIO close       → release device

FT245.FIFO open  ...   → reclaim as FIFO
```

### Command Dispatch Model

The dispatch model uses two layers of `std::map`:

1. **Top-level map** (`m_mapCmds`): maps command names (`INFO`, `FIFO`, `GPIO`) to member-function pointers on `FT245Plugin`.
2. **Module-level maps** (`m_mapCmds_FIFO`, `m_mapCmds_GPIO`): each module owns a map of sub-command name → handler pointer.

A meta-map (`m_mapCommandsMaps`) maps module names to their sub-maps, so the generic dispatcher can locate any sub-command dynamically. Command registration is driven by X-macros:

```cpp
// In fifo_config.hpp:
#define FIFO_COMMANDS_CONFIG_TABLE  \
FIFO_CMD_RECORD( open   )           \
FIFO_CMD_RECORD( close  )           \
...

// In the constructor (ft245_plugin.hpp):
#define FIFO_CMD_RECORD(a) \
    m_mapCmds_FIFO.insert({#a, &FT245Plugin::m_handle_fifo_##a});
FIFO_COMMANDS_CONFIG_TABLE
#undef FIFO_CMD_RECORD
```

Both speed maps (`m_mapSpeedsMaps`) are registered as `nullptr` — this signals to `generic_module_set_speed` that no speed presets exist, and the function returns `false` with a warning rather than attempting a lookup.

### Generic Template Helpers

`ft245_generic.hpp` provides the same stateless template functions used by all other FTDI plugins in this project:

| Function | Purpose |
|---|---|
| `generic_module_dispatch<T>()` | Splits `"subcmd args"` and routes to the correct module handler |
| `generic_module_set_speed<T>()` | Returns an error for FT245 (no speed presets registered) |
| `generic_write_data<T>()` | Unhexlifies a hex string and calls a write callback (up to 65536 bytes) |
| `generic_write_read_data<T>()` | Parses `HEXDATA:rdlen` and calls a write-then-read callback |
| `generic_write_read_file<T>()` | Reads data from a binary file in `ARTEFACTS_PATH`, streams in chunks |
| `generic_execute_script<T>()` | Runs a `CommScriptClient` script on an open driver |
| `generic_module_list_commands<T>()` | Logs all registered sub-command names (used by `help`) |

Two limits are defined for bulk data operations:

```cpp
FT_WRITE_MAX_CHUNK_SIZE  = 4096     // Default chunk size for file-based transfers
FT_BULK_MAX_BYTES        = 65536    // FT245 FIFO max per transfer
```

### Pending Configuration Structs

Each module maintains a "pending configuration" struct populated from INI defaults at `doInit()` time and updated per-command by `open` and `cfg`.

| Struct | Fields | Default |
|---|---|---|
| `FifoPendingCfg` | `variant`, `fifoMode` | `FT245BM`, `Async` |
| `GpioPendingCfg` | `variant`, `dirMask`, `initValue` | `FT245BM`, `0x00` (all inputs), `0x00` |

Both structs share the same `variant` field. Setting `variant` in a FIFO `cfg` or `open` does not affect the GPIO pending config, and vice versa — they are updated independently.

### INI Configuration Keys

| Key | Type | Default | Description |
|---|---|---|---|
| `DEVICE_INDEX` | uint8 | `0` | Zero-based FTDI device index passed to D2XX |
| `VARIANT` | string | `BM` | Default hardware variant: `BM` (FT245BM/RL) or `R` (FT245R) |
| `FIFO_MODE` | string | `async` | Default FIFO transfer mode: `async` or `sync` |
| `ARTEFACTS_PATH` | string | `""` | Base directory for script and binary data files |
| `READ_TIMEOUT` | uint32 (ms) | `1000` | Per-operation read timeout for script execution |
| `SCRIPT_DELAY` | uint32 (ms) | `0` | Inter-command delay during script execution |

Note that there are no `SPI_CLOCK`, `I2C_CLOCK`, `*_CHANNEL`, or `UART_BAUD` keys — the FT245 has none of these concepts.

---

## Building

```bash
mkdir build && cd build
cmake .. -DFTD2XX_ROOT=/path/to/ftd2xx/sdk
make ftdi245_plugin
```

The output is `libftdi245_plugin.so` (Linux) or `ftdi245_plugin.dll` (Windows).

**Required libraries** (must be present in the CMake build tree):

- `ft245` — FTDI FT245 driver abstraction (FT245Sync, FT245GPIO)
- `ftdi::sdk` — FTDI D2XX SDK (`FTD2XX.dll` / `libftd2xx.so`); on Windows, set `FTD2XX_ROOT` to the SDK root containing `include/ftd2xx.h` and `amd64/` or `i386/` subdirectories
- `uPluginOps`, `uIPlugin`, `uSharedConfig` — plugin framework
- `uICoreScript`, `uCommScriptClient`, `uCommScriptCommandInterpreter`, `uScriptReader` — scripting engine
- `uICommDriver`, `uUtils` — communication driver base and utilities

On Windows, the build also links against `setupapi`, `user32`, and `advapi32`, and copies `FTD2XX64.dll` into the output directory automatically via a post-build step.

---

## Platform Notes

**Linux:**
- Install the FTDI D2XX userspace library from [ftdichip.com](https://ftdichip.com/drivers/d2xx-drivers/).
- Unbind the `ftdi_sio` kernel driver before use: `sudo rmmod ftdi_sio` or add a udev rule. The kernel driver binds automatically on enumeration and will block D2XX access.
- The device is addressed by zero-based index (`0` = first FT245 of the matching variant on the bus).
- FT245BM and FT245R have different USB PIDs; if both variants are connected, adjust `DEVICE_INDEX` accordingly.

**Windows:**
- The FTDI D2XX DLL (`FTD2XX.dll`) must be present — either from a system-wide installation or copied next to the plugin (the build does this automatically).
- Set `FTD2XX_ROOT` during CMake configuration to point at the extracted FTDI D2XX SDK.
- Device index `0` selects the first enumerated FT245 device.

---

## Command Reference

### INFO

Prints version, active variant, default FIFO mode, device index, and a complete command reference for both modules. Takes **no arguments** and works even before `doInit()`.

```
FT245.INFO
```

---

### FIFO

The FIFO module provides a bulk byte-stream interface through the FT245 parallel FIFO. There is no serial framing, no clock divisor, and no chip-select — data is written into the TX FIFO and read from the RX FIFO directly.

**FT245 FIFO pin mapping:**

| Signal | Pin | Description |
|---|---|---|
| D0–D7 | Data bus | 8-bit parallel data |
| RD# | Read strobe | Driven by D2XX driver |
| WR | Write strobe | Driven by D2XX driver |
| TXE# | TX empty flag | Indicates TX FIFO can accept data |
| RXF# | RX full flag | Indicates RX FIFO has data available |
| PWREN# | Power enable | USB power control output |

All handshaking is managed automatically by the `FT245Sync` driver and the D2XX library — user commands only supply data bytes and byte counts.

---

#### FIFO · open — Open FIFO interface

```
FT245.FIFO open [variant=BM|R] [mode=async|sync] [device=N]
```

| Argument | Description | Default |
|---|---|---|
| `variant` | `BM` = FT245BM/RL, `R` = FT245R | INI `VARIANT` (default `BM`) |
| `mode` | `async` (both variants) or `sync` (FT245BM only) | INI `FIFO_MODE` (default `async`) |
| `device` | Zero-based FT245 device index | `0` |

> **FT245R restriction:** `mode=sync` is rejected at open time with an error — use `mode=async` or omit `mode=`.

```
# FT245BM async FIFO (default)
FT245.FIFO open variant=BM mode=async

# FT245BM synchronous FIFO (high-throughput)
FT245.FIFO open variant=BM mode=sync

# FT245R (async only)
FT245.FIFO open variant=R

# Second device on bus
FT245.FIFO open variant=BM device=1
```

---

#### FIFO · close — Release FIFO interface

```
FT245.FIFO close
```

---

#### FIFO · cfg — Update FIFO configuration without reopening

Updates the pending FIFO configuration. Changes take effect on the next `open`. Query current state with `?`.

```
FT245.FIFO cfg [variant=BM|R] [mode=async|sync]
FT245.FIFO cfg ?
```

```
FT245.FIFO cfg mode=sync
FT245.FIFO cfg variant=R
FT245.FIFO cfg ?
```

---

#### FIFO · write — Transmit bytes into the TX FIFO

Unhexlifies the argument string and writes all bytes in one call. Up to 65536 bytes per command.

```
FT245.FIFO write <HEXDATA>
```

```
FT245.FIFO write DEADBEEF
FT245.FIFO write 48656C6C6F    # ASCII "Hello"
FT245.FIFO write 00112233AABBCCDD
```

---

#### FIFO · read — Receive N bytes from the RX FIFO

Reads exactly N bytes from the RX FIFO using `ReadMode::Exact` and prints them as a hex dump.

```
FT245.FIFO read <N>
```

```
FT245.FIFO read 4
FT245.FIFO read 256
```

---

#### FIFO · wrrd — Write then read

Writes hex data then reads back a specified number of bytes. Either phase can be omitted.

```
FT245.FIFO wrrd <HEXDATA>:<rdlen>   # write + read
FT245.FIFO wrrd :<rdlen>            # read only
FT245.FIFO wrrd <HEXDATA>           # write only
```

The write and read phases are sequential — the write completes fully before the read begins. There is no concept of full-duplex on the FT245 FIFO.

```
# Write a 2-byte command, read 4-byte response
FT245.FIFO wrrd 9F00:4

# Write header bytes then read payload
FT245.FIFO wrrd AABBCCDD:16

# Read only — no write phase
FT245.FIFO wrrd :8
```

---

#### FIFO · wrrdf — File-backed write-then-read

Reads write data from a binary file in `ARTEFACTS_PATH` and streams it to the device in chunks, reading back a matching number of bytes per chunk.

```
FT245.FIFO wrrdf <filename>[:<wrchunk>][:<rdchunk>]
```

| Argument | Description | Default |
|---|---|---|
| `filename` | Binary file under `ARTEFACTS_PATH` | — |
| `wrchunk` | Write chunk size in bytes | `4096` |
| `rdchunk` | Read chunk size in bytes | `4096` |

```
FT245.FIFO wrrdf payload.bin
FT245.FIFO wrrdf large_transfer.bin:4096:4096
FT245.FIFO wrrdf data.bin:512:512
```

---

#### FIFO · flush — Purge RX and TX FIFOs

Discards any data currently buffered in the RX and TX FIFOs without closing the interface. Useful for resetting communication state between test steps.

```
FT245.FIFO flush
```

---

#### FIFO · script — Execute a command script

**FIFO must be open first.**

Executes a `CommScriptClient` script from `ARTEFACTS_PATH`. The script can contain write, read, and expect operations against the open FIFO driver. `READ_TIMEOUT` and `SCRIPT_DELAY` from the INI govern timing.

```
FT245.FIFO script <filename>
```

```
FT245.FIFO script comm_test.txt
FT245.FIFO script bulk_transfer.txt
```

---

#### FIFO · help — List available sub-commands

```
FT245.FIFO help
```

---

### GPIO

The GPIO module controls all eight data pins (D0–D7) using the FT245 `BITMODE_BITBANG` mode via the `FT245GPIO` driver. Unlike the MPSSE-based GPIO in other FTDI plugins, the FT245 exposes a **single 8-bit port** — there are no banks (`low`/`high`), no channel selectors, and no separate direction/data registers. Every command operates on all eight pins at once.

> **FIFO must be closed before opening GPIO.** The bit-bang and FIFO modes are mutually exclusive at the hardware level.

> When GPIO is closed, all pins revert to inputs (the D2XX library resets BITMODE on close).

**FT245 GPIO pin mapping:**

| Bit | Pin | Direction |
|---|---|---|
| Bit 0 | D0 | Controlled by direction mask |
| Bit 1 | D1 | Controlled by direction mask |
| … | … | … |
| Bit 7 | D7 | Controlled by direction mask |

The direction mask follows the convention `1 = output`, `0 = input`, applied to the full 8-bit bus simultaneously.

---

#### GPIO · open — Open GPIO bit-bang mode

```
FT245.GPIO open [variant=BM|R] [dir=0xNN] [val=0xNN] [device=N]
```

| Argument | Description | Default |
|---|---|---|
| `variant` | `BM` = FT245BM/RL, `R` = FT245R | INI `VARIANT` (default `BM`) |
| `dir` | Direction mask: `1`=output, `0`=input (applied to D0–D7) | `0x00` (all inputs) |
| `val` / `value` | Initial output levels for pins configured as outputs | `0x00` |
| `device` | Zero-based device index | `0` |

```
# All pins as outputs, initially low
FT245.GPIO open variant=BM dir=0xFF val=0x00

# Upper nibble outputs, lower nibble inputs
FT245.GPIO open dir=0xF0

# FT245R, all inputs
FT245.GPIO open variant=R

# Mixed — D0–D3 outputs initially 0xA, D4–D7 inputs
FT245.GPIO open dir=0x0F val=0x0A
```

---

#### GPIO · close — Release GPIO interface

All pins revert to inputs on close.

```
FT245.GPIO close
```

---

#### GPIO · cfg — Update GPIO configuration without reopening

Updates the pending direction mask, initial value, and variant. Takes effect on the next `open`. Query with `?`.

```
FT245.GPIO cfg [variant=BM|R] [dir=0xNN] [val=0xNN]
FT245.GPIO cfg ?
```

```
FT245.GPIO cfg dir=0xFF val=0xAA
FT245.GPIO cfg variant=R
FT245.GPIO cfg ?
```

---

#### GPIO · dir — Set pin direction at runtime

Applies a new direction mask to all 8 pins while GPIO is open. An optional initial value for the newly-configured output pins can be specified.

```
FT245.GPIO dir <MASK> [<INITVAL>]
```

```
# All D0–D7 as outputs, initially low
FT245.GPIO dir 0xFF 0x00

# D0–D3 outputs, D4–D7 inputs (no initial value change)
FT245.GPIO dir 0x0F

# All as inputs
FT245.GPIO dir 0x00
```

---

#### GPIO · write — Write a byte to all output pins

Writes an absolute 8-bit value across all D0–D7 output pins. Pins configured as inputs are not affected.

```
FT245.GPIO write <VALUE>
```

```
FT245.GPIO write 0xAA
FT245.GPIO write 0x00
FT245.GPIO write 0xFF
```

---

#### GPIO · set — Drive masked pins HIGH

Sets the specified pins HIGH without changing the state of any other pins.

```
FT245.GPIO set <MASK>
```

```
# D0 high
FT245.GPIO set 0x01

# D7 high
FT245.GPIO set 0x80

# D0 and D7 high
FT245.GPIO set 0x81
```

---

#### GPIO · clear — Drive masked pins LOW

Clears the specified pins LOW without changing the state of any other pins.

```
FT245.GPIO clear <MASK>
```

```
FT245.GPIO clear 0x01
FT245.GPIO clear 0xF0
```

---

#### GPIO · toggle — Invert masked output pins

Inverts the current output state of the specified pins.

```
FT245.GPIO toggle <MASK>
```

```
FT245.GPIO toggle 0x01
FT245.GPIO toggle 0xFF
```

---

#### GPIO · read — Sample all 8 pins

Reads the current logical levels of all D0–D7 pins (regardless of direction). Output pins echo the last written value; input pins reflect the external signal. Prints the result as hex and binary, D7 MSB.

```
FT245.GPIO read
```

**Example output:**
```
FT245_GPIO | D0-D7: 0x5A  [01011010]  (D7..D0)
```

---

#### GPIO · help — List available sub-commands

```
FT245.GPIO help
```

---

## Script Files

Script files are plain text files located under `ARTEFACTS_PATH`. They are executed by the `CommScriptClient` engine, which reads each line and performs write/read/expect operations against the open FIFO driver. GPIO does not support script execution.

The FIFO interface must be open before calling `script`. The `SCRIPT_DELAY` INI key inserts a per-command delay in milliseconds; `READ_TIMEOUT` governs how long each read operation waits before timing out.

```
# Typical FIFO script usage
FT245.FIFO open variant=BM mode=async
FT245.FIFO script comm_test.txt
FT245.FIFO close
```

---

## Fault-Tolerant and Dry-Run Modes

- **Dry-run mode**: when `doEnable()` has not been called, every command validates its arguments (including variant/mode constraints) and returns `true` without touching hardware. The generic dispatcher detects `isEnabled() == false` and returns early after argument parsing. This allows test framework validators to check command syntax before a live run.

- **Fault-tolerant mode** (`setFaultTolerant()` / `isFaultTolerant()`): when set, the plugin framework can be configured to continue execution past command failures. Useful in production test scripts where a non-critical FIFO timeout should not abort a longer sequence.

- **Privileged mode** (`isPrivileged()`): always returns `false`; reserved for future framework use.

---

## Error Handling and Return Values

Every handler returns `bool`:
- `true` — success (or argument validation passed in disabled/dry-run mode).
- `false` — argument validation failed, unknown sub-command, mode/variant constraint violated (e.g. `mode=sync` with `variant=R`), driver open failed, hardware operation returned an error status, or file not found.

All drivers return a typed `Status` enum (`SUCCESS`, and various error values). The plugin checks these and logs an error then returns `false` on any non-`SUCCESS` status.

Diagnostic messages are emitted via `LOG_PRINT` at several severity levels:

| Level | Usage |
|---|---|
| `LOG_ERROR` | Command failed, invalid argument, mode/variant constraint violation, hardware error |
| `LOG_WARNING` | Non-fatal issue (e.g., closing a port that was not open; `setModuleSpeed` called) |
| `LOG_INFO` | Successful operations (bytes written/read, device opened, direction set, etc.) |
| `LOG_VERBOSE` | INI parameter loading details |
| `LOG_FIXED` | Help text output, `read` result, `cfg ?` dump |

Log verbosity is controlled by the host application via the shared `uLogger` configuration.
