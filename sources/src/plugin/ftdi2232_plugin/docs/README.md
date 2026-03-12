# FT2232 Plugin

A C++ shared-library plugin that exposes the [FTDI FT2232](https://ftdichip.com/products/ft2232h-mini-module/) adapter through a unified command dispatcher. The plugin supports **two hardware variants** in a single binary:

- **FT2232H** — Hi-Speed USB 2.0, dual MPSSE channels (A and B), 60 MHz clock base, max 30 MHz SPI / 1 MHz I2C
- **FT2232D / FT2232C / FT2232L** — Full-Speed USB 2.0, single MPSSE on channel A, 6 MHz clock base, max 3 MHz SPI / ~400 kHz I2C; channel B provides an async UART

The active variant is selected per-command via `variant=H|D` (or set globally in the INI file). It determines the USB PID searched during enumeration, the MPSSE clock base, the legal channel set, and hardware speed limits. All four interfaces — **SPI**, **I2C**, **GPIO**, and **UART** — are exposed through the same string-command dispatch mechanism.

**Version:** 1.0.0.0  
**Requires:** C++20

> **UART note:** The FT2232H has no dedicated async serial channel. Attempting to open UART with `variant=H` is rejected at runtime. UART is available on **FT2232D only** (channel B).

---

## Table of Contents

1. [Overview](#overview)
2. [Variant Comparison](#variant-comparison)
3. [Project Structure](#project-structure)
4. [Architecture](#architecture)
   - [Plugin Lifecycle](#plugin-lifecycle)
   - [Variant and Channel Selection](#variant-and-channel-selection)
   - [Hardware Speed Limits](#hardware-speed-limits)
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
FT2232.SPI  open variant=H clock=10000000 mode=0 channel=A
FT2232.SPI  wrrd 9F:3
FT2232.I2C  open variant=H addr=0x50 clock=400000 channel=B
FT2232.I2C  scan
FT2232.GPIO open variant=H channel=B lowdir=0xFF
FT2232.GPIO set  low 0x01
FT2232.UART open variant=D baud=115200
```

Each interface must be explicitly **opened** and **closed**. On the FT2232H, SPI, I2C, and GPIO can run simultaneously on different channels (A and B). On the FT2232D, only channel A supports MPSSE; channel B is the async UART.

---

## Variant Comparison

| Feature | FT2232H | FT2232D / C / L |
|---|---|---|
| USB speed | Hi-Speed (480 Mbps) | Full-Speed (12 Mbps) |
| MPSSE clock base | 60 MHz | 6 MHz |
| MPSSE channels | A and B | A only |
| Max SPI clock | 30 MHz | 3 MHz |
| Max I2C clock | ~1 MHz (fast-plus) | ~400 kHz |
| UART | ✗ (no dedicated channel) | ✓ (channel B, async VCP) |
| `variant=` value | `H` | `D` |

---

## Project Structure

```
ftdi2232_plugin/
├── CMakeLists.txt                  # Build definition (shared library, C++20)
├── inc/
│   ├── ft2232_plugin.hpp           # Main class + command tables + pending config structs
│   ├── ft2232_generic.hpp          # Generic template helpers + write/read/script helpers
│   └── private/
│       ├── spi_config.hpp          # SPI_COMMANDS_CONFIG_TABLE + SPI_SPEED_CONFIG_TABLE
│       ├── i2c_config.hpp          # I2C_COMMANDS_CONFIG_TABLE + I2C_SPEED_CONFIG_TABLE
│       ├── gpio_config.hpp         # GPIO_COMMANDS_CONFIG_TABLE
│       └── uart_config.hpp         # UART_COMMANDS_CONFIG_TABLE + UART_SPEED_CONFIG_TABLE
└── src/
    ├── ft2232_plugin.cpp           # Entry points, init/cleanup, INFO, INI loading,
    │                               # parseVariant, parseChannel, checkVariantSpeedLimit
    ├── ft2232_spi.cpp              # SPI sub-command implementations
    ├── ft2232_i2c.cpp              # I2C sub-command implementations
    ├── ft2232_gpio.cpp             # GPIO sub-command implementations
    └── ft2232_uart.cpp             # UART sub-command implementations (FT2232D only)
```

Each protocol lives in its own `.cpp` file. Command and speed tables are defined in the config headers using X-macros, following the same pattern as all other FTDI plugins in this project.

---

## Architecture

### Plugin Lifecycle

```
pluginEntry()               → creates FT2232Plugin instance
  setParams()               → loads INI values (variant, channels, speeds, timeouts…)
  doInit()                  → propagates INI defaults into all pending config structs;
                              logs variant and device index; marks initialized
  doEnable()                → enables real execution (without this, commands only validate args)
  doDispatch(cmd, args)     → routes to the correct top-level or module handler
  FT2232.SPI open ...       → opens SPI driver (FT2232SPI) — variant + channel enforced here
  FT2232.SPI close          → closes SPI driver
  (similar for I2C / GPIO / UART)
  doCleanup()               → closes all four drivers, resets state
pluginExit(ptr)             → deletes the FT2232Plugin instance
```

`doEnable()` controls a **dry-run / argument-validation mode**: when not enabled, commands parse their arguments and return `true` without touching hardware. This is used by test frameworks to verify command syntax before a live run.

`doInit()` does **not** open any hardware interface. Opening happens explicitly via the per-module `open` sub-command.

### Variant and Channel Selection

The `variant` (H or D) and `channel` (A or B) can be set in three ways, in decreasing priority:

1. **Per-command** — `variant=H` / `channel=B` in the `open` or `cfg` argument string
2. **INI file** — `VARIANT`, `SPI_CHANNEL`, `I2C_CHANNEL`, `GPIO_CHANNEL` keys (loaded by `setParams`)
3. **Compiled defaults** — `FT2232H`, channel A for SPI/I2C, channel B for GPIO

The variant is stored inside each module's pending config struct and forwarded to the driver's `open()` call. The static helper `parseVariant()` accepts `H`, `h`, `FT2232H`, `2232H` (or the D equivalents). The static helper `parseChannel()` accepts `A`/`a` or `B`/`b`.

FT2232D channel enforcement is checked at `open` time for each module:

```cpp
// FT2232D only has MPSSE on channel A — checked before every open
if (variant == FT2232D && channel != Channel::A) → error, return false
```

### Hardware Speed Limits

`checkVariantSpeedLimit()` is called before every `open` and speed-change operation:

```
FT2232D + SPI → max 3,000,000 Hz
FT2232D + I2C → max 400,000 Hz
FT2232H       → no plugin-level cap (driver and hardware determine the limit)
```

Exceeding these limits returns `false` with a descriptive log error before the driver is ever opened. This means invalid configurations fail fast without leaving a half-open USB handle.

### Command Dispatch Model

The dispatch model uses two layers of `std::map`:

1. **Top-level map** (`m_mapCmds`): maps command names (`INFO`, `SPI`, `I2C`, `GPIO`, `UART`) to member-function pointers on `FT2232Plugin`.
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

// In the constructor (ft2232_plugin.hpp):
#define SPI_CMD_RECORD(a) \
    m_mapCmds_SPI.insert({#a, &FT2232Plugin::m_handle_spi_##a});
SPI_COMMANDS_CONFIG_TABLE
#undef SPI_CMD_RECORD
```

Adding a new sub-command requires only one line in the config table and one handler function.

### Generic Template Helpers

`ft2232_generic.hpp` provides stateless template functions shared by all modules:

| Function | Purpose |
|---|---|
| `generic_module_dispatch<T>()` | Splits `"subcmd args"` and routes to the correct module handler |
| `generic_module_set_speed<T>()` | Looks up a speed label (or raw Hz), checks variant limits, calls `setModuleSpeed()` |
| `generic_write_data<T>()` | Unhexlifies a hex string and calls a write callback (up to 65536 bytes) |
| `generic_write_read_data<T>()` | Parses `HEXDATA:rdlen` and calls a write-then-read callback |
| `generic_write_read_file<T>()` | Reads write data from a binary file in `ARTEFACTS_PATH`, streams results in chunks |
| `generic_execute_script<T>()` | Runs a `CommScriptClient` script on an open driver |
| `generic_module_list_commands<T>()` | Logs all registered sub-command names (used by `help`) |

The speed help text in `generic_module_set_speed` includes a note reminding users of the FT2232D caps.

Two limits are defined for bulk data operations:

```cpp
FT_WRITE_MAX_CHUNK_SIZE  = 4096     // Default chunk size for file-based transfers
FT_BULK_MAX_BYTES        = 65536    // MPSSE max per-transfer limit
```

### Pending Configuration Structs

Each module maintains a "pending configuration" struct that carries both the variant/channel selection and the protocol parameters. All fields are populated from INI defaults at `doInit()` time, then updated per-command by `open` and `cfg`.

| Struct | Fields | Default |
|---|---|---|
| `SpiPendingCfg` | `clockHz`, `mode`, `bitOrder`, `csPin`, `csPolarity`, `variant`, `channel` | 1 MHz, mode=0, MSB, csPin=0x08, active-low, H, A |
| `I2cPendingCfg` | `address`, `clockHz`, `variant`, `channel` | addr=0x50, 100 kHz, H, A |
| `GpioPendingCfg` | `variant`, `channel`, `lowDirMask`, `lowValue`, `highDirMask`, `highValue` | H, **B** (default GPIO channel), all inputs, all 0 |
| `UartPendingCfg` | `baudRate`, `dataBits`, `stopBits`, `parity`, `hwFlowCtrl`, `variant` | 115200, 8, 1, none, false, D |

Note that GPIO defaults to **channel B** — the INI key `GPIO_CHANNEL` controls this, and the rationale is that channel A is typically consumed by SPI or I2C on the FT2232H.

### INI Configuration Keys

| Key | Type | Default | Description |
|---|---|---|---|
| `DEVICE_INDEX` | uint8 | `0` | Zero-based FTDI device index passed to D2XX |
| `VARIANT` | string | `H` | Default hardware variant: `H` (FT2232H) or `D` (FT2232D) |
| `ARTEFACTS_PATH` | string | `""` | Base directory for script and binary data files |
| `SPI_CHANNEL` | char | `A` | Default SPI MPSSE channel: `A` or `B` |
| `I2C_CHANNEL` | char | `A` | Default I2C MPSSE channel: `A` or `B` |
| `GPIO_CHANNEL` | char | `B` | Default GPIO MPSSE channel: `A` or `B` |
| `SPI_CLOCK` | uint32 (Hz) | `1000000` | Initial SPI clock frequency |
| `I2C_CLOCK` | uint32 (Hz) | `100000` | Initial I2C clock frequency |
| `I2C_ADDRESS` | uint8 (hex) | `0x50` | Default I2C target device address |
| `READ_TIMEOUT` | uint32 (ms) | `1000` | Per-operation read timeout for script execution |
| `SCRIPT_DELAY` | uint32 (ms) | `0` | Inter-command delay during script execution |
| `UART_BAUD` | uint32 | `115200` | Default UART baud rate |

---

## Building

```bash
mkdir build && cd build
cmake .. -DFTD2XX_ROOT=/path/to/ftd2xx/sdk
make ftdi2232_plugin
```

The output is `libftdi2232_plugin.so` (Linux) or `ftdi2232_plugin.dll` (Windows).

**Required libraries** (must be present in the CMake build tree):

- `ft2232` — FTDI FT2232 driver abstraction (FT2232SPI, FT2232I2C, FT2232GPIO, FT2232UART)
- `ftdi::sdk` — FTDI D2XX SDK (`FTD2XX.dll` / `libftd2xx.so`); on Windows, set `FTD2XX_ROOT` to the SDK root containing `include/ftd2xx.h` and `amd64/` or `i386/` subdirectories
- `uPluginOps`, `uIPlugin`, `uSharedConfig` — plugin framework
- `uICoreScript`, `uCommScriptClient`, `uCommScriptCommandInterpreter`, `uScriptReader` — scripting engine
- `uICommDriver`, `uUtils` — communication driver base and utilities

On Windows, the build also links against `setupapi`, `user32`, and `advapi32`, and copies `FTD2XX64.dll` into the output directory automatically via a post-build step.

---

## Platform Notes

**Linux:**
- Install the FTDI D2XX userspace library from [ftdichip.com](https://ftdichip.com/drivers/d2xx-drivers/).
- You may need to unbind the `ftdi_sio` kernel driver: `sudo rmmod ftdi_sio` or add a udev rule to prevent auto-binding.
- The device is addressed by zero-based enumeration index (`0` = first FT2232 on the bus).
- Different variants (FT2232H vs FT2232D) have different USB PIDs, so the D2XX library enumerates them separately. Adjust `DEVICE_INDEX` if both variants are connected.

**Windows:**
- The FTDI D2XX DLL (`FTD2XX.dll`) must be present — either from a system-wide installation or copied next to the plugin (the build does this automatically).
- Set `FTD2XX_ROOT` during CMake configuration to point at the extracted FTDI D2XX SDK.
- Device index `0` selects the first enumerated FT2232 device; use `1`, `2`, etc. for additional devices.

---

## Command Reference

### INFO

Prints version, active variant, and a complete command reference for all modules. Takes **no arguments** and works even before `doInit()`.

```
FT2232.INFO
```

---

### SPI

The SPI module drives the FT2232 MPSSE engine in SPI master mode. CS is automatically asserted at the start of every transfer and de-asserted at the end.

**FT2232 SPI pin mapping (per selected channel's ADBUS):**

| Signal | ADBUS pin |
|---|---|
| SCK | ADBUS0 |
| MOSI | ADBUS1 |
| MISO | ADBUS2 |
| CS (default) | ADBUS3 (csPin=0x08) |

---

#### SPI · open — Open SPI interface

```
FT2232.SPI open [variant=H|D] [clock=N] [mode=0-3] [bitorder=msb|lsb] [cspin=N] [cspol=low|high] [channel=A|B] [device=N]
```

| Argument | Description | Default |
|---|---|---|
| `variant` | `H` = FT2232H, `D` = FT2232D | INI `VARIANT` (default `H`) |
| `clock` | SPI clock frequency in Hz | `1000000` (1 MHz) |
| `mode` | SPI mode 0–3 (CPOL/CPHA) | `0` |
| `bitorder` | `msb` or `lsb` | `msb` |
| `cspin` | Chip-select pin bitmask on ADBUS | `0x08` (ADBUS3) |
| `cspol` | CS polarity: `low` (active-low) or `high` (active-high) | `low` |
| `channel` | MPSSE channel: `A` or `B` | INI `SPI_CHANNEL` (default `A`) |
| `device` | Zero-based FT2232 device index | `0` |

> **FT2232D restriction:** channel A only, max clock 3 MHz. Specifying `channel=B` or `clock` > 3 MHz with `variant=D` returns an error.

```
# FT2232H on channel A at 10 MHz
FT2232.SPI open variant=H clock=10000000 mode=0 channel=A

# FT2232H on channel B at 5 MHz, LSB-first
FT2232.SPI open variant=H clock=5000000 bitorder=lsb channel=B

# FT2232D at 1 MHz (channel A only)
FT2232.SPI open variant=D clock=1000000 channel=A

# Active-high CS on ADBUS4
FT2232.SPI open clock=2000000 cspin=0x10 cspol=high
```

---

#### SPI · close — Release SPI interface

```
FT2232.SPI close
```

---

#### SPI · cfg — Update SPI configuration without reopening

Updates the pending SPI configuration. Changes take effect on the next `open`. Query current state with `?`.

```
FT2232.SPI cfg [variant=H|D] [clock=N] [mode=0-3] [bitorder=msb|lsb] [cspin=N] [cspol=low|high]
FT2232.SPI cfg ?
```

```
FT2232.SPI cfg clock=5000000 mode=2
FT2232.SPI cfg variant=D clock=1000000
FT2232.SPI cfg ?
```

---

#### SPI · cs — CS information

The CS line is driven automatically per-transfer. This command is informational only.

```
FT2232.SPI cs
```

---

#### SPI · write — Transmit bytes (MOSI only)

Sends hex-encoded bytes on MOSI while clocking — MISO data is discarded.

```
FT2232.SPI write <HEXDATA>
```

```
FT2232.SPI write DEADBEEF
FT2232.SPI write 9F
```

---

#### SPI · read — Receive N bytes (clock zeros on MOSI)

Clocks out N zero bytes and captures MISO, then prints it as a hex dump.

```
FT2232.SPI read <N>
```

```
FT2232.SPI read 4
```

---

#### SPI · wrrd — Full-duplex write then read

Writes hex data while simultaneously or sequentially reading back. Accepts three forms:

```
FT2232.SPI wrrd <HEXDATA>:<rdlen>   # write + read
FT2232.SPI wrrd :<rdlen>            # read only
FT2232.SPI wrrd <HEXDATA>           # write only
```

Uses `spi_transfer()` for true full-duplex operation when both write data and a read length are specified.

```
# Send JEDEC Read-ID command, read back 3 bytes
FT2232.SPI wrrd 9F:3

# Write 2 bytes, read back 4 bytes
FT2232.SPI wrrd AABB:4

# Read only (clock zeros)
FT2232.SPI wrrd :8
```

---

#### SPI · wrrdf — File-backed write-then-read

Reads write data from a binary file in `ARTEFACTS_PATH` and streams it to the device in chunks.

```
FT2232.SPI wrrdf <filename>[:<wrchunk>][:<rdchunk>]
```

| Argument | Description | Default |
|---|---|---|
| `filename` | Binary file under `ARTEFACTS_PATH` | — |
| `wrchunk` | Write chunk size in bytes | `4096` |
| `rdchunk` | Read chunk size in bytes | `4096` |

```
FT2232.SPI wrrdf flash_image.bin
FT2232.SPI wrrdf flash_image.bin:512:512
```

---

#### SPI · xfer — Full-duplex transfer (simultaneous TX/RX)

Transmits hex-encoded bytes while simultaneously capturing the same count of MISO bytes.

```
FT2232.SPI xfer <HEXDATA>
```

```
FT2232.SPI xfer DEADBEEF
FT2232.SPI xfer AABBCCDD
```

---

#### SPI · script — Execute a command script

**SPI must be open first.**

```
FT2232.SPI script <filename>
```

```
FT2232.SPI script flash_read_id.txt
FT2232.SPI script read_flash.txt
```

---

#### SPI · help — List available sub-commands

```
FT2232.SPI help
```

---

### I2C

The I2C module drives the FT2232 MPSSE engine in I2C master mode. Transfers follow standard START / address+R/W / data / STOP framing managed by the driver.

**FT2232 I2C pin mapping (per selected channel's ADBUS):**

| Signal | ADBUS pin |
|---|---|
| SCL | ADBUS0 |
| SDA | ADBUS1 + ADBUS2 (bidirectional) |

> **FT2232D restriction:** channel A only, practical max clock ~400 kHz (6 MHz base / ((1+6)×2) ≈ 428 kHz). The FT2232H supports up to ~1 MHz (fast-plus) on either channel.

---

#### I2C · open — Open I2C interface

```
FT2232.I2C open [variant=H|D] [channel=A|B] [addr=0xNN] [clock=N] [device=N]
```

| Argument | Description | Default |
|---|---|---|
| `variant` | `H` = FT2232H, `D` = FT2232D | INI `VARIANT` |
| `channel` | MPSSE channel: `A` or `B` | INI `I2C_CHANNEL` (default `A`) |
| `addr` / `address` | 7-bit I2C target address (hex) | `0x50` |
| `clock` | I2C clock frequency in Hz | `100000` (100 kHz) |
| `device` | Zero-based device index | `0` |

```
# FT2232H on channel A at 400 kHz
FT2232.I2C open variant=H addr=0x50 clock=400000 channel=A

# FT2232H on channel B (freeing channel A for SPI)
FT2232.I2C open addr=0x50 clock=1MHz channel=B

# FT2232D (channel A only, max 400 kHz)
FT2232.I2C open variant=D addr=0x68 clock=100000
```

---

#### I2C · close — Release I2C interface

```
FT2232.I2C close
```

---

#### I2C · cfg — Update I2C configuration without reopening

Query current config with `?`.

```
FT2232.I2C cfg [variant=H|D] [addr=0xNN] [clock=N]
FT2232.I2C cfg ?
```

```
FT2232.I2C cfg addr=0x68 clock=400000
FT2232.I2C cfg variant=D clock=100000
```

---

#### I2C · write — Transmit bytes to the target device

Issues START, sends address + W, writes data bytes, then STOP.

```
FT2232.I2C write <HEXDATA>
```

```
FT2232.I2C write 00
FT2232.I2C write 01FF
```

---

#### I2C · read — Receive N bytes from the target device

Issues START, sends address + R, reads N bytes (ACKs all but last), then STOP.

```
FT2232.I2C read <N>
```

```
FT2232.I2C read 2
FT2232.I2C read 16
```

---

#### I2C · wrrd — Write then read (combined transfer)

Sends write data then follows with a read phase. Either phase can be omitted.

```
FT2232.I2C wrrd <HEXDATA>:<rdlen>   # write + read
FT2232.I2C wrrd :<rdlen>            # read only
FT2232.I2C wrrd <HEXDATA>           # write only
```

```
# Write register address 0x0000, read back 2 bytes
FT2232.I2C wrrd 0000:2

# Send measurement trigger 0xF3, read 3 bytes result
FT2232.I2C wrrd F3:3

# Read 8 bytes without a preceding write
FT2232.I2C wrrd :8
```

---

#### I2C · wrrdf — File-backed write-then-read

```
FT2232.I2C wrrdf <filename>[:<wrchunk>][:<rdchunk>]
```

```
FT2232.I2C wrrdf eeprom_data.bin
FT2232.I2C wrrdf i2c_seq.bin:64:64
```

---

#### I2C · scan — Probe all I2C addresses

Probes address space `0x08–0x77` by briefly opening a temporary driver at each address and attempting a zero-byte write. Reports all responding devices. Uses the current `clockHz`, `variant`, `channel`, and `device` from the pending I2C config — no prior `open` is required.

```
FT2232.I2C scan
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
FT2232.I2C script <filename>
```

```
FT2232.I2C script eeprom_test.txt
FT2232.I2C script sensor_init.txt
```

---

#### I2C · help — List available sub-commands

```
FT2232.I2C help
```

---

### GPIO

The GPIO module exposes the FT2232 MPSSE GPIO pins as two 8-bit banks per channel:

| Bank | Pins | Direction bit |
|---|---|---|
| `low` | ADBUS[7:0] | 1 = output, 0 = input |
| `high` | ACBUS[7:0] | 1 = output, 0 = input |

The default GPIO channel is **B** (set by `GPIO_CHANNEL` in the INI), allowing the FT2232H to run SPI or I2C on channel A and GPIO on channel B simultaneously.

> **FT2232D restriction:** channel A only. On FT2232D, ADBUS[3:0] are shared with the MPSSE protocol lines — avoid driving them while SPI or I2C is active on the same channel.

> **FT2232H shared-channel caution:** if GPIO and SPI/I2C share the same channel, ADBUS[3:0] are under MPSSE protocol control. Use only ADBUS[7:4] (low bank, upper nibble) or ACBUS[7:0] (high bank) for user GPIO on a shared channel.

---

#### GPIO · open — Open GPIO interface

```
FT2232.GPIO open [variant=H|D] [channel=A|B] [device=N] [lowdir=0xNN] [lowval=0xNN] [highdir=0xNN] [highval=0xNN]
```

| Argument | Description | Default |
|---|---|---|
| `variant` | `H` = FT2232H, `D` = FT2232D | INI `VARIANT` |
| `channel` | MPSSE channel: `A` or `B` | INI `GPIO_CHANNEL` (default `B`) |
| `device` | Zero-based device index | `0` |
| `lowdir` | Direction mask for low bank (ADBUS): `1`=output, `0`=input | `0x00` (all inputs) |
| `lowval` | Initial output values for low bank | `0x00` |
| `highdir` | Direction mask for high bank (ACBUS) | `0x00` (all inputs) |
| `highval` | Initial output values for high bank | `0x00` |

```
# FT2232H channel B — all low-bank pins as outputs
FT2232.GPIO open variant=H channel=B lowdir=0xFF lowval=0x00

# FT2232H — both banks partially configured
FT2232.GPIO open variant=H channel=B lowdir=0xF0 highdir=0xFF

# FT2232D — channel A only
FT2232.GPIO open variant=D channel=A lowdir=0x0F highdir=0xFF

# Default (uses INI variant and GPIO_CHANNEL)
FT2232.GPIO open lowdir=0xFF highval=0xAA
```

---

#### GPIO · close — Release GPIO interface

```
FT2232.GPIO close
```

---

#### GPIO · cfg — Update GPIO configuration without reopening

Updates the pending direction/value masks and variant/channel. Takes effect on the next `open`. Query with `?`.

```
FT2232.GPIO cfg [variant=H|D] [channel=A|B] [lowdir=0xNN] [lowval=0xNN] [highdir=0xNN] [highval=0xNN]
FT2232.GPIO cfg ?
```

```
FT2232.GPIO cfg lowdir=0x0F highdir=0xFF
FT2232.GPIO cfg variant=H channel=B
```

---

#### GPIO · dir — Set pin direction at runtime

Applies a direction mask to a bank while the GPIO interface is open. `1` bits select output; `0` bits select input.

```
FT2232.GPIO dir [low|high] <MASK>
```

```
# Configure low bank bits 0–3 as outputs
FT2232.GPIO dir low 0x0F

# Configure high bank all as outputs
FT2232.GPIO dir high 0xFF

# ACBUS[3:0] outputs, [7:4] inputs
FT2232.GPIO dir high 0x0F
```

---

#### GPIO · write — Write a full byte to a bank

Writes an absolute 8-bit value to the specified bank.

```
FT2232.GPIO write [low|high] <VALUE>
```

```
FT2232.GPIO write low 0xAA
FT2232.GPIO write high 0x01
```

---

#### GPIO · set — Drive masked pins HIGH

```
FT2232.GPIO set [low|high] <MASK>
```

```
# Set ADBUS[0] high
FT2232.GPIO set low 0x01

# Set ACBUS[7] high
FT2232.GPIO set high 0x80
```

---

#### GPIO · clear — Drive masked pins LOW

```
FT2232.GPIO clear [low|high] <MASK>
```

```
FT2232.GPIO clear low 0x01
FT2232.GPIO clear high 0xF0
```

---

#### GPIO · toggle — Invert masked output pins

```
FT2232.GPIO toggle [low|high] <MASK>
```

```
FT2232.GPIO toggle low 0xFF
FT2232.GPIO toggle high 0x01
```

---

#### GPIO · read — Read current pin levels from a bank

Returns the current logical level of all 8 pins in the bank (regardless of direction), printed as a hex value and binary string (MSB first).

```
FT2232.GPIO read [low|high]
```

```
FT2232.GPIO read low
FT2232.GPIO read high
```

**Example output:**
```
FT2_GPIO   | Bank low: 0x5A  [01011010]
```

---

#### GPIO · help — List available sub-commands

```
FT2232.GPIO help
```

---

### UART

The UART module opens the FT2232 **channel B** in async serial (VCP) mode. This interface is available **on FT2232D only** — attempting to open UART with `variant=H` is rejected at runtime with a clear error message.

> On FT2232D, channel B cannot be used as MPSSE simultaneously with the UART. Channel A (MPSSE) and channel B (UART) may be used together.

---

#### UART · open — Open UART interface (FT2232D only)

```
FT2232.UART open [baud=N] [data=8] [stop=1] [parity=none|odd|even|mark|space] [flow=none|hw] [variant=D] [device=N]
```

| Argument | Description | Default |
|---|---|---|
| `baud` | Baud rate | `115200` |
| `data` | Data bits | `8` |
| `stop` | Stop bits | `1` |
| `parity` | `none`, `odd`, `even`, `mark`, or `space` | `none` |
| `flow` | `none` or `hw` (RTS/CTS hardware flow control) | `none` |
| `variant` | Must be `D`; `H` is rejected | INI `VARIANT` |
| `device` | Zero-based device index | `0` |

```
FT2232.UART open variant=D baud=115200
FT2232.UART open variant=D baud=9600 parity=even stop=1 flow=hw
FT2232.UART open variant=D baud=460800 device=1
```

---

#### UART · close — Release UART interface

```
FT2232.UART close
```

---

#### UART · cfg — Update UART parameters

Parameters can be updated while UART is open (applied immediately via `configure()`) or stored for the next `open`. The `variant` and `device` arguments are not accepted here — use `open` to change them.

```
FT2232.UART cfg [baud=N] [data=8] [stop=1] [parity=none|odd|even|mark|space] [flow=none|hw]
```

```
FT2232.UART cfg baud=9600 parity=even
FT2232.UART cfg baud=115200 flow=hw
```

---

#### UART · write — Transmit hex bytes over UART

```
FT2232.UART write <HEXDATA>
```

```
# Send 4 bytes
FT2232.UART write DEADBEEF

# Send ASCII "Hello"
FT2232.UART write 48656C6C6F
```

---

#### UART · read — Receive N bytes over UART

Blocks until N bytes are received or `READ_TIMEOUT` (from INI) expires. Received bytes are printed as a hex dump.

```
FT2232.UART read <N>
```

```
FT2232.UART read 4
FT2232.UART read 64
```

---

#### UART · script — Execute a command script

**UART must be open first.**

```
FT2232.UART script <filename>
```

```
FT2232.UART script uart_test.txt
FT2232.UART script modem_init.txt
```

---

#### UART · help — List available sub-commands

```
FT2232.UART help
```

---

## SPI Clock Reference

Named presets valid for both variants. The FT2232D runtime limit of 3 MHz is enforced by `checkVariantSpeedLimit()` before the driver opens — presets above 3 MHz are accepted by the table but will be rejected at open time when `variant=D`.

| Preset label | Frequency | FT2232H | FT2232D |
|---|---|---|---|
| `100kHz` | 100 kHz | ✓ | ✓ |
| `500kHz` | 500 kHz | ✓ | ✓ |
| `1MHz` | 1 MHz | ✓ | ✓ |
| `2MHz` | 2 MHz | ✓ | ✓ |
| `3MHz` | 3 MHz | ✓ | ✓ (maximum for D) |
| `5MHz` | 5 MHz | ✓ | ✗ (exceeds D limit) |
| `10MHz` | 10 MHz | ✓ | ✗ |
| `30MHz` | 30 MHz | ✓ (maximum for H) | ✗ |

Raw Hz values are also accepted.

---

## I2C Speed Reference

Named presets valid for both variants. The FT2232D practical cap of ~400 kHz is enforced at open time.

| Preset label | Frequency | FT2232H | FT2232D |
|---|---|---|---|
| `50kHz` | 50 kHz | ✓ | ✓ |
| `100kHz` | 100 kHz | ✓ | ✓ |
| `400kHz` | 400 kHz | ✓ | ✓ (practical maximum for D) |
| `1MHz` | 1 MHz | ✓ | ✗ (exceeds D limit) |

Raw Hz values are also accepted.

---

## UART Baud Rate Reference

Named baud rate presets (FT2232D only):

| Preset | Baud rate |
|---|---|
| `9600` | 9600 |
| `19200` | 19200 |
| `38400` | 38400 |
| `57600` | 57600 |
| `115200` | 115200 (power-on default) |
| `230400` | 230400 |
| `460800` | 460800 |
| `921600` | 921600 |

Raw integer values are also accepted.

---

## Script Files

Script files are plain text files located under `ARTEFACTS_PATH`. They are executed by the `CommScriptClient` engine, which reads each line and performs send/receive/expect operations. The `SCRIPT_DELAY` INI key inserts a per-command delay in milliseconds.

The corresponding interface must be open before calling `script`. The plugin passes the already-open driver handle directly to `CommScriptClient`, so no reconnection occurs inside the script.

```
# FT2232H — SPI on channel A, I2C on channel B simultaneously
FT2232.SPI open variant=H clock=10000000 channel=A
FT2232.SPI script flash_read_id.txt

FT2232.I2C open variant=H addr=0x50 clock=400000 channel=B
FT2232.I2C script eeprom_test.txt

# FT2232D — SPI on channel A, UART on channel B
FT2232.SPI open variant=D clock=1000000 channel=A
FT2232.UART open variant=D baud=115200
FT2232.UART script uart_test.txt
```

---

## Fault-Tolerant and Dry-Run Modes

- **Dry-run mode**: when `doEnable()` has not been called, every command validates its arguments (including variant/channel constraints and speed limits) and returns `true` without touching hardware. The generic dispatcher detects `isEnabled() == false` and returns early. This allows test framework validators to check command syntax and hardware limits before a live run.

- **Fault-tolerant mode** (`setFaultTolerant()` / `isFaultTolerant()`): when set, the plugin framework can be configured to continue execution past command failures. Useful in production test scripts where a non-critical probe failure should not abort a longer sequence.

- **Privileged mode** (`isPrivileged()`): always returns `false`; reserved for future framework use.

---

## Error Handling and Return Values

Every handler returns `bool`:
- `true` — success (or argument validation passed in disabled/dry-run mode).
- `false` — argument validation failed, unknown sub-command, variant/channel constraint violated, hardware speed limit exceeded, driver open failed, hardware operation returned an error status, or file not found.

All four module drivers return a typed `Status` enum (`SUCCESS`, and various error values). The plugin checks these and logs an error then returns `false` on any non-`SUCCESS` status.

Diagnostic messages are emitted via `LOG_PRINT` at several severity levels:

| Level | Usage |
|---|---|
| `LOG_ERROR` | Command failed, invalid argument, variant/speed constraint violation, hardware error |
| `LOG_WARNING` | Non-fatal issue (e.g., closing a port that was not open) |
| `LOG_INFO` | Successful operations (bytes written, device opened, clock updated, etc.) |
| `LOG_DEBUG` | Internal state changes |
| `LOG_VERBOSE` | INI parameter loading details |
| `LOG_FIXED` | Help text output and scan results |

Log verbosity is controlled by the host application via the shared `uLogger` configuration.
