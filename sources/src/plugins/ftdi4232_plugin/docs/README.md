# FT4232 Plugin

A C++ shared-library plugin that exposes the [FTDI FT4232H](https://ftdichip.com/products/ft4232h-mini-module/) adapter through a unified command dispatcher. The FT4232H is a Hi-Speed USB 2.0 device with **four independent channels**:

- **Channels A and B** — MPSSE-capable: SPI master, I2C master, or GPIO
- **Channels C and D** — Async UART only (VCP or D2XX direct)

All four interfaces — **SPI**, **I2C**, **GPIO**, and **UART** — are available simultaneously. Each module owns its own USB handle and can be open at the same time as any other module, on any valid channel.

**Version:** 1.0.0.0  
**USB PID:** 0x6011  
**Clock base:** 60 MHz  
**Requires:** C++20

> **Channel rule:** SPI, I2C, and GPIO use channels A or B (MPSSE). UART uses channels C or D (async). Specifying `channel=A` or `channel=B` for UART is rejected at runtime. There is no `variant` parameter — the FT4232H is a single-variant device.

---

## Table of Contents

1. [Overview](#overview)
2. [Channel Summary](#channel-summary)
3. [Project Structure](#project-structure)
4. [Architecture](#architecture)
   - [Plugin Lifecycle](#plugin-lifecycle)
   - [Channel Selection](#channel-selection)
   - [Command Dispatch Model](#command-dispatch-model)
   - [Generic Template Helpers](#generic-template-helpers)
   - [Pending Configuration Structs](#pending-configuration-structs)
   - [INI Configuration Keys](#ini-configuration-keys)
5. [Building](#building)
6. [Platform Notes](#platform-notes)
7. [Command Reference](#command-reference)
   - [INFO](#info)
   - [SPI](#spi)
   - [I2C](#i2c)
   - [GPIO](#gpio)
   - [UART](#uart)
8. [SPI Clock Reference](#spi-clock-reference)
9. [I2C Speed Reference](#i2c-speed-reference)
10. [UART Baud Rate Reference](#uart-baud-rate-reference)
11. [Script Files](#script-files)
12. [Fault-Tolerant and Dry-Run Modes](#fault-tolerant-and-dry-run-modes)
13. [Error Handling and Return Values](#error-handling-and-return-values)

---

## Overview

The plugin loads as a dynamic shared library (`.so` / `.dll`). The host application calls the exported C entry points `pluginEntry()` / `pluginExit()` to create and destroy the plugin instance. Once loaded, settings are pushed via `setParams()`, the plugin is initialized with `doInit()`, enabled with `doEnable()`, and commands are dispatched with `doDispatch()`.

All commands follow the pattern:

```
<PLUGIN>.<COMMAND> [subcommand] [arguments]
```

For example:

```
FT4232.SPI  open channel=A clock=10000000 mode=0
FT4232.SPI  wrrd 9F:3
FT4232.I2C  open channel=B addr=0x50 clock=400000
FT4232.I2C  scan
FT4232.GPIO open channel=B lowdir=0xFF
FT4232.GPIO set  low 0x01
FT4232.UART open channel=C baud=115200
FT4232.UART write DEADBEEF
```

Each interface must be explicitly **opened** and **closed**. Because every module owns an independent USB handle, all four can be active simultaneously — for example, SPI on channel A, I2C on channel B, and two UART ports on channels C and D at the same time.

---

## Channel Summary

| Channel | Capability | Default for |
|---|---|---|
| A | MPSSE — SPI, I2C, GPIO | SPI, I2C |
| B | MPSSE — SPI, I2C, GPIO | GPIO |
| C | Async UART | UART |
| D | Async UART | — |

The FT4232H exposes all four channels through a single USB device (PID 0x6011). Channel assignment is per-module and set via the `channel=` argument or the corresponding INI key.

---

## Project Structure

```
ftdi4232_plugin/
├── CMakeLists.txt                  # Build definition (shared library, C++20)
├── inc/
│   ├── ft4232_plugin.hpp           # Main class + command tables + pending config structs
│   ├── ft4232_generic.hpp          # Generic template helpers + write/read/script helpers
│   └── private/
│       ├── spi_config.hpp          # SPI_COMMANDS_CONFIG_TABLE + SPI_SPEED_CONFIG_TABLE
│       ├── i2c_config.hpp          # I2C_COMMANDS_CONFIG_TABLE + I2C_SPEED_CONFIG_TABLE
│       ├── gpio_config.hpp         # GPIO_COMMANDS_CONFIG_TABLE
│       └── uart_config.hpp         # UART_COMMANDS_CONFIG_TABLE + UART_SPEED_CONFIG_TABLE
└── src/
    ├── ft4232_plugin.cpp           # Entry points, init/cleanup, INFO, INI loading,
    │                               # parseChannel, setModuleSpeed
    ├── ft4232_spi.cpp              # SPI sub-command implementations
    ├── ft4232_i2c.cpp              # I2C sub-command implementations
    ├── ft4232_gpio.cpp             # GPIO sub-command implementations
    └── ft4232_uart.cpp             # UART sub-command implementations (channels C/D only)
```

Each protocol lives in its own `.cpp` file. Command and speed tables are defined in the config headers using X-macros, following the same pattern as all other FTDI plugins in this project.

---

## Architecture

### Plugin Lifecycle

```
pluginEntry()               → creates FT4232Plugin instance
  setParams()               → loads INI values (channels, speeds, timeouts…)
  doInit()                  → propagates INI defaults into all pending config structs;
                              logs device index and default SPI/I2C channels;
                              marks initialized
  doEnable()                → enables real execution (without this, commands only validate args)
  doDispatch(cmd, args)     → routes to the correct top-level or module handler
  FT4232.SPI open ...       → opens SPI driver (FT4232SPI) on the specified MPSSE channel
  FT4232.SPI close          → closes SPI driver
  (similar for I2C / GPIO / UART)
  doCleanup()               → closes all four drivers, resets state
pluginExit(ptr)             → deletes the FT4232Plugin instance
```

`doEnable()` controls a **dry-run / argument-validation mode**: when not enabled, commands parse their arguments and return `true` without touching hardware. This is used by test frameworks to verify command syntax before a live run.

`doInit()` does **not** open any hardware interface. Opening happens explicitly via the per-module `open` sub-command.

### Channel Selection

Each module has an independent channel that can be set in three ways, in decreasing priority:

1. **Per-command** — `channel=A` (or B, C, D) in the `open` or `cfg` argument string
2. **INI file** — `SPI_CHANNEL`, `I2C_CHANNEL`, `GPIO_CHANNEL`, `UART_CHANNEL` keys (loaded by `setParams`)
3. **Compiled defaults** — channel A for SPI and I2C, channel B for GPIO, channel C for UART

The static helper `parseChannel()` accepts `A`/`a`, `B`/`b`, `C`/`c`, `D`/`d`.

UART channel enforcement is checked at `open` time:

```cpp
// Only channels C and D support async UART — checked before every open
if (channel != Channel::C && channel != Channel::D) → error, return false
```

There is no equivalent MPSSE restriction: any channel (A or B) is valid for SPI, I2C, and GPIO. The defaults deliberately assign SPI/I2C to channel A and GPIO to channel B so they do not share MPSSE channel resources by default.

### Command Dispatch Model

The dispatch model uses two layers of `std::map`:

1. **Top-level map** (`m_mapCmds`): maps command names (`INFO`, `SPI`, `I2C`, `GPIO`, `UART`) to member-function pointers on `FT4232Plugin`.
2. **Module-level maps** (`m_mapCmds_SPI`, `m_mapCmds_I2C`, `m_mapCmds_GPIO`, `m_mapCmds_UART`): each module owns a map of sub-command name → handler pointer.

A meta-map (`m_mapCommandsMaps`) maps module names to their sub-maps, so the generic dispatcher can locate any sub-command dynamically without switch statements.

Command registration is entirely driven by X-macros in the `*_config.hpp` headers:

```cpp
// In spi_config.hpp:
#define SPI_COMMANDS_CONFIG_TABLE  \
SPI_CMD_RECORD( open   )           \
SPI_CMD_RECORD( close  )           \
SPI_CMD_RECORD( cfg    )           \
...

// In the constructor (ft4232_plugin.hpp):
#define SPI_CMD_RECORD(a) \
    m_mapCmds_SPI.insert({#a, &FT4232Plugin::m_handle_spi_##a});
SPI_COMMANDS_CONFIG_TABLE
#undef SPI_CMD_RECORD
```

Adding a new sub-command requires only one line in the config table and one handler function.

### Generic Template Helpers

`ft4232_generic.hpp` provides stateless template functions shared by all modules:

| Function | Purpose |
|---|---|
| `generic_module_dispatch<T>()` | Splits `"subcmd args"` and routes to the correct module handler |
| `generic_module_set_speed<T>()` | Looks up a speed label (or raw Hz), calls `setModuleSpeed()` |
| `generic_write_data<T>()` | Unhexlifies a hex string and calls a write callback (up to 65536 bytes) |
| `generic_write_read_data<T>()` | Parses `HEXDATA:rdlen` and calls a write-then-read callback |
| `generic_write_read_file<T>()` | Reads write data from a binary file in `ARTEFACTS_PATH`, streams in chunks |
| `generic_execute_script<T>()` | Runs a `CommScriptClient` script on an open driver |
| `generic_module_list_commands<T>()` | Logs all registered sub-command names (used by `help`) |

Two limits are defined for bulk data operations:

```cpp
FT_WRITE_MAX_CHUNK_SIZE  = 4096     // Default chunk size for file-based transfers
FT_BULK_MAX_BYTES        = 65536    // MPSSE max per-transfer limit
```

### Pending Configuration Structs

Each module maintains a "pending configuration" struct populated from INI defaults at `doInit()` time and updated per-command by `open` and `cfg`.

| Struct | Fields | Default |
|---|---|---|
| `SpiPendingCfg` | `clockHz`, `mode`, `bitOrder`, `csPin`, `csPolarity`, `channel` | 1 MHz, mode=0, MSB, csPin=0x08, active-low, A |
| `I2cPendingCfg` | `address`, `clockHz`, `channel` | addr=0x50, 100 kHz, A |
| `GpioPendingCfg` | `channel`, `lowDirMask`, `lowValue`, `highDirMask`, `highValue` | **B**, all inputs, all 0 |
| `UartPendingCfg` | `baudRate`, `dataBits`, `stopBits`, `parity`, `hwFlowCtrl`, `channel` | 115200, 8, 1, none, false, **C** |

Note that GPIO defaults to **channel B** and UART defaults to **channel C** — these INI-controlled defaults keep all four modules on separate channels out of the box.

### INI Configuration Keys

| Key | Type | Default | Description |
|---|---|---|---|
| `DEVICE_INDEX` | uint8 | `0` | Zero-based FTDI device index passed to D2XX |
| `ARTEFACTS_PATH` | string | `""` | Base directory for script and binary data files |
| `SPI_CHANNEL` | char | `A` | Default SPI MPSSE channel: `A` or `B` |
| `I2C_CHANNEL` | char | `A` | Default I2C MPSSE channel: `A` or `B` |
| `GPIO_CHANNEL` | char | `B` | Default GPIO MPSSE channel: `A` or `B` |
| `UART_CHANNEL` | char | `C` | Default UART async channel: `C` or `D` |
| `SPI_CLOCK` | uint32 (Hz) | `1000000` | Initial SPI clock frequency |
| `I2C_CLOCK` | uint32 (Hz) | `100000` | Initial I2C clock frequency |
| `I2C_ADDRESS` | uint8 (hex) | `0x50` | Default I2C target device address |
| `UART_BAUD` | uint32 | `115200` | Default UART baud rate |
| `READ_TIMEOUT` | uint32 (ms) | `1000` | Per-operation read timeout for script execution |
| `SCRIPT_DELAY` | uint32 (ms) | `0` | Inter-command delay during script execution |

---

## Building

```bash
mkdir build && cd build
cmake .. -DFTD2XX_ROOT=/path/to/ftd2xx/sdk
make ftdi4232_plugin
```

The output is `libftdi4232_plugin.so` (Linux) or `ftdi4232_plugin.dll` (Windows).

**Required libraries** (must be present in the CMake build tree):

- `ft4232` — FTDI FT4232H driver abstraction (FT4232SPI, FT4232I2C, FT4232GPIO, FT4232UART)
- `ftdi::sdk` — FTDI D2XX SDK (`FTD2XX.dll` / `libftd2xx.so`); on Windows, set `FTD2XX_ROOT` to the SDK root containing `include/ftd2xx.h` and `amd64/` or `i386/` subdirectories
- `uPluginOps`, `uIPlugin`, `uSharedConfig` — plugin framework
- `uICoreScript`, `uCommScriptClient`, `uCommScriptCommandInterpreter`, `uScriptReader` — scripting engine
- `uICommDriver`, `uUtils` — communication driver base and utilities

On Windows, the build also links against `setupapi`, `user32`, and `advapi32`, and copies `FTD2XX64.dll` into the output directory automatically via a post-build step.

---

## Platform Notes

**Linux:**
- Install the FTDI D2XX userspace library from [ftdichip.com](https://ftdichip.com/drivers/d2xx-drivers/).
- Unbind the kernel `ftdi_sio` driver before use: `sudo rmmod ftdi_sio` or add a udev rule. All four channels enumerate independently; the kernel driver will bind to each one it finds.
- The device is addressed by zero-based index (`0` = first FT4232H on the bus).

**Windows:**
- The FTDI D2XX DLL (`FTD2XX.dll`) must be present — either from a system-wide installation or copied next to the plugin (the build does this automatically).
- Set `FTD2XX_ROOT` during CMake configuration to point at the extracted FTDI D2XX SDK.
- Device index `0` selects the first enumerated FT4232H device.

---

## Command Reference

### INFO

Prints version, device index, channel assignments, and a complete command reference for all modules. Takes **no arguments** and works even before `doInit()`.

```
FT4232.INFO
```

---

### SPI

The SPI module drives a selected MPSSE channel (A or B) in SPI master mode. CS is automatically asserted at the start of every transfer and de-asserted at the end.

**FT4232H SPI pin mapping (per selected channel's ADBUS):**

| Signal | ADBUS pin |
|---|---|
| SCK | ADBUS0 |
| MOSI | ADBUS1 |
| MISO | ADBUS2 |
| CS (default) | ADBUS3 (csPin=0x08) |

---

#### SPI · open — Open SPI interface

```
FT4232.SPI open [channel=A|B] [clock=N] [mode=0-3] [bitorder=msb|lsb] [cspin=N] [cspol=low|high] [device=N]
```

| Argument | Description | Default |
|---|---|---|
| `channel` | MPSSE channel: `A` or `B` | INI `SPI_CHANNEL` (default `A`) |
| `clock` | SPI clock frequency in Hz | `1000000` (1 MHz) |
| `mode` | SPI mode 0–3 (CPOL/CPHA) | `0` |
| `bitorder` | `msb` or `lsb` | `msb` |
| `cspin` | Chip-select pin bitmask on ADBUS | `0x08` (ADBUS3) |
| `cspol` | CS polarity: `low` (active-low) or `high` (active-high) | `low` |
| `device` | Zero-based FT4232H device index | `0` |

```
# Channel A at 10 MHz, SPI mode 0
FT4232.SPI open channel=A clock=10000000 mode=0

# Channel B at 5 MHz, LSB-first, active-high CS on ADBUS4
FT4232.SPI open channel=B clock=5000000 bitorder=lsb cspin=0x10 cspol=high

# Use defaults from INI
FT4232.SPI open clock=2000000 mode=3
```

---

#### SPI · close — Release SPI interface

```
FT4232.SPI close
```

---

#### SPI · cfg — Update SPI configuration without reopening

Updates the pending SPI configuration. Changes take effect on the next `open`. Query current state with `?`.

```
FT4232.SPI cfg [clock=N] [mode=0-3] [bitorder=msb|lsb] [cspin=N] [cspol=low|high]
FT4232.SPI cfg ?
```

```
FT4232.SPI cfg clock=5000000 mode=2
FT4232.SPI cfg bitorder=lsb cspol=high
FT4232.SPI cfg ?
```

> Note: `cfg` does not accept `channel=` — channel is fixed at `open` time. To change channel, `close` and `open` with the new channel.

---

#### SPI · cs — CS information

The CS line is driven automatically per-transfer. This command is informational only.

```
FT4232.SPI cs
```

---

#### SPI · write — Transmit bytes (MOSI only)

Sends hex-encoded bytes on MOSI while clocking — MISO data is discarded.

```
FT4232.SPI write <HEXDATA>
```

```
FT4232.SPI write DEADBEEF
FT4232.SPI write 9F
```

---

#### SPI · read — Receive N bytes (clock zeros on MOSI)

Clocks out N zero bytes and captures MISO, then prints it as a hex dump.

```
FT4232.SPI read <N>
```

```
FT4232.SPI read 4
```

---

#### SPI · wrrd — Full-duplex write then read

Writes hex data while simultaneously or sequentially reading back. Accepts three forms:

```
FT4232.SPI wrrd <HEXDATA>:<rdlen>   # write + read (CS held for entire transaction)
FT4232.SPI wrrd :<rdlen>            # read only
FT4232.SPI wrrd <HEXDATA>           # write only
```

When both write data and a read length are specified, uses `spi_transfer()` for true full-duplex operation — the TX buffer is padded with `0x00` for the read phase, and CS is held asserted throughout.

```
# Send JEDEC Read-ID command, read back 3 bytes
FT4232.SPI wrrd 9F:3

# Write 2 bytes, read back 4 bytes
FT4232.SPI wrrd AABB:4

# Read only (clock zeros)
FT4232.SPI wrrd :8
```

---

#### SPI · wrrdf — File-backed write-then-read

Reads write data from a binary file in `ARTEFACTS_PATH` and streams it to the device in chunks.

```
FT4232.SPI wrrdf <filename>[:<wrchunk>][:<rdchunk>]
```

| Argument | Description | Default |
|---|---|---|
| `filename` | Binary file under `ARTEFACTS_PATH` | — |
| `wrchunk` | Write chunk size in bytes | `4096` |
| `rdchunk` | Read chunk size in bytes | `4096` |

```
FT4232.SPI wrrdf flash_image.bin
FT4232.SPI wrrdf flash_image.bin:512:512
```

---

#### SPI · xfer — Full-duplex transfer (simultaneous TX/RX)

Transmits hex-encoded bytes while simultaneously capturing the same count of MISO bytes.

```
FT4232.SPI xfer <HEXDATA>
```

```
FT4232.SPI xfer DEADBEEF
FT4232.SPI xfer AABBCCDD
```

---

#### SPI · script — Execute a command script

**SPI must be open first.**

```
FT4232.SPI script <filename>
```

```
FT4232.SPI script flash_read_id.txt
FT4232.SPI script read_flash.txt
```

---

#### SPI · help — List available sub-commands

```
FT4232.SPI help
```

---

### I2C

The I2C module drives a selected MPSSE channel (A or B) in I2C master mode. Transfers follow standard START / address+R/W / data / STOP framing managed by the driver. The FT4232H's 60 MHz base clock supports up to 3.4 MHz (High-speed mode) with appropriate pull-ups.

**FT4232H I2C pin mapping (per selected channel's ADBUS):**

| Signal | ADBUS pin |
|---|---|
| SCL | ADBUS0 |
| SDA | ADBUS1 + ADBUS2 (bidirectional) |

---

#### I2C · open — Open I2C interface

```
FT4232.I2C open [channel=A|B] [addr=0xNN] [clock=N] [device=N]
```

| Argument | Description | Default |
|---|---|---|
| `channel` | MPSSE channel: `A` or `B` | INI `I2C_CHANNEL` (default `A`) |
| `addr` / `address` | 7-bit I2C target address (hex) | `0x50` |
| `clock` | I2C clock frequency in Hz | `100000` (100 kHz) |
| `device` | Zero-based device index | `0` |

```
# Channel A at 400 kHz, target address 0x50
FT4232.I2C open channel=A addr=0x50 clock=400000

# Channel B at 100 kHz, second device on bus
FT4232.I2C open channel=B addr=0x68 clock=100000 device=1

# High-speed mode on channel A
FT4232.I2C open channel=A addr=0x50 clock=3400000
```

---

#### I2C · close — Release I2C interface

```
FT4232.I2C close
```

---

#### I2C · cfg — Update I2C configuration without reopening

Query current config with `?`.

```
FT4232.I2C cfg [addr=0xNN] [clock=N]
FT4232.I2C cfg ?
```

```
FT4232.I2C cfg addr=0x68 clock=400000
FT4232.I2C cfg clock=1000000
```

> Note: `cfg` does not accept `channel=`. To change channel, `close` and `open` with the new channel.

---

#### I2C · write — Transmit bytes to the target device

Issues START, sends address + W, writes data bytes, then STOP.

```
FT4232.I2C write <HEXDATA>
```

```
FT4232.I2C write 00
FT4232.I2C write 01FF
```

---

#### I2C · read — Receive N bytes from the target device

Issues START, sends address + R, reads N bytes (ACKs all but last), then STOP.

```
FT4232.I2C read <N>
```

```
FT4232.I2C read 2
FT4232.I2C read 16
```

---

#### I2C · wrrd — Write then read (combined transfer)

Sends write data then follows with a read phase. Either phase can be omitted.

```
FT4232.I2C wrrd <HEXDATA>:<rdlen>   # write + read
FT4232.I2C wrrd :<rdlen>            # read only
FT4232.I2C wrrd <HEXDATA>           # write only
```

```
# Write register address 0x0000, read back 2 bytes
FT4232.I2C wrrd 0000:2

# Send measurement trigger 0xF3, read 3-byte result
FT4232.I2C wrrd F3:3

# Read 8 bytes without a preceding write
FT4232.I2C wrrd :8
```

---

#### I2C · wrrdf — File-backed write-then-read

```
FT4232.I2C wrrdf <filename>[:<wrchunk>][:<rdchunk>]
```

```
FT4232.I2C wrrdf eeprom_data.bin
FT4232.I2C wrrdf i2c_seq.bin:64:64
```

---

#### I2C · scan — Probe all I2C addresses

Probes address space `0x08–0x77` by briefly opening a temporary driver at each address and attempting a zero-byte write. Reports all responding devices. Uses the current `clockHz`, `channel`, and `device` from the pending I2C config — no prior `open` is required.

```
FT4232.I2C scan
```

**Example output:**
```
FT_GENERIC | I2C: Scanning I2C bus...
FT_GENERIC | I2C: Found device at 0x50
FT_GENERIC | I2C: Found device at 0x68
```

---

#### I2C · script — Execute a command script

**I2C must be open first.**

```
FT4232.I2C script <filename>
```

```
FT4232.I2C script eeprom_test.txt
FT4232.I2C script sensor_init.txt
```

---

#### I2C · help — List available sub-commands

```
FT4232.I2C help
```

---

### GPIO

The GPIO module exposes the FT4232H MPSSE GPIO pins as two 8-bit banks per channel:

| Bank | Pins | Direction bit |
|---|---|---|
| `low` | ADBUS[7:0] | 1 = output, 0 = input |
| `high` | ACBUS[7:0] | 1 = output, 0 = input |

The default GPIO channel is **B** (set by `GPIO_CHANNEL` in the INI), which keeps GPIO on a separate MPSSE channel from SPI and I2C (which default to channel A).

> **Shared-channel caution:** If GPIO and SPI or I2C share the same channel (A or B), ADBUS[3:0] are under MPSSE protocol control. In that case use only ADBUS[7:4] (upper nibble of the low bank) or ACBUS[7:0] (entire high bank) for user-controlled GPIO on a shared channel.

---

#### GPIO · open — Open GPIO interface

```
FT4232.GPIO open [channel=A|B] [device=N] [lowdir=0xNN] [lowval=0xNN] [highdir=0xNN] [highval=0xNN]
```

| Argument | Description | Default |
|---|---|---|
| `channel` | MPSSE channel: `A` or `B` | INI `GPIO_CHANNEL` (default `B`) |
| `device` | Zero-based device index | `0` |
| `lowdir` | Direction mask for low bank (ADBUS): `1`=output, `0`=input | `0x00` (all inputs) |
| `lowval` | Initial output values for low bank | `0x00` |
| `highdir` | Direction mask for high bank (ACBUS) | `0x00` (all inputs) |
| `highval` | Initial output values for high bank | `0x00` |

```
# Channel B — all low-bank pins as outputs, initial 0
FT4232.GPIO open channel=B lowdir=0xFF lowval=0x00

# Channel A — mixed direction, preset high bank
FT4232.GPIO open channel=A lowdir=0xF0 highdir=0xFF highval=0xAA

# Default channel (from INI), specify only directions
FT4232.GPIO open lowdir=0x0F highdir=0xFF
```

---

#### GPIO · close — Release GPIO interface

```
FT4232.GPIO close
```

---

#### GPIO · cfg — Update GPIO configuration without reopening

Updates the pending direction/value masks and channel. Takes effect on the next `open`. Query with `?`.

```
FT4232.GPIO cfg [channel=A|B] [lowdir=0xNN] [lowval=0xNN] [highdir=0xNN] [highval=0xNN]
FT4232.GPIO cfg ?
```

```
FT4232.GPIO cfg lowdir=0x0F highdir=0xFF
FT4232.GPIO cfg channel=A
```

---

#### GPIO · dir — Set pin direction at runtime

Applies a direction mask to a bank while the GPIO interface is open. `1` bits select output; `0` bits select input.

```
FT4232.GPIO dir [low|high] <MASK>
```

```
# All low-bank pins as outputs
FT4232.GPIO dir low 0xFF

# High-bank bits [3:0] as outputs, [7:4] as inputs
FT4232.GPIO dir high 0x0F
```

---

#### GPIO · write — Write a full byte to a bank

Writes an absolute 8-bit value to the specified bank.

```
FT4232.GPIO write [low|high] <VALUE>
```

```
FT4232.GPIO write low 0xAA
FT4232.GPIO write high 0x01
```

---

#### GPIO · set — Drive masked pins HIGH

```
FT4232.GPIO set [low|high] <MASK>
```

```
# ADBUS[0] high
FT4232.GPIO set low 0x01

# ACBUS[7] high
FT4232.GPIO set high 0x80
```

---

#### GPIO · clear — Drive masked pins LOW

```
FT4232.GPIO clear [low|high] <MASK>
```

```
FT4232.GPIO clear low 0x01
FT4232.GPIO clear high 0xF0
```

---

#### GPIO · toggle — Invert masked output pins

```
FT4232.GPIO toggle [low|high] <MASK>
```

```
FT4232.GPIO toggle low 0xFF
FT4232.GPIO toggle high 0x01
```

---

#### GPIO · read — Read current pin levels from a bank

Returns the current logical level of all 8 pins in the bank (regardless of direction), printed as a hex value and binary string (MSB first).

```
FT4232.GPIO read [low|high]
```

```
FT4232.GPIO read low
FT4232.GPIO read high
```

**Example output:**
```
FT_GPIO    | Bank low: 0x5A  [01011010]
```

---

#### GPIO · help — List available sub-commands

```
FT4232.GPIO help
```

---

### UART

The UART module opens one of the FT4232H async serial channels (C or D). Because channels C and D each have their own D2XX handle, **two independent UART ports can be open simultaneously** — one on channel C and one on channel D.

> Channels A and B are MPSSE-only. Attempting to open UART with `channel=A` or `channel=B` is rejected at runtime.

> Maximum baud rate: approximately 3 Mbps (60 MHz / 20). The named presets `1M` and `3M` can be used for these high rates.

---

#### UART · open — Open UART interface

```
FT4232.UART open [channel=C|D] [baud=N] [data=8] [stop=1] [parity=none|odd|even|mark|space] [flow=none|hw] [device=N]
```

| Argument | Description | Default |
|---|---|---|
| `channel` | Async UART channel: `C` or `D` | INI `UART_CHANNEL` (default `C`) |
| `baud` | Baud rate | `115200` |
| `data` | Data bits | `8` |
| `stop` | Stop bits | `1` |
| `parity` | `none`, `odd`, `even`, `mark`, or `space` | `none` |
| `flow` | `none` or `hw` (RTS/CTS hardware flow control) | `none` |
| `device` | Zero-based device index | `0` |

```
FT4232.UART open channel=C baud=115200
FT4232.UART open channel=D baud=921600 data=8 stop=1 parity=none
FT4232.UART open channel=C baud=3M flow=hw
FT4232.UART open channel=C baud=9600 parity=even stop=1 flow=hw device=1
```

---

#### UART · close — Release UART interface

```
FT4232.UART close
```

---

#### UART · cfg — Update UART parameters

Parameters can be updated while UART is open (applied immediately via `configure()`) or stored for the next `open`.

```
FT4232.UART cfg [channel=C|D] [baud=N] [data=8] [stop=1] [parity=none|odd|even|mark|space] [flow=none|hw]
```

```
FT4232.UART cfg baud=9600 parity=even
FT4232.UART cfg baud=115200 flow=hw
FT4232.UART cfg channel=D
```

---

#### UART · write — Transmit hex bytes over UART

```
FT4232.UART write <HEXDATA>
```

```
# Send 4 bytes
FT4232.UART write DEADBEEF

# Send ASCII "Hello"
FT4232.UART write 48656C6C6F
```

---

#### UART · read — Receive N bytes over UART

Blocks until N bytes are received or `READ_TIMEOUT` (from INI) expires. Received bytes are printed as a hex dump.

```
FT4232.UART read <N>
```

```
FT4232.UART read 4
FT4232.UART read 64
```

---

#### UART · script — Execute a command script

**UART must be open first.**

```
FT4232.UART script <filename>
```

```
FT4232.UART script comms_sequence.txt
FT4232.UART script modem_init.txt
```

---

#### UART · help — List available sub-commands

```
FT4232.UART help
```

---

## SPI Clock Reference

Named presets for the FT4232H (60 MHz clock base, max 30 MHz SPI). Raw Hz values are also accepted.

| Preset label | Frequency |
|---|---|
| `100kHz` | 100 kHz |
| `500kHz` | 500 kHz |
| `1MHz` | 1 MHz |
| `2MHz` | 2 MHz |
| `5MHz` | 5 MHz |
| `10MHz` | 10 MHz |
| `20MHz` | 20 MHz |
| `30MHz` | 30 MHz (maximum) |

---

## I2C Speed Reference

Named presets for the FT4232H (supports up to 3.4 MHz high-speed mode). Raw Hz values are also accepted.

| Preset label | Frequency | Mode |
|---|---|---|
| `50kHz` | 50 kHz | Low-speed |
| `100kHz` | 100 kHz | Standard |
| `400kHz` | 400 kHz | Fast |
| `1MHz` | 1 MHz | Fast-plus |
| `3.4MHz` | 3.4 MHz | High-speed |

---

## UART Baud Rate Reference

Named baud rate presets for channels C and D:

| Preset | Baud rate |
|---|---|
| `9600` | 9600 |
| `19200` | 19200 |
| `38400` | 38400 |
| `57600` | 57600 |
| `115200` | 115200 (default) |
| `230400` | 230400 |
| `460800` | 460800 |
| `921600` | 921600 |
| `1M` | 1,000,000 |
| `3M` | 3,000,000 (near maximum) |

Raw integer values are also accepted.

---

## Script Files

Script files are plain text files located under `ARTEFACTS_PATH`. They are executed by the `CommScriptClient` engine, which reads each line and performs send/receive/expect operations. The `SCRIPT_DELAY` INI key inserts a per-command delay in milliseconds.

The corresponding interface must be open before calling `script`. The plugin passes the already-open driver handle directly to `CommScriptClient`, so no reconnection occurs inside the script.

```
# All four interfaces open simultaneously on separate channels
FT4232.SPI  open channel=A clock=10000000
FT4232.I2C  open channel=A addr=0x50 clock=400000
FT4232.GPIO open channel=B lowdir=0xFF
FT4232.UART open channel=C baud=115200

FT4232.SPI  script flash_read_id.txt
FT4232.I2C  script eeprom_test.txt
FT4232.UART script comms_sequence.txt

FT4232.SPI  close
FT4232.I2C  close
FT4232.GPIO close
FT4232.UART close
```

---

## Fault-Tolerant and Dry-Run Modes

- **Dry-run mode**: when `doEnable()` has not been called, every command validates its arguments (including channel constraints) and returns `true` without touching hardware. The generic dispatcher detects `isEnabled() == false` and returns early after argument parsing. This allows test framework validators to check command syntax before a live run.

- **Fault-tolerant mode** (`setFaultTolerant()` / `isFaultTolerant()`): when set, the plugin framework can be configured to continue execution past command failures. Useful in production test scripts where a non-critical probe failure should not abort a longer sequence.

- **Privileged mode** (`isPrivileged()`): always returns `false`; reserved for future framework use.

---

## Error Handling and Return Values

Every handler returns `bool`:
- `true` — success (or argument validation passed in disabled/dry-run mode).
- `false` — argument validation failed, unknown sub-command, invalid channel for the module (e.g. `channel=A` on UART), driver open failed, hardware operation returned an error status, or file not found.

All four module drivers return a typed `Status` enum (`SUCCESS`, and various error values). The plugin checks these and logs an error then returns `false` on any non-`SUCCESS` status.

Diagnostic messages are emitted via `LOG_PRINT` at several severity levels:

| Level | Usage |
|---|---|
| `LOG_ERROR` | Command failed, invalid argument, channel constraint violation, hardware error |
| `LOG_WARNING` | Non-fatal issue (e.g., closing a port that was not open) |
| `LOG_INFO` | Successful operations (bytes written, device opened, clock updated, etc.) |
| `LOG_VERBOSE` | INI parameter loading details |
| `LOG_FIXED` | Help text output and scan results |

Log verbosity is controlled by the host application via the shared `uLogger` configuration.
