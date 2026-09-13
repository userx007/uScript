# HydraBus Plugin

A C++ shared-library plugin that exposes the [HydraBus](https://hydrabus.com/?v=5f02f0889301) multi-protocol hardware interface through a unified command dispatcher. The plugin covers all protocol modes supported by the HydraHAL library: **SPI**, **I2C**, **UART**, **1-Wire**, **Raw-Wire**, **SWD**, **Smartcard**, **NFC**, **MMC**, and **SDIO**.

**Version:** 1.0.0.0

---

## Table of Contents

1. [Overview](#overview)
2. [Project Structure](#project-structure)
3. [Architecture](#architecture)
   - [Plugin Lifecycle](#plugin-lifecycle)
   - [Command Dispatch Model](#command-dispatch-model)
   - [Generic Template Helpers](#generic-template-helpers)
   - [INI Configuration Keys](#ini-configuration-keys)
4. [Building](#building)
5. [Command Reference](#command-reference)
   - [INFO](#info)
   - [MODE](#mode)
   - [SPI](#spi)
   - [I2C](#i2c)
   - [UART](#uart)
   - [ONEWIRE](#onewire)
   - [RAWWIRE](#rawwire)
   - [SWD](#swd)
   - [SMARTCARD](#smartcard)
   - [NFC](#nfc)
   - [MMC](#mmc)
   - [SDIO](#sdio)
6. [Script Files](#script-files)
7. [AUX GPIO Control (aux)](#aux-gpio-control-aux)
8. [Fault-Tolerant and Privileged Modes](#fault-tolerant-and-privileged-modes)
9. [Error Handling and Return Values](#error-handling-and-return-values)

---

## Overview

The plugin loads as a dynamic shared library (`.so` / `.dll`). The host application calls the exported C entry points `pluginEntry()` / `pluginExit()` to create and destroy the plugin object. Once loaded, the host passes configuration settings (serial port, baud rate, timeouts, artefacts path…) via `setParams()`, optionally calls `doInit()` to open the UART connection, and then calls `doDispatch()` for every command it wants to execute.

All commands follow the pattern:

```
<PLUGIN>.<COMMAND> [subcommand] [arguments]
```

For example:

```
HYDRABUS.MODE  spi
HYDRABUS.SPI   speed 10MHz
HYDRABUS.SPI   cfg   polarity=0 phase=1
HYDRABUS.SPI   cs    en
HYDRABUS.SPI   wrrd  9F:3
HYDRABUS.SPI   cs    dis
HYDRABUS.MODE  bbio
```

---

## Project Structure

```
hydrabus_plugin/
├── CMakeLists.txt                  # Build definition (shared library, C++20)
├── inc/
│   ├── hydrabus_plugin.hpp         # Main class definition + command tables
│   ├── hydrabus_generic.hpp        # Generic template helpers (dispatch, write, speed, script)
│   └── private/
│       ├── mode_config.hpp         # MODE_COMMANDS_CONFIG_TABLE
│       ├── spi_config.hpp          # SPI command + speed tables
│       ├── i2c_config.hpp          # I2C command + speed tables
│       └── protocol_configs.hpp    # UART, OneWire, RawWire, SWD, Smartcard, NFC, MMC, SDIO tables
└── src/
    ├── hydrabus_plugin.cpp         # Entry points, init/cleanup, INFO, MODE, setParams
    ├── hydrabus_spi.cpp            # SPI sub-commands + SPI_COMMANDS/SPEED config tables
    ├── hydrabus_i2c.cpp            # I2C sub-commands + I2C_COMMANDS/SPEED config tables
    ├── hydrabus_uart.cpp           # UART sub-commands + UART_COMMANDS config table
    ├── hydrabus_onewire.cpp        # 1-Wire sub-commands + ONEWIRE_COMMANDS config table
    ├── hydrabus_rawwire.cpp        # Raw-Wire sub-commands + RAWWIRE_COMMANDS/SPEED config tables
    ├── hydrabus_swd.cpp            # SWD sub-commands + SWD_COMMANDS config table
    ├── hydrabus_smartcard.cpp      # Smartcard sub-commands + SMARTCARD_COMMANDS config table
    ├── hydrabus_nfc.cpp            # NFC sub-commands + NFC_COMMANDS config table
    ├── hydrabus_mmc.cpp            # MMC sub-commands + MMC_COMMANDS config table
    └── hydrabus_sdio.cpp           # SDIO sub-commands + SDIO_COMMANDS config table
```

Each protocol lives in its own `.cpp` file, which also defines its own command/speed configuration tables using X-macro patterns.

---

## Architecture

### Plugin Lifecycle

```
pluginEntry()           → creates HydrabusPlugin instance
  setParams()           → loads INI values (port, baud rate, timeouts, artefacts path...)
  doInit()              → opens the UART connection to the HydraBus hardware
  doEnable()            → enables real execution (without this, commands validate args only)
  doDispatch(cmd, args) → routes a command string to the correct handler
  doCleanup()           → tears down active protocol, closes the UART port
pluginExit(ptr)         → deletes the HydrabusPlugin instance
```

`doEnable()` controls a "dry-run / validation" mode: when not enabled, every command validates its arguments and returns `true` without touching the hardware. This allows test frameworks to verify command syntax before the device is connected.

### Command Dispatch Model

Commands are stored in two layers of `std::map`:

1. **Top-level map** (`m_mapCmds`): associates a command name (`INFO`, `MODE`, `SPI`, `I2C`, …) with a member-function pointer.
2. **Module-level maps** (`m_mapCmds_SPI`, `m_mapCmds_I2C`, …): each protocol module owns its own map associating sub-command names (`cfg`, `speed`, `write`, `read`, …) with handler pointers.

A meta-map (`m_mapCommandsMaps`) maps the module name string to its sub-map, allowing the generic dispatcher to locate any sub-command dynamically.

The registration is driven entirely by X-macros defined in the `*_config.hpp` headers:

```cpp
// In spi_config.hpp  (simplified):
#define SPI_COMMANDS_CONFIG_TABLE  \
SPI_CMD_RECORD( cfg   )            \
SPI_CMD_RECORD( speed )            \
SPI_CMD_RECORD( cs    )            \
...

// In the constructor (hydrabus_plugin.hpp):
#define SPI_CMD_RECORD(a) \
    m_mapCmds_SPI.insert({#a, &HydrabusPlugin::m_handle_spi_##a});
SPI_COMMANDS_CONFIG_TABLE
#undef SPI_CMD_RECORD
```

Adding a new sub-command only requires adding one line to the config table and implementing the handler function—no other registration code is needed.

### Generic Template Helpers

`hydrabus_generic.hpp` provides stateless template functions used by all protocol modules:

| Template function | Purpose |
|---|---|
| `generic_module_dispatch<T>()` | Splits `"subcmd args"` and routes to the module map |
| `generic_module_set_speed<T>()` | Looks up a speed label in the module speed map and calls `setModuleSpeed()` |
| `generic_write_data<T>()` | Unhexlifies a hex string and calls a protocol-specific bulk-write callback |
| `generic_write_read_data<T>()` | Parses `"HEXDATA:rdlen"` and calls a protocol-specific wrrd callback |
| `generic_write_read_file<T>()` | Reads write data from a binary file in chunks and streams results |
| `generic_execute_script<T>()` | Opens a script file from `ARTEFACTS_PATH` and runs it via `CommScriptClient` |
| `generic_module_list_commands<T>()` | Logs all registered sub-command names (used by `help`) |

### INI Configuration Keys

The following keys are read from the host configuration/INI file:

| Key | Type | Description |
|---|---|---|
| `UART_PORT` | string | Serial port path (e.g., `/dev/ttyUSB0`, `COM3`) |
| `BAUDRATE` | uint32 | Baud rate for the host↔HydraBus UART |
| `READ_TIMEOUT` | uint32 | Per-byte read timeout in milliseconds |
| `WRITE_TIMEOUT` | uint32 | Per-byte write timeout in milliseconds |
| `READ_BUF_SIZE` | uint32 | Receive buffer size for script execution |
| `SCRIPT_DELAY` | uint32 | Inter-command delay in milliseconds during script execution |
| `ARTEFACTS_PATH` | string | Base directory for script and binary data files |

---

## Building

The plugin is built as a CMake shared library targeting C++20. It links against several utility libraries (`uSharedConfig`, `uIPlugin`, `uPluginOps`, `uICoreScript`, `uCommScriptClient`, `uUart`, `uUtils`…) and the `HydraHAL` protocol library, which must all be available in the CMake build tree.

```bash
mkdir build && cd build
cmake ..
make hydrabus_plugin
```

The output is `libhydrabus_plugin.so` (Linux) or `hydrabus_plugin.dll` (Windows).

---

## Command Reference

### INFO

Prints version information, the active port/baud configuration, and a complete usage summary of all supported commands directly to the logger. This command takes **no arguments** and works even if `doInit()` failed (i.e., no hardware is required).

```
HYDRABUS.INFO
```

**Example output (abbreviated):**
```
HYDRABUS   | Vers: 1.0.0.0
HYDRABUS   | Description: HydraBus multi-protocol interface (SPI/I2C/UART/1-Wire/RawWire/SWD/Smartcard/NFC/MMC/SDIO)
HYDRABUS   |   Port: /dev/ttyUSB0  Baud: 115200
...
```

---

### MODE

Switch the HydraBus into a specific protocol mode. This **must be called before any protocol command**. Only one protocol is active at a time; switching modes tears down the previous protocol instance. The mode manager enters BBIO first, then constructs the HydraHAL protocol object.

```
HYDRABUS.MODE <mode>
```

#### Sub-commands

| Sub-command | Mode byte | Repetitions | Expected response | Description |
|---|---|---|---|---|
| `bbio` | `0x00` | ×20 | `BBIO1` | Enter raw bitbang mode / exit current protocol |
| `spi` | `0x01` | ×1 | `SPI1` | Enter SPI mode |
| `i2c` | `0x02` | ×1 | `I2C1` | Enter I2C mode |
| `uart` | `0x03` | ×1 | `ART1` | Enter UART mode |
| `onewire` | `0x04` | ×1 | `1W01` | Enter 1-Wire mode |
| `rawwire` | `0x05` | ×1 | `RAW1` | Enter Raw-Wire mode |
| `smartcard` | `0x0B` | ×1 | `CRD1` | Enter ISO 7816 Smartcard mode |
| `nfc` | `0x0C` | ×1 | `NFC1` | Enter NFC reader mode |
| `mmc` | `0x0D` | ×1 | `MMC1` | Enter MMC/eMMC mode |
| `sdio` | `0x0E` | ×1 | `SDI1` | Enter SDIO mode |
| `swd` | `0x05` | ×1 | `RAW1` | Enter ARM SWD mode (via Raw-Wire transport) |

#### Examples

```
# Enter SPI mode before any SPI commands
HYDRABUS.MODE spi

# Enter I2C mode
HYDRABUS.MODE i2c

# Enter 1-Wire mode
HYDRABUS.MODE onewire

# Enter ARM SWD mode
HYDRABUS.MODE swd

# Enter NFC reader mode
HYDRABUS.MODE nfc

# Return to BBIO idle (exit current protocol)
HYDRABUS.MODE bbio
```

---

### SPI

Full-duplex SPI bus interface. **Prerequisite: `HYDRABUS.MODE spi`**

```
HYDRABUS.SPI <subcommand> <args>
```

---

#### SPI · cfg — Configure SPI bus settings

Sets clock polarity, phase, and the hardware device index. The configuration is applied via `key=value` pairs; only the keys present in the argument are modified.

```
HYDRABUS.SPI cfg [polarity=0|1] [phase=0|1] [device=0|1]
```

| Key | Values | Description |
|---|---|---|
| `polarity` | `0` / `1` | Clock idle level: 0 = low (CPOL=0), 1 = high (CPOL=1) |
| `phase` | `0` / `1` | Clock phase: 0 = first edge (CPHA=0), 1 = second edge (CPHA=1) |
| `device` | `0` / `1` | Hardware SPI device index |

```
# Standard SPI mode 0 (CPOL=0, CPHA=0)
HYDRABUS.SPI cfg polarity=0 phase=0 device=0

# SPI mode 1 (CPOL=0, CPHA=1)
HYDRABUS.SPI cfg polarity=0 phase=1

# SPI mode 3 (CPOL=1, CPHA=1)
HYDRABUS.SPI cfg polarity=1 phase=1

# Print the current configuration
HYDRABUS.SPI cfg ?
```

---

#### SPI · cs — Chip-select control

```
HYDRABUS.SPI cs <en|dis>
```

| Argument | Effect |
|---|---|
| `en` | Assert CS → GND (logic low, chip selected) |
| `dis` | Deassert CS → high (chip deselected) |

```
# Select the target chip
HYDRABUS.SPI cs en

# Deselect the target chip
HYDRABUS.SPI cs dis
```

---

#### SPI · speed — Set clock frequency

```
HYDRABUS.SPI speed <frequency>
```

Available presets:

| Label | Index | Frequency |
|---|---|---|
| `320kHz` | 0 | 320 kHz (default) |
| `650kHz` | 1 | 650 kHz |
| `1MHz` | 2 | 1 MHz |
| `2MHz` | 3 | 2 MHz |
| `5MHz` | 4 | 5 MHz |
| `10MHz` | 5 | 10 MHz |
| `21MHz` | 6 | 21 MHz |
| `42MHz` | 7 | 42 MHz |

```
HYDRABUS.SPI speed 10MHz
HYDRABUS.SPI speed 42MHz
HYDRABUS.SPI speed 320kHz
```

---

#### SPI · write — Bulk SPI transfer (1–16 bytes)

Full-duplex transfer: sends the given bytes on MOSI while clocking MISO. The received MISO bytes are printed as a hex dump.

```
HYDRABUS.SPI write <HEXDATA>
```

- `HEXDATA` is a hex string, 1–16 bytes (2–32 hex characters).

```
# Send a single byte (e.g., read JEDEC ID command on a flash)
HYDRABUS.SPI write 9F

# Send four bytes
HYDRABUS.SPI write DEADBEEF

# Write a 16-byte block (maximum)
HYDRABUS.SPI write 000102030405060708090A0B0C0D0E0F
```

---

#### SPI · read — Receive bytes

Clocks `0xFF` bytes on MOSI to generate the clock signal and captures MISO. The received bytes are printed as a hex dump.

```
HYDRABUS.SPI read <N>
```

```
# Read 4 bytes
HYDRABUS.SPI read 4

# Read 1 byte
HYDRABUS.SPI read 1
```

---

#### SPI · wrrd — Write then read

Sends a write-then-read transaction in a single operation. Write data and read length are separated by a colon.

```
HYDRABUS.SPI wrrd <HEXDATA>:<rdlen>
```

- `HEXDATA` — hex string for the write phase (can be empty for read-only).
- `rdlen` — decimal number of bytes to read back.

```
# JEDEC ID: send 0x9F, read 3 bytes (manufacturer, type, capacity)
HYDRABUS.SPI wrrd 9F:3

# Status register read (0x05)
HYDRABUS.SPI wrrd 05:1

# Read 256 bytes starting at address 0x000000 (READ command 0x03)
HYDRABUS.SPI wrrd 03000000:256

# Write Enable (0x06) — write only, no read
HYDRABUS.SPI wrrd 06:0

# Read-only (no write bytes)
HYDRABUS.SPI wrrd :4
```

---

#### SPI · wrrdf — Write/read using binary files

Same as `wrrd` but the write data is loaded from a binary file and the read data is saved to a file, both resolved under `ARTEFACTS_PATH`.

```
HYDRABUS.SPI wrrdf <filename>[:<wrchunk>][:<rdchunk>]
```

```
# Send the contents of flash_cmd.bin and save the response
HYDRABUS.SPI wrrdf flash_cmd.bin

# With explicit chunk sizes for large files
HYDRABUS.SPI wrrdf program_page.bin:256:256
```

---

#### SPI · script — Run a command script

Executes a text script from `ARTEFACTS_PATH` via the `CommScriptClient` engine.

```
HYDRABUS.SPI script <filename>
```

```
HYDRABUS.SPI script read_flash.txt
HYDRABUS.SPI script erase_chip.txt
```

---

#### SPI · aux — AUX GPIO control

See [AUX GPIO Control](#aux-gpio-control-aux).

```
HYDRABUS.SPI aux 0 out 1    # set AUX0 as output, drive high
HYDRABUS.SPI aux 1 in       # set AUX1 as input
```

---

### I2C

I2C bus master. **Prerequisite: `HYDRABUS.MODE i2c`**

```
HYDRABUS.I2C <subcommand> <args>
```

---

#### I2C · cfg — Configure I2C bus settings

```
HYDRABUS.I2C cfg [pullup=0|1]
```

| Key | Values | Description |
|---|---|---|
| `pullup` | `0` / `1` | Enable / disable internal pull-up resistors on SDA and SCL |

```
HYDRABUS.I2C cfg pullup=1   # enable internal pull-ups
HYDRABUS.I2C cfg ?          # print current cfg
```

---

#### I2C · speed — Set clock speed

```
HYDRABUS.I2C speed <frequency>
```

| Label | Index | Frequency |
|---|---|---|
| `50kHz` | 0 | ~50 kHz |
| `100kHz` | 1 | ~100 kHz (standard) |
| `400kHz` | 2 | ~400 kHz (fast mode) |
| `1MHz` | 3 | ~1 MHz (fast-mode plus) |

```
HYDRABUS.I2C speed 100kHz
HYDRABUS.I2C speed 400kHz
```

---

#### I2C · bit — Send bus control sequences

Sends individual I2C framing signals. Use this for manual transaction construction when `wrrd` is not sufficient.

```
HYDRABUS.I2C bit <start|stop|ack|nack>
```

| Argument | Description |
|---|---|
| `start` | Assert START condition (SDA high→low while SCL high) |
| `stop` | Assert STOP condition (SDA low→high while SCL high) |
| `ack` | Clock out ACK bit (SDA low) |
| `nack` | Clock out NACK bit (SDA high) |

```
# Manually build a read transaction for a device at 0x50
HYDRABUS.I2C bit start
HYDRABUS.I2C write A1         # address 0x50 with R/W=1
HYDRABUS.I2C read 1
HYDRABUS.I2C bit nack
HYDRABUS.I2C bit stop
```

---

#### I2C · write — Bulk I2C write (1–16 bytes)

Sends up to 16 bytes. ACK/NACK status is reported for each byte.

```
HYDRABUS.I2C write <HEXDATA>
```

```
# Write device address 0xA0 (write direction) + register 0x10
HYDRABUS.I2C write A010

# Write 4 bytes of data to a register
HYDRABUS.I2C write A0103A7F
```

---

#### I2C · read — Read N bytes

Reads N bytes, sending ACK after each byte except the last (which receives NACK).

```
HYDRABUS.I2C read <N>
```

```
# Read 2 bytes (e.g., a 16-bit sensor register)
HYDRABUS.I2C read 2

# Read 8 bytes
HYDRABUS.I2C read 8
```

---

#### I2C · wrrd — Write then read

Atomic write-then-repeated-START-read transaction (standard I2C register access pattern).

```
HYDRABUS.I2C wrrd <HEXDATA>:<rdlen>
```

```
# Write device address + register, read 2 bytes
# Device 0x48 (TMP102 temperature sensor), register 0x00
HYDRABUS.I2C wrrd 9000:2

# Read 4 bytes from EEPROM at address 0x50, register 0x10
HYDRABUS.I2C wrrd A010:4

# Write only (no read bytes)
HYDRABUS.I2C wrrd A010FF:0

# Read only (no write bytes)
HYDRABUS.I2C wrrd :2
```

---

#### I2C · wrrdf — Write/read using binary files

```
HYDRABUS.I2C wrrdf <filename>[:<wrchunk>][:<rdchunk>]
```

```
HYDRABUS.I2C wrrdf i2c_sequence.bin
HYDRABUS.I2C wrrdf eeprom_write.bin:16:0
```

---

#### I2C · scan — Bus address scan

Probes all 7-bit addresses (0x00–0x7F) and prints the address of every device that responds with an ACK.

```
HYDRABUS.I2C scan
```

```
HYDRABUS.I2C scan
# Example output:
# HB_I2C | Scanning I2C bus...
# HB_I2C | Found device at 0x48
# HB_I2C | Found device at 0x50
```

---

#### I2C · stretch — Configure clock-stretch timeout

Sets the maximum number of cycles the master will wait for a device to release the clock (clock stretching). Set to `0` to disable.

```
HYDRABUS.I2C stretch <N>
```

```
HYDRABUS.I2C stretch 1000   # allow up to 1000 stretch cycles
HYDRABUS.I2C stretch 0      # disable clock stretching
```

---

#### I2C · script — Run a command script

```
HYDRABUS.I2C script <filename>
```

```
HYDRABUS.I2C script eeprom_test.txt
HYDRABUS.I2C script sensor_init.txt
```

---

#### I2C · aux — AUX GPIO control

See [AUX GPIO Control](#aux-gpio-control-aux).

```
HYDRABUS.I2C aux 0 out 0    # drive AUX0 low
```

---

### UART

UART serial interface. **Prerequisite: `HYDRABUS.MODE uart`**

```
HYDRABUS.UART <subcommand> <args>
```

---

#### UART · baud — Set baud rate

Sets an arbitrary baud rate (any value supported by the HydraHAL UART driver).

```
HYDRABUS.UART baud <N>
```

```
HYDRABUS.UART baud 9600
HYDRABUS.UART baud 115200
HYDRABUS.UART baud 921600
```

---

#### UART · parity — Set parity

```
HYDRABUS.UART parity <none|even|odd>
```

```
HYDRABUS.UART parity none
HYDRABUS.UART parity even
HYDRABUS.UART parity odd
```

---

#### UART · echo — Enable/disable RX echo

Controls whether received bytes are echoed back to the host.

```
HYDRABUS.UART echo <on|off>
```

```
# Enable RX echo to monitor incoming data
HYDRABUS.UART echo on

# Disable echo (default state)
HYDRABUS.UART echo off
```

---

#### UART · bridge — Transparent bridge mode

Enters a transparent UART bridge. The HydraBus forwards all data bidirectionally between the host and the UART pins. **Exit by pressing the UBTN button on the HydraBus hardware.** This call is blocking until UBTN is pressed.

```
HYDRABUS.UART bridge
```

---

#### UART · write — Bulk UART transmit (1–16 bytes)

Transmits up to 16 bytes.

```
HYDRABUS.UART write <HEXDATA>
```

```
# Send the ASCII string "HELLO"
HYDRABUS.UART write 48454C4C4F

# Send a single byte (e.g., carriage return)
HYDRABUS.UART write 0D

# Send an AT command: "AT\r\n"
HYDRABUS.UART write 41540D0A
```

---

#### UART · read — Receive N bytes

```
HYDRABUS.UART read <N>
```

```
HYDRABUS.UART read 8
HYDRABUS.UART read 1
```

---

#### UART · script — Run a command script

```
HYDRABUS.UART script <filename>
```

```
HYDRABUS.UART script modem_init.txt
HYDRABUS.UART script at_commands.txt
```

---

#### UART · aux — AUX GPIO control

See [AUX GPIO Control](#aux-gpio-control-aux).

```
HYDRABUS.UART aux 3 out 1
```

---

### ONEWIRE

Dallas/Maxim 1-Wire bus master. **Prerequisite: `HYDRABUS.MODE onewire`**

```
HYDRABUS.ONEWIRE <subcommand> <args>
```

---

#### ONEWIRE · cfg — Configure bus settings

```
HYDRABUS.ONEWIRE cfg [pullup=0|1]
```

```
# Enable internal pull-up on DQ (required for standard-power DS18B20)
HYDRABUS.ONEWIRE cfg pullup=1

# Print current configuration
HYDRABUS.ONEWIRE cfg ?
```

---

#### ONEWIRE · reset — Send 1-Wire reset pulse

Sends a 480 µs reset pulse and detects whether any device asserts a presence pulse in response. Returns `1` if devices are present, `0` if no device was detected.

```
HYDRABUS.ONEWIRE reset
```

---

#### ONEWIRE · write — Bulk 1-Wire write (1–16 bytes)

Sends up to 16 bytes onto the 1-Wire bus.

```
HYDRABUS.ONEWIRE write <HEXDATA>
```

Common DS18B20 ROM command bytes:

| Hex | 1-Wire command |
|---|---|
| `CC` | SKIP ROM (broadcast) |
| `55` | MATCH ROM |
| `F0` | SEARCH ROM |
| `33` | READ ROM |
| `44` | Convert T (start temperature conversion) |
| `BE` | Read Scratchpad |

```
# Broadcast: skip ROM, then start temperature conversion
HYDRABUS.ONEWIRE write CC44

# Skip ROM + Read Scratchpad (9 bytes follow via read)
HYDRABUS.ONEWIRE write CCBE

# Select a specific device by 64-bit ROM address
HYDRABUS.ONEWIRE write 5528000000AABBCC
```

---

#### ONEWIRE · read — Read N bytes

Reads N bytes from the bus one at a time.

```
HYDRABUS.ONEWIRE read <N>
```

```
# Read the 9-byte DS18B20 scratchpad
HYDRABUS.ONEWIRE read 9

# Read the 8-byte ROM address of a single device
HYDRABUS.ONEWIRE read 8
```

---

#### ONEWIRE · swio — ARM Serial Wire I/O register access

Provides access to the SWIO (ARM CoreSight Serial Wire debug I/O) register bus, which shares the 1-Wire physical interface.

```
HYDRABUS.ONEWIRE swio init
HYDRABUS.ONEWIRE swio read  <ADDR>
HYDRABUS.ONEWIRE swio write <ADDR> <VALUE>
```

- `ADDR` — 1 hex byte (register address).
- `VALUE` — 4 hex bytes, little-endian (32-bit register value).

```
# Initialise the SWIO bus
HYDRABUS.ONEWIRE swio init

# Read register 0x00
HYDRABUS.ONEWIRE swio read 00

# Write 0x00000050 to register 0x04
HYDRABUS.ONEWIRE swio write 04 50000000
```

---

#### ONEWIRE · aux — AUX GPIO control

See [AUX GPIO Control](#aux-gpio-control-aux).

```
HYDRABUS.ONEWIRE aux 0 out 1
```

---

### RAWWIRE

Bit-bang 2-wire or 3-wire interface. Useful for proprietary serial protocols. **Prerequisite: `HYDRABUS.MODE rawwire`**

```
HYDRABUS.RAWWIRE <subcommand> <args>
```

---

#### RAWWIRE · cfg — Configure bus settings

```
HYDRABUS.RAWWIRE cfg [polarity=0|1] [wires=2|3] [gpio=0|1]
```

| Key | Values | Description |
|---|---|---|
| `polarity` | `0` / `1` | Clock idle level: 0 = low, 1 = high |
| `wires` | `2` / `3` | 2-wire (shared MOSI/MISO) or 3-wire (separate MOSI/MISO) |
| `gpio` | `0` / `1` | GPIO mode: 0 = normal, 1 = open-drain |

```
# 2-wire, clock idle low, standard GPIO
HYDRABUS.RAWWIRE cfg polarity=0 wires=2 gpio=0

# 3-wire, clock idle high
HYDRABUS.RAWWIRE cfg polarity=1 wires=3

# Print current configuration
HYDRABUS.RAWWIRE cfg ?
```

---

#### RAWWIRE · speed — Set bit-bang clock speed

```
HYDRABUS.RAWWIRE speed <frequency>
```

| Label | Frequency |
|---|---|
| `5kHz` | ~5 kHz |
| `50kHz` | ~50 kHz |
| `100kHz` | ~100 kHz |
| `1MHz` | ~1 MHz |

```
HYDRABUS.RAWWIRE speed 100kHz
HYDRABUS.RAWWIRE speed 1MHz
```

---

#### RAWWIRE · sda — Drive SDA/MOSI pin

```
HYDRABUS.RAWWIRE sda <0|1>
```

```
HYDRABUS.RAWWIRE sda 1   # drive SDA high
HYDRABUS.RAWWIRE sda 0   # drive SDA low
```

---

#### RAWWIRE · clk — Control clock line

```
HYDRABUS.RAWWIRE clk <0|1|tick>
```

| Argument | Description |
|---|---|
| `0` | Drive CLK permanently low |
| `1` | Drive CLK permanently high |
| `tick` | Generate one clock pulse (low→high→low) |

```
HYDRABUS.RAWWIRE clk tick   # one clock pulse
HYDRABUS.RAWWIRE clk 1      # hold clock high
HYDRABUS.RAWWIRE clk 0      # release clock low
```

---

#### RAWWIRE · bit — Send N bits from a hex byte

Clocks out N bits of a single hex byte, MSB first.

```
HYDRABUS.RAWWIRE bit <N> <HEXBYTE>
```

- `N` — number of bits to send (1–8).
- `HEXBYTE` — 1 hex byte; bits are sent MSB first.

```
# Send all 8 bits of byte 0xA5
HYDRABUS.RAWWIRE bit 8 A5

# Send only the upper 7 bits of 0xA5
HYDRABUS.RAWWIRE bit 7 A5

# Send 1 bit (MSB of 0x80 = logic 1)
HYDRABUS.RAWWIRE bit 1 80
```

---

#### RAWWIRE · ticks — Generate N bare clock pulses

Sends N clock pulses with no data on SDA.

```
HYDRABUS.RAWWIRE ticks <N>
```

- `N` — number of clock pulses (1–16).

```
HYDRABUS.RAWWIRE ticks 8
HYDRABUS.RAWWIRE ticks 16
```

---

#### RAWWIRE · write — Bulk raw-wire write (1–16 bytes)

Sends up to 16 bytes. MISO bytes captured during the transfer are printed.

```
HYDRABUS.RAWWIRE write <HEXDATA>
```

```
HYDRABUS.RAWWIRE write CA
HYDRABUS.RAWWIRE write CAFEBABE
HYDRABUS.RAWWIRE write 0102030405060708
```

---

#### RAWWIRE · read — Read N bytes

```
HYDRABUS.RAWWIRE read <N>
```

```
HYDRABUS.RAWWIRE read 4
HYDRABUS.RAWWIRE read 1
```

---

#### RAWWIRE · aux — AUX GPIO control

See [AUX GPIO Control](#aux-gpio-control-aux).

```
HYDRABUS.RAWWIRE aux 1 in
```

---

### SWD

ARM Serial Wire Debug interface. **Prerequisite: `HYDRABUS.MODE swd`**

> **Note:** Call `init` (or `multidrop` for multi-drop targets) before issuing any register read/write commands.

```
HYDRABUS.SWD <subcommand> <args>
```

---

#### SWD · init — Bus initialisation

Sends the JTAG-to-SWD switching sequence and synchronisation clocks to bring the target into SWD mode.

```
HYDRABUS.SWD init
```

---

#### SWD · multidrop — ADIv6 multi-drop activation

Sends the ADIv6 dormant-to-active sequence to select a specific DP target in a multi-drop SWD configuration.

```
HYDRABUS.SWD multidrop [addr]
```

- `addr` — optional 32-bit DP address in hex (4 bytes) or decimal. Defaults to `0`.

```
# Activate with default address
HYDRABUS.SWD multidrop

# Activate a specific DP target
HYDRABUS.SWD multidrop 01000000
```

---

#### SWD · read_dp — Read Debug Port register

```
HYDRABUS.SWD read_dp <addr>
```

- `addr` — hex byte (e.g. `00` = DPIDR, `04` = CTRL/STAT, `08` = SELECT, `0C` = RDBUFF).

```
HYDRABUS.SWD read_dp 00   # read DPIDR
HYDRABUS.SWD read_dp 04   # read CTRL/STAT
```

**Return:** `DP[0xADDR] = 0xXXXXXXXX`

---

#### SWD · write_dp — Write Debug Port register

```
HYDRABUS.SWD write_dp <addr> <value>
```

- `addr` — hex byte.
- `value` — hex 32-bit value.

```
HYDRABUS.SWD write_dp 04 50000000   # power up DP (CTRL/STAT)
HYDRABUS.SWD write_dp 08 000000F0   # select AP 0, bank 0xF
```

---

#### SWD · read_ap — Read Access Port register

```
HYDRABUS.SWD read_ap <ap_addr> <bank>
```

- `ap_addr` — AP index (hex byte, 0–255).
- `bank` — register bank (hex byte).

```
HYDRABUS.SWD read_ap 00 FC   # read AP 0 IDR (bank 0xFC)
HYDRABUS.SWD read_ap 00 04   # read AP 0 TAR
```

**Return:** `AP[idx][0xBANK] = 0xXXXXXXXX`

---

#### SWD · write_ap — Write Access Port register

```
HYDRABUS.SWD write_ap <ap_addr> <bank> <value>
```

```
HYDRABUS.SWD write_ap 00 04 23000002   # configure MEM-AP CSW
HYDRABUS.SWD write_ap 00 08 20000000   # write TAR (target address register)
```

---

#### SWD · scan — Scan all AP slots

Probes all 256 AP indices and prints those with valid (non-zero) IDR values along with their component type information.

```
HYDRABUS.SWD scan
```

---

#### SWD · abort — Write DP ABORT register

Clears sticky error bits in the DP ABORT register. Useful to recover from a failed transaction.

```
HYDRABUS.SWD abort [flags]
```

- `flags` — hex byte (default `1F` = clear all sticky bits).

```
HYDRABUS.SWD abort           # clear all sticky bits (default 0x1F)
HYDRABUS.SWD abort 04        # clear STKCMPCLR only
```

---

### SMARTCARD

ISO 7816 smartcard interface. **Prerequisite: `HYDRABUS.MODE smartcard`**

```
HYDRABUS.SMARTCARD <subcommand> <args>
```

---

#### SMARTCARD · cfg — Configure bus settings

```
HYDRABUS.SMARTCARD cfg [pullup=0|1]
```

```
HYDRABUS.SMARTCARD cfg pullup=1   # enable I/O line pull-up
HYDRABUS.SMARTCARD cfg ?          # print current cfg
```

---

#### SMARTCARD · rst — Drive reset pin

```
HYDRABUS.SMARTCARD rst <0|1>
```

```
HYDRABUS.SMARTCARD rst 0   # assert reset (cold reset)
HYDRABUS.SMARTCARD rst 1   # release reset
```

---

#### SMARTCARD · baud — Set baud rate

```
HYDRABUS.SMARTCARD baud <N>
```

```
HYDRABUS.SMARTCARD baud 9600
HYDRABUS.SMARTCARD baud 38400
```

---

#### SMARTCARD · prescaler — Set clock prescaler

Sets the clock divider (0–255) that divides the card clock frequency.

```
HYDRABUS.SMARTCARD prescaler <N>
```

```
HYDRABUS.SMARTCARD prescaler 10
```

---

#### SMARTCARD · guardtime — Set guard time

Sets the extra guard time between bytes in ETUs (0–255).

```
HYDRABUS.SMARTCARD guardtime <N>
```

```
HYDRABUS.SMARTCARD guardtime 2
```

---

#### SMARTCARD · write — Send APDU bytes

Sends an APDU or raw bytes to the card. Unlike the other protocols, there is no 16-byte limit.

```
HYDRABUS.SMARTCARD write <HEXDATA>
```

```
# SELECT FILE APDU (AID: A0000000031010)
HYDRABUS.SMARTCARD write 00A4040007A0000000031010

# GET RESPONSE
HYDRABUS.SMARTCARD write 00C0000000
```

---

#### SMARTCARD · read — Read N bytes from card

```
HYDRABUS.SMARTCARD read <N>
```

```
# Read 2-byte status word (SW1 SW2)
HYDRABUS.SMARTCARD read 2
```

---

#### SMARTCARD · atr — Retrieve Answer-To-Reset

Triggers a card reset and retrieves the ATR sequence, which identifies the card type and its communication capabilities.

```
HYDRABUS.SMARTCARD atr
```

**Return:** ATR bytes printed as hex dump.

---

#### SMARTCARD · aux — AUX GPIO control

See [AUX GPIO Control](#aux-gpio-control-aux).

```
HYDRABUS.SMARTCARD aux 0 out 1
```

---

### NFC

NFC reader interface (ISO 14443-A and ISO 15693). **Prerequisite: `HYDRABUS.MODE nfc`**

```
HYDRABUS.NFC <subcommand> <args>
```

---

#### NFC · mode — Select NFC standard

```
HYDRABUS.NFC mode <14443a|15693>
```

```
HYDRABUS.NFC mode 14443a   # ISO 14443-A (MIFARE, NFC-A tags)
HYDRABUS.NFC mode 15693    # ISO 15693 (vicinity cards)
```

---

#### NFC · rf — Control RF field

```
HYDRABUS.NFC rf <on|off>
```

```
HYDRABUS.NFC rf on    # power up the RF field
HYDRABUS.NFC rf off   # field off (card power down)
```

---

#### NFC · write — Transmit bytes

Transmits bytes to the card. An optional `crc` token at the end instructs the firmware to append the protocol CRC automatically.

```
HYDRABUS.NFC write <HEXDATA> [crc]
```

**Return:** Card response bytes printed as hex dump.

```
# Send 2 bytes, no CRC
HYDRABUS.NFC write 6000

# READ BLOCK command with CRC appended by firmware
HYDRABUS.NFC write 3000 crc
```

---

#### NFC · write_bits — Transmit a partial byte

Transmits N bits of a single byte. Used for anti-collision and REQA/WUPA commands that require fewer than 8 bits.

```
HYDRABUS.NFC write_bits <HEXBYTE> <N>
```

- `HEXBYTE` — 1 hex byte.
- `N` — number of bits to send (1–7).

**Return:** Card response bytes printed as hex dump.

```
# Send REQA (0x26, 7 bits) to request card presence
HYDRABUS.NFC write_bits 26 7

# Send WUPA (0x52, 7 bits)
HYDRABUS.NFC write_bits 52 7
```

---

#### NFC · aux — AUX GPIO control

See [AUX GPIO Control](#aux-gpio-control-aux).

```
HYDRABUS.NFC aux 0 out 1
```

---

### MMC

eMMC/MMC block device access. **Prerequisite: `HYDRABUS.MODE mmc`**

> **Note:** Block size is always 512 bytes.

```
HYDRABUS.MMC <subcommand> <args>
```

---

#### MMC · cfg — Configure bus width

```
HYDRABUS.MMC cfg [width=1|4]
```

```
HYDRABUS.MMC cfg width=4   # use 4-bit bus
HYDRABUS.MMC cfg width=1   # use 1-bit bus
HYDRABUS.MMC cfg ?         # print current cfg
```

---

#### MMC · cid — Read Card Identification register

Reads the 16-byte CID register, which contains manufacturer ID, product name, serial number, and manufacturing date.

```
HYDRABUS.MMC cid
```

---

#### MMC · csd — Read Card Specific Data register

Reads the 16-byte CSD register, which describes card capacity, timing, and feature flags.

```
HYDRABUS.MMC csd
```

---

#### MMC · ext_csd — Read Extended CSD register

Reads the 512-byte EXT_CSD register, which contains extended configuration and status information.

```
HYDRABUS.MMC ext_csd
```

---

#### MMC · read — Read a 512-byte block

```
HYDRABUS.MMC read <block_num>
```

- `block_num` — decimal block address.

```
HYDRABUS.MMC read 0      # read boot sector (block 0)
HYDRABUS.MMC read 2048   # read block 2048
```

**Return:** 512-byte block content printed as hex dump.

---

#### MMC · write — Write a 512-byte block

```
HYDRABUS.MMC write <block_num> <HEXDATA>
```

- `block_num` — decimal block address.
- `HEXDATA` — exactly 512 bytes (1024 hex characters).

```
HYDRABUS.MMC write 0 000102030405...   # write 512 bytes to block 0
```

---

#### MMC · aux — AUX GPIO control

See [AUX GPIO Control](#aux-gpio-control-aux).

```
HYDRABUS.MMC aux 0 out 1
```

---

### SDIO

SD card command interface at the raw command level. **Prerequisite: `HYDRABUS.MODE sdio`**

> **Note:** `cmd_id` is a decimal value (0–63). `cmd_arg` is a 4-byte hex value (e.g. `000001AA`). Block size is always 512 bytes.

```
HYDRABUS.SDIO <subcommand> <args>
```

---

#### SDIO · cfg — Configure bus width and frequency

```
HYDRABUS.SDIO cfg [width=1|4] [freq=slow|fast]
```

```
HYDRABUS.SDIO cfg width=4 freq=fast
HYDRABUS.SDIO cfg ?   # print current cfg
```

---

#### SDIO · send_no — Send command with no response

Sends a command and does not wait for any response.

```
HYDRABUS.SDIO send_no <cmd_id> <cmd_arg>
```

```
HYDRABUS.SDIO send_no 0 00000000   # CMD0 GO_IDLE_STATE
```

---

#### SDIO · send_short — Send command, receive 4-byte response

Sends a command and captures the 4-byte R1, R3, R6, or R7 response.

```
HYDRABUS.SDIO send_short <cmd_id> <cmd_arg>
```

**Return:** 4-byte response printed as hex dump.

```
HYDRABUS.SDIO send_short 8 000001AA   # CMD8 SEND_IF_COND (check voltage)
HYDRABUS.SDIO send_short 55 00000000  # CMD55 APP_CMD (prefix for ACMD)
```

---

#### SDIO · send_long — Send command, receive 16-byte response

Sends a command and captures the 16-byte R2 response (CID or CSD register content).

```
HYDRABUS.SDIO send_long <cmd_id> <cmd_arg>
```

**Return:** 16-byte response printed as hex dump.

```
HYDRABUS.SDIO send_long 2 00000000    # CMD2 ALL_SEND_CID
HYDRABUS.SDIO send_long 9 00010000    # CMD9 SEND_CSD for RCA 0x0001
```

---

#### SDIO · read — Send block-read command

Sends a command and captures the 512-byte data block that follows.

```
HYDRABUS.SDIO read <cmd_id> <cmd_arg>
```

**Return:** 512-byte block printed as hex dump.

```
HYDRABUS.SDIO read 17 00000000   # CMD17 READ_SINGLE_BLOCK at address 0
```

---

#### SDIO · write — Send block-write command

Sends a command followed by a 512-byte data payload.

```
HYDRABUS.SDIO write <cmd_id> <cmd_arg> <HEXDATA>
```

- `HEXDATA` — exactly 512 bytes (1024 hex characters).

```
HYDRABUS.SDIO write 24 00000000 000102...   # CMD24 WRITE_BLOCK at address 0
```

---

#### SDIO · aux — AUX GPIO control

See [AUX GPIO Control](#aux-gpio-control-aux).

```
HYDRABUS.SDIO aux 0 out 1
```

---

## Script Files

Script files are plain text files stored under `ARTEFACTS_PATH`. They are executed by `CommScriptClient`, which reads the file line by line and sends/receives bytes according to a simple scripting syntax. The `SCRIPT_DELAY` INI value inserts a delay (in ms) between commands.

The following protocol modules support a `script` sub-command:

```
HYDRABUS.SPI    script read_flash.txt
HYDRABUS.I2C    script sensor_probe.txt
HYDRABUS.UART   script at_modem.txt
```

---

## AUX GPIO Control (aux)

All protocol modules share an identical `aux` sub-command for controlling the HydraBus AUX GPIO pins (0–3). Each pin can be individually configured as input, output, or open-drain with pull-up.

```
HYDRABUS.<PROTO> aux <pin_idx> [in|out|pp] [0|1]
```

| Argument | Description |
|---|---|
| `pin_idx` | AUX pin index: 0, 1, 2, or 3 |
| `in` | Set pin as input (high-impedance) |
| `out` | Set pin as push-pull output |
| `pp` | Enable pull-up resistor |
| `0` / `1` | Drive output low / high |

```
# Set AUX0 as push-pull output and drive it high
HYDRABUS.SPI aux 0 out 1

# Set AUX1 as input (read state)
HYDRABUS.SPI aux 1 in

# Enable pull-up on AUX2
HYDRABUS.I2C aux 2 pp

# Drive AUX3 low
HYDRABUS.UART aux 3 out 0
```

The `aux` command is available on **all** protocol modules (SPI, I2C, UART, ONEWIRE, RAWWIRE, SWD, SMARTCARD, NFC, MMC, SDIO) and operates identically regardless of the active protocol.

---

## Fault-Tolerant and Privileged Modes

- **Fault-tolerant mode** (`setFaultTolerant()` / `isFaultTolerant()`): when set, the plugin continues execution even after a sub-command returns `false`. This is useful in test scripts where non-fatal errors should not abort a sequence.
- **Privileged mode** (`isPrivileged()`): always returns `false` in this plugin. Reserved for future use in the plugin framework.

---

## Error Handling and Return Values

Every handler returns `bool`:
- `true` — command executed successfully (or argument validation passed in disabled mode).
- `false` — argument validation failed, an unknown sub-command was given, the protocol returned an unexpected response, the hardware is not in the expected mode, or a file was not found.

Errors and diagnostic messages are emitted via the `LOG_PRINT` macros at various severity levels (`LOG_ERROR`, `LOG_WARNING`, `LOG_INFO`, `LOG_VERBOSE`). The host application controls log verbosity through the shared `uLogger` configuration.

If a handler is called when the plugin is not in the correct protocol mode (e.g., calling `HYDRABUS.SPI write` before `HYDRABUS.MODE spi`), the protocol accessor returns `nullptr` and the handler logs an error and returns `false` immediately.
