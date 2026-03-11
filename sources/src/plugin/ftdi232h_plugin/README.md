# FT232H Plugin

A C++ shared-library plugin that exposes the [FTDI FT232H](https://ftdichip.com/products/ft232h/) Hi-Speed USB adapter through a unified command dispatcher. The FT232H is a single-chip USB 2.0 Hi-Speed bridge providing **SPI**, **I2C**, **GPIO**, and **UART** interfaces over one USB device. It communicates via the FTDI D2XX library (`FTD2XX`) using the MPSSE (Multi-Protocol Synchronous Serial Engine) engine for SPI, I2C, and GPIO, and the virtual COM port (VCP) mode for UART.

**Version:** 1.0.0.0  
**Requires:** C++20

> **Important hardware constraint:** The FT232H has a **single MPSSE channel** (unlike FT2232H or FT4232H). As a result, SPI, I2C, and GPIO share one physical interface and cannot be open simultaneously on the same chip. UART mode is mutually exclusive with all MPSSE modes on the same device. Use `device=N` to address a separate FT232H chip when multiple protocols are needed concurrently.

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
   - [UART](#uart)
7. [SPI Clock Reference](#spi-clock-reference)
8. [I2C Speed Reference](#i2c-speed-reference)
9. [UART Baud Rate Reference](#uart-baud-rate-reference)
10. [Script Files](#script-files)
11. [Fault-Tolerant and Dry-Run Modes](#fault-tolerant-and-dry-run-modes)
12. [Error Handling and Return Values](#error-handling-and-return-values)

---

## Overview

The plugin loads as a dynamic shared library (`.so` / `.dll`). The host application calls the exported C entry points `pluginEntry()` / `pluginExit()` to create and destroy the plugin instance. Once loaded, settings are pushed via `setParams()`, the plugin is initialized with `doInit()`, enabled with `doEnable()`, and commands are dispatched with `doDispatch()`.

All commands follow the pattern:

```
<PLUGIN>.<COMMAND> [subcommand] [arguments]
```

For example:

```
FT232H.SPI open clock=10000000 mode=0
FT232H.SPI wrrd 9F:3
FT232H.I2C scan
FT232H.GPIO set low 0x01
FT232H.UART open baud=115200
```

Each interface (SPI, I2C, GPIO, UART) must be explicitly **opened** and **closed**. Because of the FT232H's single MPSSE channel constraint, only one MPSSE-based module (SPI, I2C, or GPIO) should be open at a time on a single chip. UART is operated in VCP mode and is entirely separate from MPSSE, but it also requires exclusive use of the chip.

---

## Project Structure

```
ftdi232h_plugin/
├── CMakeLists.txt                  # Build definition (shared library, C++20)
├── inc/
│   ├── ft232h_plugin.hpp           # Main class + command tables + pending config structs
│   ├── ft232h_generic.hpp          # Generic template helpers + write/read/script helpers
│   └── private/
│       ├── spi_config.hpp          # SPI_COMMANDS_CONFIG_TABLE + SPI_SPEED_CONFIG_TABLE
│       ├── i2c_config.hpp          # I2C_COMMANDS_CONFIG_TABLE + I2C_SPEED_CONFIG_TABLE
│       ├── gpio_config.hpp         # GPIO_COMMANDS_CONFIG_TABLE
│       └── uart_config.hpp         # UART_COMMANDS_CONFIG_TABLE + UART_SPEED_CONFIG_TABLE
└── src/
    ├── ft232h_plugin.cpp           # Entry points, init/cleanup, INFO, setParams, parse helpers
    ├── ft232h_spi.cpp              # SPI sub-command implementations
    ├── ft232h_i2c.cpp              # I2C sub-command implementations
    ├── ft232h_gpio.cpp             # GPIO sub-command implementations
    └── ft232h_uart.cpp             # UART sub-command implementations
```

Each protocol lives in its own `.cpp` file. The command and speed tables are defined at the bottom of each config header using X-macros, mirroring the same pattern used by sibling FTDI plugins in this project.

---

## Architecture

### Plugin Lifecycle

```
pluginEntry()               → creates FT232HPlugin instance
  setParams()               → loads INI values (device index, speeds, address, timeouts…)
  doInit()                  → propagates INI defaults into pending config structs; marks initialized
  doEnable()                → enables real execution (without this, commands only validate args)
  doDispatch(cmd, args)     → routes to the correct top-level or module handler
  FT232H.SPI open ...       → opens SPI driver (FT232HSPI), validates hardware connection
  FT232H.SPI close          → closes SPI driver
  (similar for I2C / GPIO / UART)
  doCleanup()               → closes all four drivers, resets state
pluginExit(ptr)             → deletes the FT232HPlugin instance
```

`doEnable()` controls a **dry-run / argument-validation mode**: when not enabled, commands parse their arguments and return `true` without touching the hardware. This is used by test frameworks to verify command syntax before the device is connected.

`doInit()` does **not** open any hardware interface. Opening happens explicitly via the per-module `open` sub-command. This allows lazy initialization and supports opening only the interface needed for a given test sequence.

### Command Dispatch Model

The dispatch model uses two layers of `std::map`:

1. **Top-level map** (`m_mapCmds`): maps command names (`INFO`, `SPI`, `I2C`, `GPIO`, `UART`) to member-function pointers on `FT232HPlugin`.
2. **Module-level maps** (`m_mapCmds_SPI`, `m_mapCmds_I2C`, `m_mapCmds_GPIO`, `m_mapCmds_UART`): each module owns a map of sub-command name → handler pointer.

A meta-map (`m_mapCommandsMaps`) maps module names to their sub-maps, so the generic dispatcher can locate any sub-command dynamically without any switch statements.

Command registration is entirely driven by X-macros in the `*_config.hpp` headers:

```cpp
// In spi_config.hpp:
#define SPI_COMMANDS_CONFIG_TABLE  \
SPI_CMD_RECORD( open   )           \
SPI_CMD_RECORD( close  )           \
SPI_CMD_RECORD( cfg    )           \
...

// In the constructor (ft232h_plugin.hpp):
#define SPI_CMD_RECORD(a) \
    m_mapCmds_SPI.insert({#a, &FT232HPlugin::m_handle_spi_##a});
SPI_COMMANDS_CONFIG_TABLE
#undef SPI_CMD_RECORD
```

Adding a new sub-command requires only one line in the config table and one handler function.

### Generic Template Helpers

`ft232h_generic.hpp` provides stateless template functions shared by all modules:

| Function | Purpose |
|---|---|
| `generic_module_dispatch<T>()` | Splits `"subcmd args"` and routes to the correct module handler |
| `generic_module_set_speed<T>()` | Looks up a speed label (or raw Hz value) and calls `setModuleSpeed()` |
| `generic_write_data<T>()` | Unhexlifies a hex string and calls a write callback (up to 65536 bytes) |
| `generic_write_read_data<T>()` | Parses `HEXDATA:rdlen` and calls a write-then-read callback |
| `generic_write_read_file<T>()` | Reads write data from a binary file in `ARTEFACTS_PATH`, streams results in chunks |
| `generic_execute_script<T>()` | Runs a `CommScriptClient` script on an open driver |
| `generic_module_list_commands<T>()` | Logs all registered sub-command names (used by `help`) |

Two limits are defined for bulk data operations:

```cpp
FT_WRITE_MAX_CHUNK_SIZE  = 4096     // Default chunk size for file-based transfers
FT_BULK_MAX_BYTES        = 65536    // MPSSE max per-transfer limit
```

### Pending Configuration Structs

Each module maintains a "pending configuration" struct that is updated by `open` and `cfg` commands and applied to the live driver if it is already open:

| Struct | Fields | Default |
|---|---|---|
| `SpiPendingCfg` | `clockHz`, `mode`, `bitOrder`, `csPin`, `csPolarity` | clock=1 MHz, mode=0, MSB, csPin=0x08, CS active-low |
| `I2cPendingCfg` | `address`, `clockHz` | addr=0x50, clock=100 kHz |
| `GpioPendingCfg` | `lowDirMask`, `lowValue`, `highDirMask`, `highValue` | all inputs, all 0 |
| `UartPendingCfg` | `baudRate`, `dataBits`, `stopBits`, `parity`, `hwFlowCtrl` | 115200, 8, 0, none, no flow |

Unlike the CH347 plugin, the FT232H's SPI chip-select is managed **automatically per-transfer** by the driver — there is no separate `cs` assertion command, only an informational `cs` sub-command that explains this behaviour.

### INI Configuration Keys

| Key | Type | Default | Description |
|---|---|---|---|
| `DEVICE_INDEX` | uint8 | `0` | Zero-based FTDI device index passed to D2XX |
| `ARTEFACTS_PATH` | string | `""` | Base directory for script and binary data files |
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
make ftdi232h_plugin
```

The output is `libftdi232h_plugin.so` (Linux) or `ftdi232h_plugin.dll` (Windows).

**Required libraries** (must be present in the CMake build tree):

- `ft232h` — FTDI FT232H driver abstraction (FT232HSPI, FT232HI2C, FT232HGPIO, FT232HUART)
- `ftdi::sdk` — FTDI D2XX SDK (`FTD2XX.dll` / `libftd2xx.so`); on Windows, set `FTD2XX_ROOT` to the SDK root containing `include/ftd2xx.h` and `amd64/` or `i386/` subdirectories
- `uPluginOps`, `uIPlugin`, `uSharedConfig` — plugin framework
- `uICoreScript`, `uCommScriptClient`, `uCommScriptCommandInterpreter`, `uScriptReader` — scripting engine
- `uICommDriver`, `uUtils` — communication driver base and utilities

On Windows, the build also links against `setupapi`, `user32`, and `advapi32`, and copies `FTD2XX64.dll` into the output directory automatically via a post-build step.

---

## Platform Notes

**Linux:**
- Install the FTDI D2XX userspace library from [ftdichip.com](https://ftdichip.com/drivers/d2xx-drivers/) or use the `libftdi1` compatibility layer.
- The device is addressed by its zero-based enumeration index (e.g., `0` for the first FT232H on the bus).
- You may need to unbind the `ftdi_sio` kernel driver: `sudo rmmod ftdi_sio` or add a udev rule to prevent auto-binding.
- Override device index per-command with `device=N` in the `open` sub-command.

**Windows:**
- The FTDI D2XX DLL (`FTD2XX.dll`) must be present — either from a system-wide installation or copied next to the plugin (the build does this automatically).
- Set `FTD2XX_ROOT` during CMake configuration to point at the extracted FTDI D2XX SDK.
- Device index `0` selects the first enumerated FT232H; use `1`, `2`, etc. for additional devices.
- Override with `device=N` in the `open` sub-command.

---

## Command Reference

### INFO

Prints version and a complete command reference for all modules. Takes **no arguments** and works even before `doInit()`.

```
FT232H.INFO
```

**Example output (abbreviated):**
```
FT232H     | Vers: 1.0.0.0
FT232H     | SPI, I2C, GPIO, UART interfaces via FTDI D2XX
FT232H     | Note: single MPSSE channel — open at most one of SPI/I2C/GPIO at a time
...
```

---

### SPI

The SPI module drives the FT232H MPSSE engine in SPI master mode. CS is automatically asserted at the start of every transfer and de-asserted at the end — there is no manual CS toggle command.

**FT232H SPI pin mapping (ADBUS):**

| Signal | ADBUS pin |
|---|---|
| SCK | ADBUS0 |
| MOSI | ADBUS1 |
| MISO | ADBUS2 |
| CS (default) | ADBUS3 (csPin=0x08) |

---

#### SPI · open — Open SPI interface

```
FT232H.SPI open [clock=N] [mode=0-3] [bitorder=msb|lsb] [cspin=N] [cspol=low|high] [device=N]
```

| Argument | Description | Default |
|---|---|---|
| `clock` | SPI clock frequency in Hz | `1000000` (1 MHz) |
| `mode` | SPI mode (0–3, CPOL/CPHA) | `0` |
| `bitorder` | `msb` or `lsb` | `msb` |
| `cspin` | Chip-select pin bitmask on ADBUS | `0x08` (ADBUS3) |
| `cspol` | CS polarity: `low` (active-low) or `high` (active-high) | `low` |
| `device` | Zero-based FT232H device index | `0` |

```
# Open at 10 MHz, SPI mode 0, default CS (ADBUS3, active-low)
FT232H.SPI open clock=10000000

# Open at 1 MHz, SPI mode 1, MSB-first, device 1
FT232H.SPI open clock=1000000 mode=1 bitorder=msb device=1

# Open at 5 MHz, active-high CS on ADBUS4
FT232H.SPI open clock=5000000 cspin=0x10 cspol=high
```

---

#### SPI · close — Release SPI interface

```
FT232H.SPI close
```

---

#### SPI · cfg — Update SPI configuration without reopening

Updates the pending SPI configuration. If SPI is currently open, the change is stored and takes effect on the next `open`. Query the current pending config with `?`.

```
FT232H.SPI cfg [clock=N] [mode=0-3] [bitorder=msb|lsb] [cspin=N] [cspol=low|high]
FT232H.SPI cfg ?
```

```
FT232H.SPI cfg clock=5000000 mode=2
FT232H.SPI cfg ?
```

---

#### SPI · cs — CS information

The CS line is driven automatically per-transfer. This command is informational only.

```
FT232H.SPI cs
```

---

#### SPI · write — Transmit bytes (MOSI only)

Sends hex-encoded bytes on MOSI while clocking — no MISO data is captured.

```
FT232H.SPI write <HEXDATA>
```

```
# Write 3 bytes
FT232H.SPI write DEADBE

# Send a JEDEC read-ID command (0x9F)
FT232H.SPI write 9F
```

---

#### SPI · read — Receive N bytes (clock zeros on MOSI)

Clocks out N zero bytes and captures the MISO data, then prints it as a hex dump.

```
FT232H.SPI read <N>
```

```
# Read 4 bytes from an SPI device
FT232H.SPI read 4
```

---

#### SPI · wrrd — Full-duplex write then read

Writes hex data while simultaneously or sequentially reading. Accepts three forms:

```
FT232H.SPI wrrd <HEXDATA>:<rdlen>   # write + read
FT232H.SPI wrrd :<rdlen>            # read only
FT232H.SPI wrrd <HEXDATA>           # write only
```

The implementation performs a true full-duplex `spi_transfer()` when both write data and a read length are specified; otherwise it falls back to write-only or read-only operations.

```
# Send JEDEC Read-ID command and read back 3 bytes
FT232H.SPI wrrd 9F:3

# Write 2 bytes, read back 4 bytes (full-duplex)
FT232H.SPI wrrd AABB:4

# Write only (no readback)
FT232H.SPI wrrd DEADBEEF

# Read only (clock zeros)
FT232H.SPI wrrd :8
```

---

#### SPI · wrrdf — File-backed write-then-read

Reads write data from a binary file in `ARTEFACTS_PATH` and streams it to the device in chunks, capturing the response.

```
FT232H.SPI wrrdf <filename>[:<wrchunk>][:<rdchunk>]
```

| Argument | Description | Default |
|---|---|---|
| `filename` | Binary file located under `ARTEFACTS_PATH` | — |
| `wrchunk` | Write chunk size in bytes | `4096` |
| `rdchunk` | Read chunk size in bytes | `4096` |

```
FT232H.SPI wrrdf flash_image.bin
FT232H.SPI wrrdf flash_image.bin:512:512
```

---

#### SPI · xfer — Full-duplex transfer (simultaneous TX/RX)

Transmits hex-encoded bytes while simultaneously capturing the same number of bytes from MISO. Both TX and RX lengths are identical (length of the provided hex data).

```
FT232H.SPI xfer <HEXDATA>
```

```
# Send 4 bytes, receive 4 bytes simultaneously
FT232H.SPI xfer DEADBEEF

# Full-duplex ping to verify loopback
FT232H.SPI xfer AABBCCDD
```

---

#### SPI · script — Execute a command script

**SPI must be open first.**

```
FT232H.SPI script <filename>
```

```
FT232H.SPI script flash_read_id.txt
FT232H.SPI script spi_test_sequence.txt
```

---

#### SPI · help — List available sub-commands

```
FT232H.SPI help
```

---

### I2C

The I2C module drives the FT232H MPSSE engine in I2C master mode. The FT232H supports I2C up to 3.4 MHz with appropriate hardware pull-ups. I2C transfers follow standard START / address+R/W / data / STOP framing managed by the driver.

**FT232H I2C pin mapping (ADBUS):**

| Signal | ADBUS pin |
|---|---|
| SCL | ADBUS0 |
| SDA | ADBUS1 + ADBUS2 (bidirectional) |

---

#### I2C · open — Open I2C interface

```
FT232H.I2C open [addr=0xNN] [clock=N] [device=N]
```

| Argument | Description | Default |
|---|---|---|
| `addr` / `address` | 7-bit I2C target device address (hex) | `0x50` |
| `clock` | I2C clock frequency in Hz | `100000` (100 kHz) |
| `device` | Zero-based FT232H device index | `0` |

```
FT232H.I2C open addr=0x50 clock=400000
FT232H.I2C open addr=0x68
FT232H.I2C open clock=100000 device=1
```

---

#### I2C · close — Release I2C interface

```
FT232H.I2C close
```

---

#### I2C · cfg — Update I2C configuration without reopening

Updates the pending I2C address and/or clock. Query current config with `?`.

```
FT232H.I2C cfg [addr=0xNN] [clock=N]
FT232H.I2C cfg ?
```

```
FT232H.I2C cfg addr=0x68
FT232H.I2C cfg clock=400000
FT232H.I2C cfg ?
```

---

#### I2C · write — Transmit bytes to the target device

Issues START, sends address + W, writes data bytes, then STOP.

```
FT232H.I2C write <HEXDATA>
```

```
# Write a register address 0x00 to device
FT232H.I2C write 00

# Write register 0x01 with value 0xFF
FT232H.I2C write 01FF
```

---

#### I2C · read — Receive N bytes from the target device

Issues START, sends address + R, reads N bytes (ACK all but last), then STOP.

```
FT232H.I2C read <N>
```

```
# Read 2 bytes
FT232H.I2C read 2

# Read 16 bytes
FT232H.I2C read 16
```

---

#### I2C · wrrd — Write then read (combined transfer)

Sends write data (register address, command, etc.) then follows with a read phase. Either phase can be omitted.

```
FT232H.I2C wrrd <HEXDATA>:<rdlen>   # write + read
FT232H.I2C wrrd :<rdlen>            # read only
FT232H.I2C wrrd <HEXDATA>           # write only
```

```
# Write register 0x00, read back 2 bytes
FT232H.I2C wrrd 0000:2

# Write command 0xF3 (trigger measurement), then read 3 bytes
FT232H.I2C wrrd F3:3

# Read 8 bytes without a preceding write
FT232H.I2C wrrd :8
```

---

#### I2C · wrrdf — File-backed write-then-read

```
FT232H.I2C wrrdf <filename>[:<wrchunk>][:<rdchunk>]
```

```
FT232H.I2C wrrdf eeprom_data.bin
FT232H.I2C wrrdf eeprom_data.bin:64:64
```

---

#### I2C · scan — Probe all I2C addresses

Probes the 7-bit address space `0x08–0x77` by briefly opening each address and attempting a zero-byte write. Reports all responding devices.

```
FT232H.I2C scan
```

**Example output:**
```
FT_GENERIC | I2C: Scanning I2C bus...
FT_GENERIC | I2C: Found device at 0x50
FT_GENERIC | I2C: Found device at 0x68
```

> **Note:** The scan uses the currently configured `clock` and `device` index. Opening a specific address for the plugin via `I2C open` is not required before scanning.

---

#### I2C · script — Execute a command script

**I2C must be open first.**

```
FT232H.I2C script <filename>
```

```
FT232H.I2C script eeprom_dump.txt
FT232H.I2C script sensor_init.txt
```

---

#### I2C · help — List available sub-commands

```
FT232H.I2C help
```

---

### GPIO

The GPIO module exposes the FT232H MPSSE GPIO pins as two 8-bit banks:

| Bank | Pins | Direction bit |
|---|---|---|
| `low` | ADBUS[7:0] | 1 = output, 0 = input |
| `high` | ACBUS[7:0] | 1 = output, 0 = input |

**Note:** When the SPI or I2C module is open, the low bank's lower bits (ADBUS[2:0] and the CS pin) are under MPSSE protocol control. Only use GPIO concurrently with SPI/I2C on the high bank (ACBUS) or on pins not used by the active protocol.

---

#### GPIO · open — Open GPIO interface

```
FT232H.GPIO open [device=N] [lowdir=0xNN] [lowval=0xNN] [highdir=0xNN] [highval=0xNN]
```

| Argument | Description | Default |
|---|---|---|
| `device` | Zero-based FT232H device index | `0` |
| `lowdir` | Direction mask for low bank (ADBUS): `1`=output, `0`=input | `0x00` (all inputs) |
| `lowval` | Initial output values for low bank | `0x00` |
| `highdir` | Direction mask for high bank (ACBUS) | `0x00` (all inputs) |
| `highval` | Initial output values for high bank | `0x00` |

```
# All inputs
FT232H.GPIO open

# Low bank: bits 7:4 as outputs (initialized low), high bank all inputs
FT232H.GPIO open lowdir=0xF0 lowval=0x00

# Both banks: lower nibble of each bank as outputs
FT232H.GPIO open lowdir=0x0F highdir=0x0F
```

---

#### GPIO · close — Release GPIO interface

```
FT232H.GPIO close
```

---

#### GPIO · cfg — Update GPIO configuration without reopening

Updates the pending direction/value masks. Takes effect on the next `open`.

```
FT232H.GPIO cfg [lowdir=0xNN] [lowval=0xNN] [highdir=0xNN] [highval=0xNN]
FT232H.GPIO cfg ?
```

---

#### GPIO · dir — Set direction of pins in a bank

Applies a direction mask to a bank while the GPIO interface is open.

```
FT232H.GPIO dir [low|high] <MASK>
```

`1` bits select output; `0` bits select input.

```
# Configure low bank bits 0–3 as outputs
FT232H.GPIO dir low 0x0F

# Configure high bank bit 7 as output only
FT232H.GPIO dir high 0x80
```

---

#### GPIO · write — Write a full byte to a bank

Writes an absolute 8-bit value to the specified bank (all output-configured bits are updated).

```
FT232H.GPIO write [low|high] <VALUE>
```

```
FT232H.GPIO write low 0xAA
FT232H.GPIO write high 0x55
```

---

#### GPIO · set — Drive masked pins HIGH

```
FT232H.GPIO set [low|high] <MASK>
```

```
# Set bits 0 and 2 high on low bank
FT232H.GPIO set low 0x05

# Set bit 7 high on high bank
FT232H.GPIO set high 0x80
```

---

#### GPIO · clear — Drive masked pins LOW

```
FT232H.GPIO clear [low|high] <MASK>
```

```
FT232H.GPIO clear low 0x05
FT232H.GPIO clear high 0x80
```

---

#### GPIO · toggle — Invert masked pins

```
FT232H.GPIO toggle [low|high] <MASK>
```

```
FT232H.GPIO toggle low 0xFF
FT232H.GPIO toggle high 0x01
```

---

#### GPIO · read — Read current pin levels from a bank

Returns the current logical level of all 8 pins in the bank (regardless of direction), printed as a hex value and binary string (MSB first).

```
FT232H.GPIO read [low|high]
```

```
FT232H.GPIO read low
FT232H.GPIO read high
```

**Example output:**
```
FT232H_GPIO| Bank low: 0x5A  [01011010]
```

---

#### GPIO · help — List available sub-commands

```
FT232H.GPIO help
```

---

### UART

The UART module opens the FT232H in async serial (VCP) mode instead of MPSSE. This is **mutually exclusive with SPI, I2C, and GPIO** on the same chip. To use UART alongside MPSSE interfaces, connect a second FT232H chip and address it with `device=N`.

---

#### UART · open — Open UART interface

```
FT232H.UART open [baud=N] [data=8] [stop=0] [parity=none|odd|even|mark|space] [flow=none|hw] [device=N]
```

| Argument | Description | Default |
|---|---|---|
| `baud` | Baud rate | `115200` |
| `data` | Data bits | `8` |
| `stop` | Stop bits | `0` |
| `parity` | `none`, `odd`, `even`, `mark`, or `space` | `none` |
| `flow` | `none` or `hw` (RTS/CTS hardware flow control) | `none` |
| `device` | Zero-based FT232H device index | `0` |

```
FT232H.UART open baud=115200
FT232H.UART open baud=9600 data=8 stop=1 parity=none flow=hw device=1
FT232H.UART open baud=460800
```

---

#### UART · close — Release UART interface

```
FT232H.UART close
```

---

#### UART · cfg — Update UART parameters

Parameters can be updated while UART is open (applied immediately) or stored for the next `open`. The `device` argument is not accepted by `cfg` — use `open` to change the device.

```
FT232H.UART cfg [baud=N] [data=8] [stop=0] [parity=none|odd|even|mark|space] [flow=none|hw]
```

```
FT232H.UART cfg baud=9600 parity=even
FT232H.UART cfg baud=115200 flow=hw
```

---

#### UART · write — Transmit hex bytes over UART

```
FT232H.UART write <HEXDATA>
```

```
# Send 4 bytes
FT232H.UART write DEADBEEF

# Send ASCII "Hello"
FT232H.UART write 48656C6C6F
```

---

#### UART · read — Receive N bytes over UART

Blocks until N bytes are received or `READ_TIMEOUT` expires. Received bytes are printed as a hex dump.

```
FT232H.UART read <N>
```

```
FT232H.UART read 4
FT232H.UART read 64
```

---

#### UART · script — Execute a command script

**UART must be open first.**

```
FT232H.UART script <filename>
```

```
FT232H.UART script uart_handshake.txt
FT232H.UART script modem_init.txt
```

---

#### UART · help — List available sub-commands

```
FT232H.UART help
```

---

## SPI Clock Reference

The FT232H SPI clock is derived from the 60 MHz MPSSE base clock and can be set to any frequency from ~0 Hz up to 30 MHz. The plugin defines the following named presets:

| Preset label | Frequency |
|---|---|
| `100kHz` | 100 kHz |
| `500kHz` | 500 kHz |
| `1MHz` | 1 MHz (power-on default) |
| `2MHz` | 2 MHz |
| `5MHz` | 5 MHz |
| `10MHz` | 10 MHz |
| `20MHz` | 20 MHz |
| `30MHz` | 30 MHz (maximum) |

A raw Hz value is also accepted (e.g., `clock=7500000`). The D2XX driver rounds down to the nearest achievable frequency for non-preset values.

---

## I2C Speed Reference

The FT232H supports I2C in standard, fast, fast-plus, and high-speed modes depending on bus pull-up strengths. Named presets:

| Preset label | Frequency |
|---|---|
| `50kHz` | 50 kHz |
| `100kHz` | 100 kHz (power-on default; standard mode) |
| `400kHz` | 400 kHz (fast mode) |
| `1MHz` | 1 MHz (fast-plus mode) |
| `3.4MHz` | 3.4 MHz (high-speed mode — requires appropriate hardware) |

Raw Hz values are also accepted.

---

## UART Baud Rate Reference

Named baud rate presets for the UART module:

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

A raw integer value is also accepted (e.g., `baud=1000000`). The FT232H supports arbitrary baud rates up to ~3 Mbps depending on the host system and cable.

---

## Script Files

Script files are plain text files located under `ARTEFACTS_PATH`. They are executed by the `CommScriptClient` engine, which reads each line and performs send/receive/expect operations. The `SCRIPT_DELAY` INI key inserts a per-command delay in milliseconds.

**Important:** The corresponding interface must be open before calling `script`. The plugin passes the already-open driver handle directly to `CommScriptClient`, so no reconnection occurs inside the script.

```
FT232H.SPI open clock=10000000
FT232H.SPI script flash_read_id.txt

FT232H.I2C open speed=400kHz addr=0x50
FT232H.I2C script eeprom_dump.txt

FT232H.UART open baud=115200
FT232H.UART script serial_handshake.txt
```

---

## Fault-Tolerant and Dry-Run Modes

- **Dry-run mode**: when `doEnable()` has not been called, every command validates its arguments and returns `true` without touching hardware. The generic dispatcher detects `isEnabled() == false` and returns early. This is used by test framework validators to check command syntax before a live run.

- **Fault-tolerant mode** (`setFaultTolerant()` / `isFaultTolerant()`): when set, the plugin framework can be configured to continue execution past command failures. Useful in production test scripts where a non-critical probe failure should not abort a longer sequence.

- **Privileged mode** (`isPrivileged()`): always returns `false`; reserved for future framework use.

---

## Error Handling and Return Values

Every handler returns `bool`:
- `true` — success (or argument validation passed in disabled/dry-run mode).
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
| `LOG_FIXED` | Help text output and scan results |

Log verbosity is controlled by the host application via the shared `uLogger` configuration.
