# CH347 Plugin

A C++ shared-library plugin that exposes the [WCH CH347](https://www.wch-ic.com/products/productsCenter/mcuInterface?categoryId=1&tName=USB%20to%20JTAG/FIFO/SPI) Hi-Speed USB adapter through a unified command dispatcher. The CH347 is a single-chip USB 2.0 Hi-Speed bridge that simultaneously provides **SPI**, **I2C**, **GPIO**, and **JTAG** interfaces over one USB device file descriptor. Unlike serial-port-based adapters, the CH347 communicates directly through the WCH native USB driver library.

**Version:** 1.0.0.0  
**Requires:** C++20

---

## Table of Contents

1. [Overview](#overview)
2. [Project Structure](#project-structure)
3. [Architecture](#architecture)
   - [Plugin Lifecycle](#plugin-lifecycle)
   - [Command Dispatch Model](#command-dispatch-model)
   - [Generic Template Helpers](#generic-template-helpers)
   - [Pending Configuration Structs](#pending-configuration-structs)
   - [INI Configuration Keys](#ini-configuration-keys)
4. [Building](#building)
5. [Platform Notes](#platform-notes)
6. [Command Reference](#command-reference)
   - [INFO](#info)
   - [SPI](#spi)
   - [I2C](#i2c)
   - [GPIO](#gpio)
   - [JTAG](#jtag)
7. [SPI Clock Reference](#spi-clock-reference)
8. [I2C Speed Reference](#i2c-speed-reference)
9. [Script Files](#script-files)
10. [Fault-Tolerant and Dry-Run Modes](#fault-tolerant-and-dry-run-modes)
11. [Error Handling and Return Values](#error-handling-and-return-values)

---

## Overview

The plugin loads as a dynamic shared library (`.so` / `.dll`). The host application calls the exported C entry points `pluginEntry()` / `pluginExit()` to create and destroy the plugin instance. Once loaded, settings are pushed via `setParams()`, the plugin is initialized with `doInit()`, enabled with `doEnable()`, and commands are dispatched with `doDispatch()`.

All commands follow the pattern:

```
<PLUGIN>.<COMMAND> [subcommand] [arguments]
```

For example:

```
CH347.SPI open clock=15000000 mode=0
CH347.SPI wrrd 9F:3
CH347.I2C scan
CH347.GPIO set pins=0x01
CH347.JTAG write ir FF
```

A key difference from the BusPirate plugin is that each interface (SPI, I2C, GPIO, JTAG) must be explicitly **opened** and **closed** — there is no mode-switch step. All four interfaces can be open simultaneously.

---

## Project Structure

```
ch347_plugin/
├── CMakeLists.txt                  # Build definition (shared library, C++20)
├── inc/
│   ├── ch347_plugin.hpp            # Main class + command tables + pending config structs
│   ├── ch347_generic.hpp           # Generic template helpers + clock index helpers
│   └── private/
│       ├── spi_config.hpp          # SPI_COMMANDS_CONFIG_TABLE + SPI_SPEED_CONFIG_TABLE
│       ├── i2c_config.hpp          # I2C_COMMANDS_CONFIG_TABLE + I2C_SPEED_CONFIG_TABLE
│       ├── gpio_config.hpp         # GPIO_COMMANDS_CONFIG_TABLE
│       └── jtag_config.hpp         # JTAG_COMMANDS_CONFIG_TABLE + JTAG_RATE_CONFIG_TABLE
└── src/
    ├── ch347_plugin.cpp            # Entry points, init/cleanup, INFO, setParams, parse helpers
    ├── ch347_spi.cpp               # SPI sub-command implementations
    ├── ch347_i2c.cpp               # I2C sub-command implementations
    ├── ch347_gpio.cpp              # GPIO sub-command implementations
    └── ch347_jtag.cpp              # JTAG sub-command implementations
```

Each protocol lives in its own `.cpp` file. The command/speed tables are defined at the bottom of each config header using X-macros, mirroring the same pattern as the BusPirate plugin.

---

## Architecture

### Plugin Lifecycle

```
pluginEntry()               → creates CH347Plugin instance
  setParams()               → loads INI values (device path, speeds, address, timeouts…)
  doInit()                  → propagates INI defaults into pending config structs; marks initialized
  doEnable()                → enables real execution (without this, commands only validate args)
  doDispatch(cmd, args)     → routes to the correct top-level or module handler
  CH347.SPI open ...        → opens SPI driver (CH347SPI), validates hardware connection
  CH347.SPI close           → closes SPI driver
  (similar for I2C / GPIO / JTAG)
  doCleanup()               → closes all four drivers, resets state
pluginExit(ptr)             → deletes the CH347Plugin instance
```

`doEnable()` controls a **dry-run / argument-validation mode**: when not enabled, commands parse their arguments and return `true` without touching the hardware. This is used by test frameworks to verify command syntax before the device is connected.

`doInit()` does **not** open any hardware interface. Opening happens explicitly via the per-module `open` sub-command. This allows lazy initialization and supports opening only the interface(s) needed for a given test sequence.

### Command Dispatch Model

The dispatch model uses two layers of `std::map`:

1. **Top-level map** (`m_mapCmds`): maps command names (`INFO`, `SPI`, `I2C`, `GPIO`, `JTAG`) to member-function pointers on `CH347Plugin`.
2. **Module-level maps** (`m_mapCmds_SPI`, `m_mapCmds_I2C`, `m_mapCmds_GPIO`, `m_mapCmds_JTAG`): each module owns a map of sub-command name → handler pointer.

A meta-map (`m_mapCommandsMaps`) maps module names to their sub-maps, so the generic dispatcher can locate any sub-command dynamically without any switch statements.

Command registration is entirely driven by X-macros in the `*_config.hpp` headers:

```cpp
// In spi_config.hpp:
#define SPI_COMMANDS_CONFIG_TABLE  \
SPI_CMD_RECORD( open   )           \
SPI_CMD_RECORD( close  )           \
SPI_CMD_RECORD( cfg    )           \
...

// In the constructor (ch347_plugin.hpp):
#define SPI_CMD_RECORD(a) \
    m_mapCmds_SPI.insert({#a, &CH347Plugin::m_handle_spi_##a});
SPI_COMMANDS_CONFIG_TABLE
#undef SPI_CMD_RECORD
```

Adding a new sub-command requires only one line in the config table and one handler function.

### Generic Template Helpers

`ch347_generic.hpp` provides stateless template functions shared by all modules:

| Function | Purpose |
|---|---|
| `generic_module_dispatch<T>()` | Splits `"subcmd args"` and routes to the correct module handler |
| `generic_module_set_speed<T>()` | Looks up a speed label (or raw Hz value) and calls `setModuleSpeed()` |
| `generic_write_data<T>()` | Unhexlifies a hex string and calls a write callback (up to 4096 bytes) |
| `generic_write_read_data<T>()` | Parses `HEXDATA:rdlen` and calls a write-then-read callback |
| `generic_write_read_file<T>()` | Reads write data from a binary file in `ARTEFACTS_PATH`, streams results |
| `generic_execute_script<T>()` | Runs a `CommScriptClient` script on an open driver |
| `generic_module_list_commands<T>()` | Logs all registered sub-command names (used by `help`) |

Two inline helper functions manage the SPI clock index encoding:

```cpp
spiHzToClockIndex(uint32_t hz)  → uint8_t   // maps Hz to CH347 clock register index 0-7
spiClockIndexToHz(uint8_t idx)  → uint32_t  // reverse mapping for display
```

### Pending Configuration Structs

Each module maintains a "pending configuration" struct that is updated by `open` and `cfg` commands and applied to the live driver if it is already open:

| Struct | Fields | Default |
|---|---|---|
| `SpiPendingCfg` | `cfg.iClock`, `cfg.iMode`, `cfg.iByteOrder`, `xferOpts.chipSelect`, `cfgDirty` | clock=1MHz, mode=0, MSB, CS1 |
| `I2cPendingCfg` | `speed`, `address` | 400 kHz, 0x50 |
| `GpioPendingCfg` | `enableMask`, `dirMask`, `dataValue` | all inputs, all 0 |
| `JtagPendingCfg` | `clockRate`, `lastReg` | rate=2, DR |

The `lastReg` field in `JtagPendingCfg` remembers the last IR/DR register used so subsequent `write`, `read`, and `wrrd` commands can omit the `ir`/`dr` prefix.

### INI Configuration Keys

| Key | Type | Default | Description |
|---|---|---|---|
| `DEVICE_PATH` | string | `/dev/ch34xpis0` (Linux) / `0` (Windows) | USB device node or index |
| `ARTEFACTS_PATH` | string | `""` | Base directory for script and binary data files |
| `SPI_CLOCK` | uint32 (Hz) | `1000000` | Initial SPI clock frequency |
| `I2C_SPEED` | string/uint8 | `400kHz` / `fast` | Initial I2C speed preset |
| `I2C_ADDRESS` | uint8 (hex) | `0x50` | Default I2C target device address |
| `JTAG_CLOCK_RATE` | uint8 | `2` | Initial JTAG clock rate (0–5) |
| `READ_TIMEOUT` | uint32 (ms) | `5000` | Per-operation read timeout |
| `SCRIPT_DELAY` | uint32 (ms) | `0` | Inter-command delay during script execution |

---

## Building

```bash
mkdir build && cd build
cmake ..
make ch347_plugin
```

The output is `libch347_plugin.so` (Linux) or `ch347_plugin.dll` (Windows).

**Required libraries** (must be present in the CMake build tree):

- `uCH347` — WCH CH347 USB driver abstraction (CH347SPI, CH347I2C, CH347GPIO, CH347JTAG)
- `uPluginOps`, `uIPlugin`, `uSharedConfig` — plugin framework
- `uICoreScript`, `uCommScriptClient`, `uCommScriptCommandInterpreter`, `uScriptReader` — scripting engine
- `uICommDriver`, `uUtils` — communication driver base and utilities

---

## Platform Notes

**Linux:**
- The default device path is `/dev/ch34xpis0`. Additional CH347 devices appear as `/dev/ch34xpis1`, `/dev/ch34xpis2`, etc.
- The WCH Linux kernel driver or userspace library must be installed.
- Override per-command with `device=/dev/ch34xpis1` in the `open` sub-command.

**Windows:**
- The default device path is `"0"` — a decimal index passed to `CH347OpenDevice()`.
- Multiple devices use `"1"`, `"2"`, etc.
- The WCH Windows DLL (`CH347DLL.DLL`) must be in the system path.
- Override with `device=1` in the `open` sub-command.

---

## Command Reference

### INFO

Prints version, device path, and a complete command reference. Takes **no arguments** and works even before `doInit()`.

```
CH347.INFO
```

**Example output (abbreviated):**
```
CH347      | Vers: 1.0.0.0
CH347      | Description: WCH CH347 Hi-Speed USB adapter (SPI/I2C/GPIO/JTAG)
CH347      |   Device: /dev/ch34xpis0
...
```

---

### SPI

Full-duplex SPI bus master. Each module must be explicitly opened before use.

```
CH347.SPI <subcommand> [arguments]
```

---

#### SPI · open — Open interface and apply configuration

Opens the CH347 SPI interface. All optional parameters follow the `key=value` format and may be combined freely. The driver is closed and re-opened if called again while already open.

```
CH347.SPI open [clock=N] [mode=0-3] [order=msb|lsb] [cs=cs1|cs2|none] [device=PATH]
```

| Parameter | Values | Default | Description |
|---|---|---|---|
| `clock` | 468750 – 60000000 (Hz) | from INI / 1 MHz | SPI clock frequency in Hz |
| `mode` | 0, 1, 2, 3 | `0` | SPI mode (CPOL/CPHA combination) |
| `order` | `msb`, `lsb` | `msb` | Bit order |
| `cs` | `cs1`, `cs2`, `none` | `cs1` | Chip-select pin selection |
| `device` | path string | from INI | Override device path for this session |

SPI mode definitions:

| Mode | CPOL | CPHA | Clock idle | Data sampled |
|---|---|---|---|---|
| 0 | 0 | 0 | Low | Rising edge |
| 1 | 0 | 1 | Low | Falling edge |
| 2 | 1 | 0 | High | Falling edge |
| 3 | 1 | 1 | High | Rising edge |

```
# Open at 15 MHz, mode 0, MSB-first, CS1 (typical flash memory)
CH347.SPI open clock=15000000 mode=0

# Open at 1 MHz, SPI mode 3, LSB-first, using CS2
CH347.SPI open clock=1000000 mode=3 order=lsb cs=cs2

# Open on an alternate device
CH347.SPI open device=/dev/ch34xpis1 clock=8000000

# No CS management (CS driven externally or via GPIO)
CH347.SPI open clock=4000000 cs=none

# Windows: second CH347 device
CH347.SPI open device=1 clock=15000000
```

---

#### SPI · close — Release interface

```
CH347.SPI close
```

---

#### SPI · cfg — Update configuration without reopening

Updates pending SPI parameters and applies them to the open driver (if currently open). Use `?` to print the current configuration.

```
CH347.SPI cfg [clock=N] [mode=0-3] [order=msb|lsb] [cs=cs1|cs2|none]
CH347.SPI cfg ?
```

```
# Change clock to 7.5 MHz, switch to mode 1
CH347.SPI cfg clock=7500000 mode=1

# Switch to MSB-first
CH347.SPI cfg order=msb

# Print current pending configuration
CH347.SPI cfg ?

# Change CS to CS2
CH347.SPI cfg cs=cs2
```

**Clock preset labels** (can be used as values for `clock=`):

| Label | Frequency |
|---|---|
| `468kHz` | 468,750 Hz |
| `937kHz` | 937,500 Hz |
| `1.875MHz` | 1,875,000 Hz |
| `3.75MHz` | 3,750,000 Hz |
| `7.5MHz` | 7,500,000 Hz |
| `15MHz` | 15,000,000 Hz |
| `30MHz` | 30,000,000 Hz |
| `60MHz` | 60,000,000 Hz |

---

#### SPI · cs — Manual chip-select control

CS is also automatically asserted/deasserted per transfer. Use this for manual control of the CS line outside of a transfer.

```
CH347.SPI cs <en|dis>
```

| Argument | Effect |
|---|---|
| `en` or `1` | Assert CS (drive low) |
| `dis` or `0` | Deassert CS (drive high / HiZ) |

```
CH347.SPI cs en
CH347.SPI cs dis
```

---

#### SPI · write — Transmit bytes (MOSI only)

Sends bytes to MOSI. MISO data is received but discarded.

```
CH347.SPI write <HEXDATA>
```

```
# Send a Write Enable command (0x06) to a flash
CH347.SPI write 06

# Send a 4-byte command
CH347.SPI write 03000000

# Send 8 bytes of data
CH347.SPI write 0102030405060708
```

---

#### SPI · read — Receive bytes

Performs a full-duplex transfer, clocking `0x00` on MOSI and capturing MISO. The received bytes are printed as a hex dump.

```
CH347.SPI read <N>
```

```
# Read 4 bytes
CH347.SPI read 4

# Read 256 bytes (e.g., one page from flash)
CH347.SPI read 256

# Read a single status byte
CH347.SPI read 1
```

---

#### SPI · xfer — Full-duplex transfer

Sends MOSI data while simultaneously capturing MISO. Both sides are the same length. The MISO bytes are printed as a hex dump.

```
CH347.SPI xfer <HEXDATA>
```

```
# Send 4 bytes, capture 4 bytes of MISO simultaneously
CH347.SPI xfer DEADBEEF

# Send command + dummy bytes, capture response
CH347.SPI xfer 9F000000

# Single-byte full-duplex exchange
CH347.SPI xfer FF
```

---

#### SPI · wrrd — Write then read in one transaction

Writes bytes, then reads back a specified number of bytes in one CS-asserted transaction. Internally this uses `tout_xfer` with a combined buffer (write bytes + zero padding for the read phase).

```
CH347.SPI wrrd <HEXDATA>:<rdlen>
```

- `HEXDATA` — hex string for the write phase (may be empty for read-only: `:N`)
- `rdlen` — number of bytes to read back

```
# JEDEC ID: send 0x9F, read 3 bytes (manufacturer, type, capacity)
CH347.SPI wrrd 9F:3

# Status register read (0x05), read 1 byte
CH347.SPI wrrd 05:1

# Read 256 bytes from address 0x000000 (READ command 0x03)
CH347.SPI wrrd 03000000:256

# Write Enable (0x06) — write only, no read
CH347.SPI wrrd 06:0

# Read-only: clock 4 dummy bytes, capture MISO
CH347.SPI wrrd :4

# Write 4 bytes, read back 4 bytes
CH347.SPI wrrd DEADBEEF:4
```

---

#### SPI · wrrdf — Write/read using binary files

Same as `wrrd` but write data is loaded from a binary file in `ARTEFACTS_PATH`. Read data is printed as it arrives. Optional chunk size parameters control how the file is split.

```
CH347.SPI wrrdf <filename>[:<wrchunk>][:<rdchunk>]
```

```
CH347.SPI wrrdf flash_program.bin
CH347.SPI wrrdf page_write.bin:256:0
CH347.SPI wrrdf firmware.bin:512:512
```

---

#### SPI · script — Execute a command script

Runs a `CommScriptClient` script from `ARTEFACTS_PATH`. **SPI must be open before calling this.**

```
CH347.SPI script <filename>
```

```
CH347.SPI script read_flash.txt
CH347.SPI script erase_chip.txt
CH347.SPI script program_sector.txt
```

---

### I2C

I2C bus master. **Must call `open` before any transfer.**

```
CH347.I2C <subcommand> [arguments]
```

---

#### I2C · open — Open interface and apply configuration

```
CH347.I2C open [speed=PRESET] [addr=0xNN] [device=PATH]
```

| Parameter | Values | Default | Description |
|---|---|---|---|
| `speed` | see table below | from INI / `fast` (400 kHz) | I2C clock speed |
| `addr` | `0x00`–`0x7F` | from INI / `0x50` | Default target device address for `read` and `wrrd` |
| `device` | path string | from INI | Override device path |

Speed presets accept both frequency labels and word aliases:

| Label | Alias | Frequency |
|---|---|---|
| `20kHz` | `low` | 20 kHz |
| `50kHz` | `std50` | 50 kHz |
| `100kHz` | `standard` | 100 kHz |
| `200kHz` | `std200` | 200 kHz |
| `400kHz` | `fast` | 400 kHz (default) |
| `750kHz` | `high` | 750 kHz |
| `1MHz` | `fast1m` | 1 MHz |

Raw enum integers 0–6 are also accepted.

```
# Open at 400 kHz targeting device at 0x50
CH347.I2C open speed=400kHz addr=0x50

# Open at 100 kHz using the "standard" alias
CH347.I2C open speed=standard addr=0x68

# Open at 1 MHz on an alternate device
CH347.I2C open speed=1MHz device=/dev/ch34xpis1

# Open at low speed (20 kHz) for long cables
CH347.I2C open speed=low
```

---

#### I2C · close — Release interface

```
CH347.I2C close
```

---

#### I2C · cfg — Update configuration without reopening

```
CH347.I2C cfg [speed=PRESET] [addr=0xNN]
CH347.I2C cfg ?
```

```
# Change target address to TMP102 temperature sensor
CH347.I2C cfg addr=0x48

# Switch to fast mode
CH347.I2C cfg speed=400kHz

# Change both at once
CH347.I2C cfg speed=100kHz addr=0x68

# Print current config
CH347.I2C cfg ?
```

---

#### I2C · write — Write bytes to the bus

Sends a complete `START + address+W + data + STOP` sequence. The first byte of the data buffer is the device address with the write bit set (`devAddr << 1`), followed by register address and payload.

```
CH347.I2C write <HEXDATA>
```

```
# Write to device 0x50 (addr byte = 0xA0), register 0x00, data 0xFF
CH347.I2C write A000FF

# Write to device 0x68 (addr = 0xD0), register 0x07, value 0x10
CH347.I2C write D00710

# Write-only to configure a register on device 0x48
CH347.I2C write 9000
```

---

#### I2C · read — Read N bytes

Reads N bytes from the device address configured via `open` or `cfg`. Performs `START + addr+R + N bytes (ACK each except last NACK) + STOP`.

```
CH347.I2C read <N>
```

```
# Read 2 bytes (e.g., a 16-bit sensor register)
CH347.I2C read 2

# Read a single byte
CH347.I2C read 1

# Read 8 bytes
CH347.I2C read 8
```

---

#### I2C · wrrd — Write then read

Performs a write phase, then a read phase. The read uses the device address from `open`/`cfg`. The write bytes are sent first; the read phase immediately follows. Read bytes are printed as a hex dump.

```
CH347.I2C wrrd <HEXDATA>:<rdlen>
```

```
# Write 0x50 (device addr+W) + reg 0x00, read 2 bytes
CH347.I2C wrrd A000:2

# TMP102 temperature read: write device addr+W + reg 0x00, read 2 bytes
CH347.I2C wrrd 9000:2

# Read-only: skip the write phase
CH347.I2C wrrd :4

# Write 3 bytes of config, read 1 byte status
CH347.I2C wrrd D007103A:1
```

---

#### I2C · wrrdf — Write/read using binary files

```
CH347.I2C wrrdf <filename>[:<wrchunk>][:<rdchunk>]
```

```
CH347.I2C wrrdf i2c_init_sequence.bin
CH347.I2C wrrdf eeprom_write.bin:16:0
```

---

#### I2C · scan — Probe bus for devices

Probes every address in the standard 7-bit range `0x08`–`0x77` by sending a zero-length write and checking for ACK. A temporary driver is opened automatically if the I2C interface is not already open. Prints the address of every responding device.

```
CH347.I2C scan
```

```
CH347.I2C open speed=100kHz
CH347.I2C scan
# Output example:
# CH347_I2C  | Found device at 0x48
# CH347_I2C  | Found device at 0x50
# CH347_I2C  | Found device at 0x68
```

---

#### I2C · eeprom — Read or write a 24Cxx series EEPROM

High-level EEPROM access using the CH347I2C library's built-in `read_eeprom()` / `write_eeprom()` calls. Supports the full 24Cxx family.

```
CH347.I2C eeprom read  <TYPE> <ADDR> <N>
CH347.I2C eeprom write <TYPE> <ADDR> <HEXDATA>
```

| TYPE index | EEPROM |
|---|---|
| 0 | 24C01 (128 bytes) |
| 1 | 24C02 (256 bytes) |
| 2 | 24C04 (512 bytes) |
| 3 | 24C08 (1 KB) |
| 4 | 24C16 (2 KB) |
| 5 | 24C32 (4 KB) |
| 6 | 24C64 (8 KB) |
| 7 | 24C128 (16 KB) |
| 8 | 24C256 (32 KB) |

```
# Read 16 bytes from a 24C04 (type 2) starting at address 0
CH347.I2C eeprom read 2 0 16

# Read the first 256 bytes of a 24C64 (type 6)
CH347.I2C eeprom read 6 0 256

# Write 4 bytes to a 24C02 (type 1) at address 0
CH347.I2C eeprom write 1 0 DEADBEEF

# Write a calibration constant to a 24C256 at offset 0x0100
CH347.I2C eeprom write 8 256 A5B6C7D8

# Write a string "HELLO" (ASCII hex) to 24C32 at address 0
CH347.I2C eeprom write 5 0 48454C4C4F
```

---

#### I2C · script — Execute a command script

**I2C must be open first.**

```
CH347.I2C script <filename>
```

```
CH347.I2C script sensor_init.txt
CH347.I2C script eeprom_test.txt
```

---

### GPIO

8-pin GPIO interface (GPIO0–GPIO7). All commands use **bitmasks** where bit N corresponds to pin GPION. **Must call `open` before use.**

```
CH347.GPIO <subcommand> [arguments]
```

---

#### GPIO · open — Open interface

Opens the GPIO interface. All 8 pins default to inputs.

```
CH347.GPIO open [device=PATH]
```

```
CH347.GPIO open
CH347.GPIO open device=/dev/ch34xpis1
```

---

#### GPIO · close — Release interface

```
CH347.GPIO close
```

---

#### GPIO · dir — Set pin directions

Sets which pins are outputs vs inputs. Bit N = 1 means pin GPION is an output.

```
CH347.GPIO dir output=0xNN
```

Also accepts a bare hex value.

```
# Set GPIO0–GPIO3 as outputs, GPIO4–GPIO7 as inputs
CH347.GPIO dir output=0x0F

# All 8 pins as outputs
CH347.GPIO dir output=0xFF

# All 8 pins as inputs (default state after open)
CH347.GPIO dir output=0x00

# GPIO0 and GPIO7 as outputs
CH347.GPIO dir output=0x81

# Bare form
CH347.GPIO dir 0x0F
```

---

#### GPIO · write — Drive output pins to specified levels

Sets the levels of a masked set of pins using `pins=` (which pins to affect) and `levels=` (their desired state). The internal cached value is updated for subsequent `toggle` operations.

```
CH347.GPIO write pins=0xNN levels=0xNN
```

Also accepts a bare hex value to set all pins at once.

```
# Set GPIO0 and GPIO2 HIGH, GPIO1 and GPIO3 LOW (pins 0-3 affected)
CH347.GPIO write pins=0x0F levels=0x05

# Set all output pins to 0xAA (alternating high/low)
CH347.GPIO write pins=0xFF levels=0xAA

# Drive all pins LOW
CH347.GPIO write pins=0xFF levels=0x00

# Bare form: set all pins to 0x55
CH347.GPIO write 0x55
```

---

#### GPIO · set — Drive masked pins HIGH

Drives the specified pins to logic high. Other pins are unchanged.

```
CH347.GPIO set pins=0xNN
```

Also accepts a bare hex value.

```
# Set GPIO0 high
CH347.GPIO set pins=0x01

# Set GPIO0–GPIO3 high
CH347.GPIO set pins=0x0F

# Set all pins high
CH347.GPIO set 0xFF

# Set GPIO7 high (e.g., LED on)
CH347.GPIO set pins=0x80
```

---

#### GPIO · clear — Drive masked pins LOW

Drives the specified pins to logic low. Other pins are unchanged.

```
CH347.GPIO clear pins=0xNN
```

Also accepts a bare hex value.

```
# Clear GPIO0 low
CH347.GPIO clear pins=0x01

# Clear GPIO4–GPIO7 low
CH347.GPIO clear pins=0xF0

# Clear all pins
CH347.GPIO clear 0xFF

# Clear GPIO7 low (e.g., LED off)
CH347.GPIO clear pins=0x80
```

---

#### GPIO · toggle — Invert masked output pins

Inverts the state of the specified pins using a read-modify-write on the internally cached data value. No hardware read is performed.

```
CH347.GPIO toggle pins=0xNN
```

Also accepts a bare hex value.

```
# Toggle GPIO0
CH347.GPIO toggle pins=0x01

# Toggle all 8 output pins
CH347.GPIO toggle 0xFF

# Toggle GPIO0 and GPIO1 (blink two LEDs)
CH347.GPIO toggle pins=0x03

# Toggle GPIO7
CH347.GPIO toggle pins=0x80
```

---

#### GPIO · read — Snapshot all GPIO pin states

Reads the current direction and data values from the hardware and prints them as hex plus a binary representation.

```
CH347.GPIO read
```

**Output format:**
```
CH347_GPIO | GPIO state:  dir=0x0F  data=0x05  [00000101]
```

- `dir` — direction bitmask (1 = output)
- `data` — current pin levels (1 = high), both inputs and outputs
- `[BBBBBBBB]` — binary representation, bit 7 on the left (GPIO7), bit 0 on the right (GPIO0)

```
CH347.GPIO open
CH347.GPIO dir output=0x0F
CH347.GPIO set pins=0x05
CH347.GPIO read
# → dir=0x0F  data=0x05  [00000101]
```

---

### JTAG

JTAG TAP interface. **Must call `open` before any operation.**

```
CH347.JTAG <subcommand> [arguments]
```

---

#### JTAG · open — Open interface

```
CH347.JTAG open [rate=0-5] [device=PATH]
```

| Parameter | Values | Default | Description |
|---|---|---|---|
| `rate` | 0–5 | from INI / `2` | Clock rate: 0 = slowest, 5 = fastest |
| `device` | path string | from INI | Override device path |

```
# Open at default rate (2)
CH347.JTAG open

# Open at high speed
CH347.JTAG open rate=5

# Open at slow speed for long cables or slow devices
CH347.JTAG open rate=0

# Open on alternate device at rate 4
CH347.JTAG open rate=4 device=/dev/ch34xpis1
```

---

#### JTAG · close — Release interface

```
CH347.JTAG close
```

---

#### JTAG · cfg — Update clock rate without reopening

```
CH347.JTAG cfg rate=0-5
CH347.JTAG cfg ?
```

```
CH347.JTAG cfg rate=3
CH347.JTAG cfg ?
```

---

#### JTAG · reset — TAP logic reset

Resets the JTAG TAP state machine. Either via a TMS sequence (5× TMS=1) or via the physical TRST pin.

```
CH347.JTAG reset
CH347.JTAG reset trst
```

| Argument | Mechanism |
|---|---|
| *(none)* | TAP reset via TMS sequence |
| `trst` | Assert TRST pin |

```
# Standard TAP reset (always safe to call before any JTAG sequence)
CH347.JTAG reset

# Assert TRST pin if connected
CH347.JTAG reset trst
```

---

#### JTAG · write — Shift bytes into IR or DR

Shifts data into the Instruction Register or Data Register. The IR/DR selection is optional and is remembered between calls (`lastReg` defaults to `DR`).

```
CH347.JTAG write [ir|dr] <HEXDATA>
```

```
# Load 0xFF into the Instruction Register
CH347.JTAG write ir FF

# Shift 4 bytes into the Data Register
CH347.JTAG write dr DEADBEEF

# Shift into DR again (omit register, uses last-used: DR)
CH347.JTAG write CAFEBABE

# Load a BYPASS instruction (all ones) into IR
CH347.JTAG write ir FFFFFFFF

# Load IDCODE instruction (0x01) into a 4-bit IR
CH347.JTAG write ir 01
```

---

#### JTAG · read — Shift N bytes out of IR or DR

Shifts N bytes out of the selected register while shifting in zeros. The received bytes are printed as a hex dump.

```
CH347.JTAG read [ir|dr] <N>
```

```
# Read 4 bytes from the Data Register (e.g., IDCODE)
CH347.JTAG read dr 4

# Read 1 byte from the Instruction Register
CH347.JTAG read ir 1

# Read using last-used register (defaults to DR)
CH347.JTAG read 4
```

---

#### JTAG · wrrd — Shift-in then shift-out in one operation

Writes to the selected register and simultaneously (or sequentially) reads back data.

```
CH347.JTAG wrrd [ir|dr] <HEXDATA>:<rdlen>
```

```
# Write to DR and read back 4 bytes
CH347.JTAG wrrd dr DEADBEEF:4

# Load IDCODE instruction into IR and read 1 byte back
CH347.JTAG wrrd ir FF:1

# Write 2 bytes to DR, read back 2 bytes
CH347.JTAG wrrd dr A5B6:2

# Without register specifier (uses last-used)
CH347.JTAG wrrd DEADBEEF:4
```

---

#### JTAG · script — Execute a command script

**JTAG must be open first.**

```
CH347.JTAG script <filename>
```

```
CH347.JTAG script jtag_identify.txt
CH347.JTAG script boundary_scan.txt
CH347.JTAG script jtag_prog.txt
```

---

## SPI Clock Reference

The CH347 SPI clock is set by a 3-bit index value (0 = fastest):

| Index | Frequency | Preset label |
|---|---|---|
| 0 | 60.000 MHz | `60MHz` |
| 1 | 30.000 MHz | `30MHz` |
| 2 | 15.000 MHz | `15MHz` |
| 3 | 7.500 MHz | `7.5MHz` |
| 4 | 3.750 MHz | `3.75MHz` |
| 5 | 1.875 MHz | `1.875MHz` |
| 6 | 937.500 kHz | `937kHz` |
| 7 | 468.750 kHz | `468kHz` (minimum) |

The `spiHzToClockIndex()` helper selects the fastest clock that does not exceed the requested frequency. For example, requesting `clock=10000000` (10 MHz) selects index 2 (15 MHz would be too fast, so 7.5 MHz is actually chosen — the mapping rounds down to the next available preset).

---

## I2C Speed Reference

| Enum value | Label | Alias | Frequency |
|---|---|---|---|
| 0 | `20kHz` | `low` | 20 kHz |
| 1 | `50kHz` | `std50` | 50 kHz |
| 2 | `100kHz` | `standard` | 100 kHz |
| 3 | `200kHz` | `std200` | 200 kHz |
| 4 | `400kHz` | `fast` | 400 kHz (power-on default) |
| 5 | `750kHz` | `high` | 750 kHz |
| 6 | `1MHz` | `fast1m` | 1 MHz |

Raw enum integers 0–6 are also accepted.

---

## Script Files

Script files are plain text files located under `ARTEFACTS_PATH`. They are executed by the `CommScriptClient` engine, which reads each line and performs send/receive/expect operations. The `SCRIPT_DELAY` INI key inserts a per-command delay in milliseconds.

**Important:** The corresponding interface must be open before calling `script`. Unlike the BusPirate plugin which re-opens the UART per script execution, this plugin passes the already-open driver handle directly to `CommScriptClient`.

```
CH347.SPI open clock=15000000
CH347.SPI script flash_read_id.txt

CH347.I2C open speed=400kHz addr=0x50
CH347.I2C script eeprom_dump.txt

CH347.JTAG open rate=2
CH347.JTAG script boundary_scan.txt
```

---

## Fault-Tolerant and Dry-Run Modes

- **Dry-run mode**: when `doEnable()` has not been called, every command validates its arguments and returns `true` without touching hardware. The generic dispatcher detects `isEnabled() == false` and returns early. This is used by test framework validators to check syntax before a live run.

- **Fault-tolerant mode** (`setFaultTolerant()` / `isFaultTolerant()`): when set, the plugin framework can be configured to continue execution past command failures. Useful in production test scripts where a non-critical probe failure should not abort a longer sequence.

- **Privileged mode** (`isPrivileged()`): always returns `false`; reserved for future framework use.

---

## Error Handling and Return Values

Every handler returns `bool`:
- `true` — success (or argument validation passed in disabled mode).
- `false` — argument validation failed, unknown sub-command, driver open failed, hardware operation returned an error status, or file not found.

All four module drivers return a typed `Status` enum (`SUCCESS`, and various error values). The plugin checks these and logs an error then returns `false` on any non-`SUCCESS` status.

Diagnostic messages are emitted via `LOG_PRINT` at several severity levels:

| Level | Usage |
|---|---|
| `LOG_ERROR` | Command failed, invalid argument, hardware error |
| `LOG_WARNING` | Non-fatal issue (e.g., closing a port that was not open) |
| `LOG_INFO` | Successful operations (bytes written, device opened, etc.) |
| `LOG_DEBUG` | Internal state changes |
| `LOG_VERBOSE` | INI parameter loading details |
| `LOG_FIXED` | Help text output |

Log verbosity is controlled by the host application via the shared `uLogger` configuration.
