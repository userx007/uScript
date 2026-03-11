# BusPirate Plugin

A C++ shared-library plugin that exposes the [Bus Pirate](http://dangerousprototypes.com/docs/Bus_Pirate) multi-protocol hardware interface through a unified command dispatcher. The plugin covers all five binary-mode protocols supported by the Bus Pirate firmware: **SPI**, **I2C**, **UART**, **1-Wire**, and **Raw-Wire**, plus the low-level **Mode** switcher.

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
6. [Script Files](#script-files)
7. [Peripheral Control (per)](#peripheral-control-per)
8. [Fault-Tolerant and Privileged Modes](#fault-tolerant-and-privileged-modes)
9. [Error Handling and Return Values](#error-handling-and-return-values)
10. [Binary Protocol Quick Reference](#binary-protocol-quick-reference)

---

## Overview

The plugin loads as a dynamic shared library (`.so` / `.dll`). The host application calls the exported C entry points `pluginEntry()` / `pluginExit()` to create and destroy the plugin object. Once loaded, the host passes configuration settings (serial port, baud rate, timeouts, artefacts path…) via `setParams()`, optionally calls `doInit()` to open the UART connection, and then calls `doDispatch()` for every command it wants to execute.

All commands follow the pattern:

```
<PLUGIN>.<COMMAND> [subcommand] [arguments]
```

For example:

```
BUSPIRATE.MODE spi
BUSPIRATE.SPI speed 1MHz
BUSPIRATE.SPI wrrd 9F:3
```

---

## Project Structure

```
buspirate_plugin/
├── CMakeLists.txt                  # Build definition (shared library)
├── inc/
│   ├── buspirate_plugin.hpp        # Main class definition + command tables
│   ├── buspirate_generic.hpp       # Generic template helpers (dispatch, write, speed, script)
│   ├── bithandling.h               # BIT_SET / BIT_CLEAR macros
│   └── private/
│       ├── mode_config.hpp         # MODE_COMMANDS_CONFIG_TABLE
│       ├── spi_config.hpp          # SPI command + speed tables  (included from .cpp)
│       ├── i2c_config.hpp          # I2C command + speed tables
│       ├── uart_config.hpp         # UART command + speed tables
│       ├── onewire_config.hpp      # 1-Wire command table
│       └── rawwire_config.hpp      # Raw-Wire command + speed tables
└── src/
    ├── buspirate_plugin.cpp        # Entry points, init/cleanup, INFO, MODE, setParams
    ├── buspirate_generic.cpp       # Shared helpers: wrrd, wrrdf, peripheral, wire write
    ├── buspirate_mode.cpp          # MODE dispatcher + MODE_COMMANDS_CONFIG_TABLE
    ├── buspirate_spi.cpp           # SPI sub-commands + SPI_COMMANDS/SPEED config tables
    ├── buspirate_i2c.cpp           # I2C sub-commands + I2C_COMMANDS/SPEED config tables
    ├── buspirate_uart.cpp          # UART sub-commands + UART_COMMANDS/SPEED config tables
    ├── buspirate_onewire.cpp       # 1-Wire sub-commands + ONEWIRE_COMMANDS config table
    └── buspirate_rawwire.cpp       # Raw-Wire sub-commands + RAWWIRE_COMMANDS/SPEED config tables
```

Each protocol lives in its own `.cpp` file, which also defines its own command/speed configuration tables using X-macro patterns.

---

## Architecture

### Plugin Lifecycle

```
pluginEntry()           → creates BuspiratePlugin instance
  setParams()           → loads INI values (port, baud rate, timeouts, artefacts path...)
  doInit()              → opens the UART port to the Bus Pirate hardware
  doEnable()            → enables real execution (without this, commands validate args only)
  doDispatch(cmd, args) → routes a command string to the correct handler
  doCleanup()           → closes the UART port
pluginExit(ptr)         → deletes the BuspiratePlugin instance
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
SPI_CMD_RECORD( cs    )            \
SPI_CMD_RECORD( speed )            \
...

// In the constructor (buspirate_plugin.hpp):
#define SPI_CMD_RECORD(a) \
    m_mapCmds_SPI.insert(std::make_pair(std::string(#a), &BuspiratePlugin::m_handle_spi_##a));
SPI_COMMANDS_CONFIG_TABLE
#undef SPI_CMD_RECORD
```

Adding a new sub-command only requires adding one line to the config table and implementing the handler function—no other registration code is needed.

### Generic Template Helpers

`buspirate_generic.hpp` provides stateless template functions used by all protocol modules:

| Template function | Purpose |
|---|---|
| `generic_module_dispatch<T>()` | Splits `"subcmd args"` and routes to the module map |
| `generic_module_set_speed<T>()` | Looks up a speed label in the module speed map and sends the corresponding `0x60+n` command byte |
| `generic_write_data<T>()` | Unhexlifies a hex string and calls a protocol-specific bulk-write callback |
| `generic_execute_script<T>()` | Opens a script file from `ARTEFACTS_PATH` and runs it via `CommScriptClient` |
| `generic_module_list_commands<T>()` | Logs all registered sub-command names (used by `help`) |

The concrete `buspirate_generic.cpp` additionally implements:

- `generic_set_peripheral()` — encodes a 4-bit `wxyz` string into the `0x40|wxyz` peripheral command.
- `generic_write_read_data()` — parses `HEXDATA:rdlen` and issues the `wrrd` binary command.
- `generic_write_read_file()` — same but reads write data from a binary file and streams the result to a file.
- `generic_wire_write_data()` — sends up to 16 bytes using the `0x10|(count-1)` bulk-write command (shared by UART, 1-Wire, Raw-Wire).

### INI Configuration Keys

The following keys are read from the host configuration/INI file:

| Key | Type | Description |
|---|---|---|
| `UART_PORT` | string | Serial port path (e.g., `/dev/ttyUSB0`, `COM3`) |
| `BAUDRATE` | uint32 | Baud rate for the host↔Bus Pirate UART (typically `115200`) |
| `READ_TIMEOUT` | uint32 | Per-byte read timeout in milliseconds |
| `WRITE_TIMEOUT` | uint32 | Per-byte write timeout in milliseconds |
| `READ_BUF_SIZE` | uint32 | Receive buffer size for script execution |
| `READ_BUF_TIMEOUT`| uint32 | Buffer read timeout for script execution |
| `SCRIPT_DELAY` | uint32 | Inter-command delay in milliseconds during script execution |
| `ARTEFACTS_PATH` | string | Base directory for script and binary data files |

---

## Building

The plugin is built as a CMake shared library. It links against several utility libraries (`uSharedConfig`, `uIPlugin`, `uPluginOps`, `uICoreScript`, `uCommScriptClient`, `uUart`, `uUtils`…) that must be available in the CMake build tree.

```bash
mkdir build && cd build
cmake ..
make buspirate_plugin
```

The output is `libbuspirate_plugin.so` (Linux) or `buspirate_plugin.dll` (Windows).

---

## Command Reference

### INFO

Prints version information, the active port/baud configuration, and a complete usage summary of all supported commands directly to the logger. This command takes **no arguments** and works even if `doInit()` failed (i.e., no hardware is required).

```
BUSPIRATE.INFO
```

**Example output (abbreviated):**
```
BUSPIRATE  | Vers: 1.0.0.0
BUSPIRATE  | Description: BusPirate multi-protocol interface (SPI/I2C/UART/1-Wire/RawWire)
BUSPIRATE  |   Port: /dev/ttyUSB0  Baud: 115200
...
```

---

### MODE

Switch the Bus Pirate into a specific binary protocol mode. This **must be called before any protocol command**. Internally, the mode table maps each mode name to a raw byte value, a repetition count, and the expected firmware response string.

```
BUSPIRATE.MODE <mode>
```

#### Sub-commands

| Sub-command | Binary request | Repetitions | Expected response | Description |
|---|---|---|---|---|
| `bitbang` | `0x00` | ×20 | `BBIO1` | Enter raw bitbang mode |
| `spi` | `0x01` | ×1 | `SPI1` | Enter SPI binary mode |
| `i2c` | `0x02` | ×1 | `I2C1` | Enter I2C binary mode |
| `uart` | `0x03` | ×1 | `ART1` | Enter UART binary mode |
| `onewire` | `0x04` | ×1 | `1W01` | Enter 1-Wire binary mode |
| `rawwire` | `0x05` | ×1 | `RAW1` | Enter Raw-Wire binary mode |
| `jtag` | `0x06` | ×1 | `JTG1` | Enter JTAG mode |
| `reset` | `0x0F` | ×1 | `0x01` | Send reset command |
| `exit` | `0x00` | ×1 | `BBIO1` | Exit current mode, return to bitbang |

> **Note:** Entering `bitbang` mode requires sending `0x00` **20 times** because the Bus Pirate firmware needs to detect the transition from terminal mode.

#### Examples

```
# Enter SPI mode before any SPI commands
BUSPIRATE.MODE spi

# Enter I2C mode
BUSPIRATE.MODE i2c

# Enter 1-Wire mode
BUSPIRATE.MODE onewire

# Reset the Bus Pirate (returns to HiZ state, mode LED off)
BUSPIRATE.MODE reset

# Exit the current protocol mode and return to bitbang
BUSPIRATE.MODE exit

# Enter raw bitbang mode (sends 0x00 twenty times)
BUSPIRATE.MODE bitbang
```

---

### SPI

Full-duplex SPI bus interface. **Prerequisite: `BUSPIRATE.MODE spi`**

```
BUSPIRATE.SPI <subcommand> <args>
```

---

#### SPI · cfg — Configure SPI bus settings

Sets the output voltage, clock polarity (CKP), clock edge (CKE), and sample point (SMP). The configuration is accumulated: each call modifies only the bits corresponding to the letters present in the argument string. The default startup state is `HiZ, CKP-low, CKE-Idle→Active, SMP-middle` (`0x80`).

**Argument letters:**

| Letter | Bit | Meaning |
|---|---|---|
| `z` | bit 3 = 0 | Output HiZ (default) |
| `V` | bit 3 = 1 | Output 3.3 V |
| `l` | bit 2 = 0 | CKP: clock idle low (default) |
| `H` | bit 2 = 1 | CKP: clock idle high |
| `i` | bit 1 = 0 | CKE: data valid on Idle→Active edge (default) |
| `A` | bit 1 = 1 | CKE: data valid on Active→Idle edge |
| `m` | bit 0 = 0 | SMP: sample in the middle (default) |
| `E` | bit 0 = 1 | SMP: sample at the end |

```
# Standard SPI mode 0 with 3.3 V output
BUSPIRATE.SPI cfg Vlim

# SPI mode 1 (clock idle low, data captured on rising edge) with 3.3 V
BUSPIRATE.SPI cfg VlA

# SPI mode 3 (clock idle high, data captured on falling edge) with 3.3 V
BUSPIRATE.SPI cfg VHi

# Print the current configuration byte (hex)
BUSPIRATE.SPI cfg ?

# Reset to HiZ output (leave other bits unchanged)
BUSPIRATE.SPI cfg z
```

---

#### SPI · cs — Chip-select control

```
BUSPIRATE.SPI cs <en|dis>
```

| Argument | Binary | Effect |
|---|---|---|
| `en` | `0x02` | Assert CS → GND (logic low, chip selected) |
| `dis` | `0x03` | Deassert CS → 3.3 V / HiZ (chip deselected) |

```
# Select the target chip
BUSPIRATE.SPI cs en

# Deselect the target chip
BUSPIRATE.SPI cs dis
```

---

#### SPI · speed — Set clock frequency

```
BUSPIRATE.SPI speed <frequency>
```

Available presets (sent as `0x60 | index`):

| Label | Index | Frequency |
|---|---|---|
| `30kHz` | 0 | 30 kHz (default) |
| `125kHz` | 1 | 125 kHz |
| `250kHz` | 2 | 250 kHz |
| `1MHz` | 3 | 1 MHz |
| `2MHz` | 4 | 2 MHz |
| `2.6MHz` | 5 | 2.6 MHz |
| `4MHz` | 6 | 4 MHz |
| `8MHz` | 7 | 8 MHz |

```
BUSPIRATE.SPI speed 1MHz
BUSPIRATE.SPI speed 8MHz
BUSPIRATE.SPI speed 250kHz
```

---

#### SPI · per — Peripheral control

See [Peripheral Control](#peripheral-control-per).

```
BUSPIRATE.SPI per 1100   # power on + pull-ups on, AUX off, CS off
BUSPIRATE.SPI per 1000   # power on only
BUSPIRATE.SPI per 0000   # all off
```

---

#### SPI · write — Bulk SPI transfer (1–16 bytes)

Asserts CS, sends the given bytes on MOSI while clocking MISO, then deasserts CS. The received MISO bytes are printed as a hex dump.

```
BUSPIRATE.SPI write <HEXDATA>
```

- `HEXDATA` is a hex string, 1–16 bytes (2–32 hex characters).
- Internally sends chunks of up to 6 bytes using the `0x10|(count-1)` bulk SPI command.

```
# Send a single byte (e.g., read JEDEC ID command on a flash)
BUSPIRATE.SPI write 9F

# Send four bytes
BUSPIRATE.SPI write DEADBEEF

# Send a 5-byte erase command
BUSPIRATE.SPI write 06C7000000

# Write a 16-byte block (maximum)
BUSPIRATE.SPI write 000102030405060708090A0B0C0D0E0F
```

---

#### SPI · read — Receive bytes (1–16)

Asserts CS, clocks out `0x00` (dummy bytes) on MOSI to generate the clock, and captures MISO. The received bytes are printed as a hex dump.

```
BUSPIRATE.SPI read <N>
```

```
# Read 4 bytes
BUSPIRATE.SPI read 4

# Read 1 byte
BUSPIRATE.SPI read 1

# Read the maximum 16 bytes
BUSPIRATE.SPI read 16
```

---

#### SPI · wrrd — Write then read (0–4096 bytes each)

Sends a complete write-then-read transaction in a single CS assertion. The Bus Pirate buffers all write bytes, performs the transfer at full SPI speed, then returns the read bytes — meeting tight timing requirements of flash memories.

```
BUSPIRATE.SPI wrrd <HEXDATA>:<rdlen>
```

- `HEXDATA` — hex string for the write phase (0–4096 bytes).
- `rdlen` — decimal number of bytes to read back (0–4096).

```
# JEDEC ID: send 0x9F, read 3 bytes (manufacturer, type, capacity)
BUSPIRATE.SPI wrrd 9F:3

# Status register read (0x05)
BUSPIRATE.SPI wrrd 05:1

# Read 256 bytes starting at address 0x000000 (READ command 0x03)
BUSPIRATE.SPI wrrd 03000000:256

# Write Enable (0x06) — write only, no read
BUSPIRATE.SPI wrrd 06:0

# Read 4 bytes from a 3-byte addressed device (fast-read 0x0B with dummy)
BUSPIRATE.SPI wrrd 0B00000000:4
```

---

#### SPI · wrrdf — Write/read using binary files

Same as `wrrd` but the write data is loaded from a binary file and the read data is saved to a file, both resolved under `ARTEFACTS_PATH`.

```
BUSPIRATE.SPI wrrdf <filename>[:<wrchunk>][:<rdchunk>]
```

```
# Send the contents of flash_cmd.bin and save the response
BUSPIRATE.SPI wrrdf flash_cmd.bin

# With explicit chunk sizes for large files
BUSPIRATE.SPI wrrdf program_page.bin:256:0
```

---

#### SPI · sniff — Hardware SPI sniffer

Puts the Bus Pirate into passive sniffer mode. The hardware SPI sniffer works up to ~10 MHz and follows the current CKP/CKE configuration. Send any byte to exit; if data overflows the buffer, the MODE LED turns off.

```
BUSPIRATE.SPI sniff <all|cslo>
```

| Argument | Binary | Behavior |
|---|---|---|
| `all` | `0x0D` | Sniff all traffic regardless of CS state |
| `cslo` | `0x0E` | Sniff only when CS is asserted low |

```
# Sniff all traffic on the bus
BUSPIRATE.SPI sniff all

# Sniff only transactions with CS asserted
BUSPIRATE.SPI sniff cslo
```

---

#### SPI · script — Run a command script

Executes a text script from `ARTEFACTS_PATH` via the `CommScriptClient` engine. Each line of the script is a raw send/receive/expect instruction.

```
BUSPIRATE.SPI script <filename>
```

```
BUSPIRATE.SPI script read_flash.txt
BUSPIRATE.SPI script erase_chip.txt
BUSPIRATE.SPI script program_sector.txt
```

---

### I2C

I2C bus master. **Prerequisite: `BUSPIRATE.MODE i2c`**

```
BUSPIRATE.I2C <subcommand> <args>
```

---

#### I2C · speed — Set clock speed

```
BUSPIRATE.I2C speed <frequency>
```

| Label | Index | Frequency |
|---|---|---|
| `5KHz` | 0 | ~5 kHz |
| `50kHz` | 1 | ~50 kHz |
| `100kHz` | 2 | ~100 kHz (standard) |
| `400kHz` | 3 | ~400 kHz (fast mode) |

```
BUSPIRATE.I2C speed 100kHz
BUSPIRATE.I2C speed 400kHz
BUSPIRATE.I2C speed 5KHz
```

---

#### I2C · bit — Send bus control sequences

Sends individual I2C framing signals. Use this for manual transaction construction when `wrrd` is not sufficient.

```
BUSPIRATE.I2C bit <start|stop|ack|nack>
```

| Argument | Binary | Description |
|---|---|---|
| `start` | `0x02` | Assert START condition (SDA high→low while SCL high) |
| `stop` | `0x03` | Assert STOP condition (SDA low→high while SCL high) |
| `ack` | `0x06` | Clock out ACK bit (SDA low) |
| `nack` | `0x07` | Clock out NACK bit (SDA high) |

```
# Manually build a read transaction for a device at 0x50
BUSPIRATE.I2C bit start
BUSPIRATE.I2C write A1         # address 0x50 with R/W=1
BUSPIRATE.I2C read 1
BUSPIRATE.I2C bit stop

# Send a NACK before STOP to end a multi-byte read
BUSPIRATE.I2C bit nack
BUSPIRATE.I2C bit stop
```

---

#### I2C · write — Bulk I2C write (1–16 bytes)

Sends up to 16 bytes in a single `0x10|(count-1)` bulk I2C write command. ACK/NACK status is reported for each byte.

```
BUSPIRATE.I2C write <HEXDATA>
```

```
# Write device address 0xA0 (write direction) + register 0x10
BUSPIRATE.I2C write A010

# Write 4 bytes of data to a register
BUSPIRATE.I2C write A0103A7F

# Write-only transaction to set a configuration register
BUSPIRATE.I2C write 4800FF
```

---

#### I2C · read — Read N bytes

Reads N bytes using individual `0x04` (read byte) commands, sending ACK after each byte except the last (which receives NACK), then sends STOP.

```
BUSPIRATE.I2C read <N>
```

```
# Read 2 bytes (e.g., a 16-bit sensor register)
BUSPIRATE.I2C read 2

# Read 8 bytes
BUSPIRATE.I2C read 8

# Read 1 byte
BUSPIRATE.I2C read 1
```

---

#### I2C · wrrd — Write then read (0–4096 bytes each)

Uses the Bus Pirate's dedicated `0x08` (I2C write-then-read) command for atomic transactions. The Bus Pirate generates a START, sends all write bytes, generates a repeated START (implicit), reads all bytes with ACK, sends NACK on the last byte, then STOP.

```
BUSPIRATE.I2C wrrd <HEXDATA>:<rdlen>
```

```
# Write device address + register, read 2 bytes
# Device 0x48 (TMP102 temperature sensor), register 0x00
BUSPIRATE.I2C wrrd 9000:2

# Read 4 bytes from EEPROM at address 0x50, register 0x10
BUSPIRATE.I2C wrrd A010:4

# Write address + 2 data bytes, read 1 byte status
BUSPIRATE.I2C wrrd 48003A7F:1

# Read only (no write bytes)
BUSPIRATE.I2C wrrd :2

# Write only (no read bytes)
BUSPIRATE.I2C wrrd A010FF:0
```

---

#### I2C · wrrdf — Write/read using binary files

```
BUSPIRATE.I2C wrrdf <filename>[:<wrchunk>][:<rdchunk>]
```

```
BUSPIRATE.I2C wrrdf i2c_sequence.bin
BUSPIRATE.I2C wrrdf eeprom_write.bin:16:0
```

---

#### I2C · sniff — Bus traffic sniffer

Passively monitors the I2C bus. Sniffed data is encoded as follows: `[` = START, `]` = STOP, `\XX` = data byte 0xXX, `+` = ACK, `-` = NACK.

```
BUSPIRATE.I2C sniff <on|off>
```

```
# Start sniffing
BUSPIRATE.I2C sniff on

# Stop sniffing (sends 0xFF, waits for 0x01 response)
BUSPIRATE.I2C sniff off
```

---

#### I2C · aux — Extended AUX / CS pin control

Provides fine-grained control over the AUX and CS pins using the `0x09` extended command.

```
BUSPIRATE.I2C aux <arg>
```

| Argument | Sub-byte | Description |
|---|---|---|
| `acl` | `0x00` | Drive AUX/CS to GND (low) |
| `ach` | `0x01` | Drive AUX/CS to 3.3 V (high) |
| `acz` | `0x02` | Set AUX/CS to high-impedance (HiZ) |
| `ra` | `0x03` | Read the current AUX pin state |
| `ua` | `0x10` | Route subsequent operations to the AUX pin |
| `uc` | `0x20` | Route subsequent operations to the CS pin |

```
# Assert AUX low (e.g., hardware reset)
BUSPIRATE.I2C aux acl

# Release AUX high
BUSPIRATE.I2C aux ach

# Read the state of the AUX pin
BUSPIRATE.I2C aux ra

# Configure to use the CS pin for subsequent peripheral commands
BUSPIRATE.I2C aux uc
```

---

#### I2C · per — Peripheral control

See [Peripheral Control](#peripheral-control-per).

```
BUSPIRATE.I2C per 1100   # power + pull-ups on
BUSPIRATE.I2C per 0000   # all off
```

---

#### I2C · mode — Query firmware mode string

Sends command `0x01` to the Bus Pirate and reads the response (`I2C1`). Useful to verify that the device is in I2C mode.

```
BUSPIRATE.I2C mode
```

---

#### I2C · script — Run a command script

```
BUSPIRATE.I2C script <filename>
```

```
BUSPIRATE.I2C script eeprom_test.txt
BUSPIRATE.I2C script sensor_init.txt
```

---

### UART

UART serial interface. **Prerequisite: `BUSPIRATE.MODE uart`**

```
BUSPIRATE.UART <subcommand> <args>
```

---

#### UART · speed — Set baud rate from preset

```
BUSPIRATE.UART speed <baud>
```

Available presets (sent as `0x60 | index`):

| Label | Index |
|---|---|
| `300` | 0 |
| `1200` | 1 |
| `2400` | 2 |
| `4800` | 3 |
| `9600` | 4 |
| `19200` | 5 |
| `31250` | 6 (MIDI) |
| `38400` | 7 |
| `57600` | 8 |
| `115200` | 10 |

```
BUSPIRATE.UART speed 9600
BUSPIRATE.UART speed 115200
BUSPIRATE.UART speed 31250   # MIDI
```

---

#### UART · bdr — Custom baud rate via BRG register

Configures the Bus Pirate UART using a raw 16-bit BRG register value (PIC24 UART peripheral). Sends command `0x07` followed by the high byte then the low byte.

```
BUSPIRATE.UART bdr <BRG>
```

Formula: `Baud = Fosc / (4 × (BRG + 1))`, where `Fosc = 32 MHz` and `BRGH = 1`.

| Target Baud | BRG value (hex) |
|---|---|
| 9600 | `0x0340` |
| 19200 | `0x019F` |
| 38400 | `0x00CF` |
| 57600 | `0x008A` |
| 115200 | `0x0044` |

```
# Set to 9600 baud via BRG
BUSPIRATE.UART bdr 0x0340

# Set to 57600 baud
BUSPIRATE.UART bdr 0x008A

# Decimal value also accepted
BUSPIRATE.UART bdr 832
```

---

#### UART · cfg — Configure frame format

```
BUSPIRATE.UART cfg <options>
```

The argument is a combination of tokens. The configuration byte layout is `100wxxyz`:

| Token | Bit | Meaning |
|---|---|---|
| `z` | bit 4 = 0 | Output HiZ (default) |
| `V` | bit 4 = 1 | Output 3.3 V |
| `8N` | bits 3:2 = `00` | 8 data bits, no parity (default) |
| `8E` | bits 3:2 = `01` | 8 data bits, even parity |
| `8O` | bits 3:2 = `10` | 8 data bits, odd parity |
| `9N` | bits 3:2 = `11` | 9 data bits, no parity |
| `1` | bit 1 = 0 | 1 stop bit (default) |
| `2` | bit 1 = 1 | 2 stop bits |
| `n` | bit 0 = 0 | Normal polarity: idle = 1 (default) |
| `i` | bit 0 = 1 | Inverted polarity: idle = 0 |

```
# Standard 8N1 at 3.3 V (most common)
BUSPIRATE.UART cfg V8N1n

# 8E2 inverted with HiZ output
BUSPIRATE.UART cfg z8E2i

# 9-bit frames, 1 stop bit, 3.3 V output, normal polarity
BUSPIRATE.UART cfg V9N1n

# Print the current configuration byte
BUSPIRATE.UART cfg ?
```

---

#### UART · echo — Enable/disable RX echo to USB

In UART binary mode the RX line is always active. This command controls whether received bytes are forwarded to the USB/host side. Echo is **off** by default on mode entry.

```
BUSPIRATE.UART echo <start|stop>
```

| Argument | Binary | Effect |
|---|---|---|
| `start` | `0x02` | Forward incoming UART bytes to the host (clears buffer-overrun flag) |
| `stop` | `0x03` | Stop forwarding |

```
# Enable RX echo to monitor incoming data
BUSPIRATE.UART echo start

# Disable echo (default state)
BUSPIRATE.UART echo stop
```

---

#### UART · mode — Transparent bridge mode

Enters a transparent UART bridge. The Bus Pirate forwards all data bidirectionally between USB and the UART pins. **Exit by physically unplugging the Bus Pirate.**

```
BUSPIRATE.UART mode bridge
```

```
BUSPIRATE.UART mode bridge
```

---

#### UART · write — Bulk UART transmit (1–16 bytes)

Transmits up to 16 bytes using the `0x10|(count-1)` bulk write command.

```
BUSPIRATE.UART write <HEXDATA>
```

```
# Send the ASCII string "HELLO"
BUSPIRATE.UART write 48454C4C4F

# Send a single byte (e.g., carriage return)
BUSPIRATE.UART write 0D

# Send an AT command: "AT\r\n"
BUSPIRATE.UART write 41540D0A

# Send 4 bytes
BUSPIRATE.UART write CAFEBABE
```

---

#### UART · per — Peripheral control

See [Peripheral Control](#peripheral-control-per).

```
BUSPIRATE.UART per 1000   # power supply on only
```

---

#### UART · script — Run a command script

```
BUSPIRATE.UART script <filename>
```

```
BUSPIRATE.UART script modem_init.txt
BUSPIRATE.UART script at_commands.txt
```

---

### ONEWIRE

Dallas/Maxim 1-Wire bus master. **Prerequisite: `BUSPIRATE.MODE onewire`**

```
BUSPIRATE.ONEWIRE <subcommand> <args>
```

---

#### ONEWIRE · reset — Send 1-Wire reset pulse

Sends a 480 µs reset pulse and detects whether any device asserts a presence pulse in response.

```
BUSPIRATE.ONEWIRE reset
```

```
# Reset the bus before every 1-Wire transaction
BUSPIRATE.ONEWIRE reset
```

---

#### ONEWIRE · write — Bulk 1-Wire write (1–16 bytes)

Sends up to 16 bytes using the `0x10|(count-1)` bulk wire-write command.

```
BUSPIRATE.ONEWIRE write <HEXDATA>
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
BUSPIRATE.ONEWIRE write CC44

# Skip ROM + Read Scratchpad (9 bytes follow via read)
BUSPIRATE.ONEWIRE write CCBE

# Select a specific device by 64-bit address (MATCH ROM + 8-byte address)
BUSPIRATE.ONEWIRE write 5528000000AABBCC
```

---

#### ONEWIRE · read — Read N bytes

Reads N bytes from the bus, one at a time (each sends `0x04` read-byte command).

```
BUSPIRATE.ONEWIRE read <N>
```

```
# Read the 9-byte DS18B20 scratchpad
BUSPIRATE.ONEWIRE read 9

# Read the 8-byte ROM address of a device
BUSPIRATE.ONEWIRE read 8

# Read 1 byte
BUSPIRATE.ONEWIRE read 1
```

---

#### ONEWIRE · search — ROM / ALARM search macro

Executes a full 1-Wire search algorithm. The Bus Pirate returns `0x01` followed by each 8-byte device address found. The sequence terminates with 8×`0xFF`.

```
BUSPIRATE.ONEWIRE search <rom|alarm>
```

| Argument | Binary cmd | 1-Wire code | Description |
|---|---|---|---|
| `rom` | `0x08` | `0xF0` | Find all devices on the bus |
| `alarm` | `0x09` | `0xEC` | Find devices with alarm flag set |

```
# Enumerate all 1-Wire devices
BUSPIRATE.ONEWIRE search rom

# Find DS18B20 sensors that triggered a temperature alarm
BUSPIRATE.ONEWIRE search alarm
```

---

#### ONEWIRE · cfg — Configure peripherals

Enables or disables the onboard power supply, pull-up resistors, AUX pin, and CS pin. Lowercase = disable, uppercase = enable.

```
BUSPIRATE.ONEWIRE cfg <letters>
```

| Letter | Bit | Meaning |
|---|---|---|
| `w` / `W` | bit 3 | Power supply off / on |
| `p` / `P` | bit 2 | Pull-up resistors off / on |
| `a` / `A` | bit 1 | AUX pin low / high |
| `c` / `C` | bit 0 | CS pin off / on |

```
# Enable power supply and pull-up resistors (mandatory for parasitic-power DS18B20)
BUSPIRATE.ONEWIRE cfg WP

# Enable power supply only
BUSPIRATE.ONEWIRE cfg W

# Disable all
BUSPIRATE.ONEWIRE cfg wpac

# Print current configuration byte
BUSPIRATE.ONEWIRE cfg ?
```

---

#### ONEWIRE · script — Run a command script

```
BUSPIRATE.ONEWIRE script <filename>
```

```
BUSPIRATE.ONEWIRE script ds18b20_read.txt
BUSPIRATE.ONEWIRE script rom_scan.txt
```

---

### RAWWIRE

Bit-bang 2-wire or 3-wire interface. Useful for proprietary serial protocols or manual PIC ICSP programming. **Prerequisite: `BUSPIRATE.MODE rawwire`**

```
BUSPIRATE.RAWWIRE <subcommand> <args>
```

---

#### RAWWIRE · cfg — Configure bus settings

```
BUSPIRATE.RAWWIRE cfg <options>
```

The configuration byte layout is `1000wxyz`:

| Token | Bit | Meaning |
|---|---|---|
| `Z` | bit 3 = 0 | Output HiZ (default) |
| `V` | bit 3 = 1 | Output 3.3 V |
| `2` | bit 2 = 0 | 2-wire mode: shared MOSI/MISO pin (default) |
| `3` | bit 2 = 1 | 3-wire mode: separate MOSI and MISO pins |
| `M` | bit 1 = 0 | MSB first (default) |
| `L` | bit 1 = 1 | LSB first |

```
# 3.3 V, 2-wire, MSB first
BUSPIRATE.RAWWIRE cfg V2M

# 3.3 V, 3-wire, LSB first (e.g., some custom serial EEPROMs)
BUSPIRATE.RAWWIRE cfg V3L

# HiZ output, 2-wire, MSB first (passive probe mode)
BUSPIRATE.RAWWIRE cfg Z2M

# Print the current configuration byte
BUSPIRATE.RAWWIRE cfg ?
```

---

#### RAWWIRE · speed — Set bit-bang clock speed

```
BUSPIRATE.RAWWIRE speed <frequency>
```

| Label | Frequency |
|---|---|
| `5KHz` | ~5 kHz |
| `50kHz` | ~50 kHz |
| `100kHz` | ~100 kHz |
| `400kHz` | ~400 kHz |

```
BUSPIRATE.RAWWIRE speed 100kHz
BUSPIRATE.RAWWIRE speed 400kHz
```

---

#### RAWWIRE · cs — Drive chip-select pin

```
BUSPIRATE.RAWWIRE cs <low|high>
```

| Argument | Binary | Effect |
|---|---|---|
| `low` | `0x04` | CS → GND |
| `high` | `0x05` | CS → 3.3 V / HiZ |

```
BUSPIRATE.RAWWIRE cs low
BUSPIRATE.RAWWIRE cs high
```

---

#### RAWWIRE · clock — Control clock line

```
BUSPIRATE.RAWWIRE clock <tick|lo|hi|N>
```

| Argument | Binary | Description |
|---|---|---|
| `tick` | `0x09` | One clock pulse (low→high→low) |
| `lo` | `0x0A` | Set clock line permanently low |
| `hi` | `0x0B` | Set clock line permanently high |
| `1`–`16` | `0x20\|(N-1)` | Send N bulk clock ticks |

```
# One clock pulse
BUSPIRATE.RAWWIRE clock tick

# Hold clock high
BUSPIRATE.RAWWIRE clock hi

# Release clock low
BUSPIRATE.RAWWIRE clock lo

# Send 8 clock ticks (useful after ICSP mode entry)
BUSPIRATE.RAWWIRE clock 8

# Send the maximum 16 clock ticks
BUSPIRATE.RAWWIRE clock 16
```

---

#### RAWWIRE · data — Drive data (MOSI/SDA) pin

```
BUSPIRATE.RAWWIRE data <low|high>
```

| Argument | Binary | Effect |
|---|---|---|
| `low` | `0x0C` | Drive data pin to GND |
| `high` | `0x0D` | Drive data pin to 3.3 V / HiZ |

```
BUSPIRATE.RAWWIRE data low
BUSPIRATE.RAWWIRE data high
```

---

#### RAWWIRE · bit — Send I2C-style start/stop bit, or bulk bits

```
BUSPIRATE.RAWWIRE bit <start|stop|0kXY>
```

| Argument | Binary | Description |
|---|---|---|
| `start` | `0x02` | I2C-style START condition (SDA high→low while CLK high) |
| `stop` | `0x03` | I2C-style STOP condition (SDA low→high while CLK high) |
| `0kXY` | `0x30\|k, XY` | Send `k+1` bits (1–8) of byte `0xXY`, MSB first |

For the bulk bits format `0kXY`: `k` is the number of bits **minus one** (0–7), and `XY` is the byte value in hex.

```
# Send I2C START
BUSPIRATE.RAWWIRE bit start

# Send I2C STOP
BUSPIRATE.RAWWIRE bit stop

# Send all 8 bits of byte 0xA5 (k=0x07 means 8 bits)
BUSPIRATE.RAWWIRE bit 07A5

# Send only the top 4 bits of 0xA0 (k=0x03 means 4 bits)
BUSPIRATE.RAWWIRE bit 03A0

# Send 1 bit (the MSB of 0x80 = bit value 1)
BUSPIRATE.RAWWIRE bit 0080
```

---

#### RAWWIRE · read — Read bit, byte, or pin state

```
BUSPIRATE.RAWWIRE read <bit|byte|dpin>
```

| Argument | Binary | Description |
|---|---|---|
| `bit` | `0x07` | Read a single bit from the bus |
| `byte` | `0x06` | Read one full byte (outputs 0xFF on MOSI in 3-wire mode) |
| `dpin` | `0x08` | Read the state of the data input pin without clocking |

```
# Read one byte
BUSPIRATE.RAWWIRE read byte

# Read a single bit (e.g., ACK check)
BUSPIRATE.RAWWIRE read bit

# Sample the data pin state without issuing a clock
BUSPIRATE.RAWWIRE read dpin
```

---

#### RAWWIRE · write — Bulk raw-wire write (1–16 bytes)

```
BUSPIRATE.RAWWIRE write <HEXDATA>
```

```
BUSPIRATE.RAWWIRE write CA
BUSPIRATE.RAWWIRE write CAFEBABE
BUSPIRATE.RAWWIRE write 0102030405060708
```

---

#### RAWWIRE · per — Peripheral control

See [Peripheral Control](#peripheral-control-per).

```
BUSPIRATE.RAWWIRE per 1100
```

---

#### RAWWIRE · pic — PIC ICSP programming extension

An extension for in-circuit serial programming (ICSP) of PIC microcontrollers. **2-wire mode only.**

```
BUSPIRATE.RAWWIRE pic <read|write>:<HEXPAYLOAD>
```

**Read** (`0xA5`): sends the 6-bit ICSP command and reads 1 byte back. Payload is 1 byte: `00YYYYYY`.

**Write** (`0xA4`): sends the 6-bit ICSP command plus a 16-bit instruction. Payload is 3 bytes: `XXYYYYYY` (cmd + delay) followed by the 16-bit instruction word.

- `XX` — delay in ms to hold PGC high on the last command bit (for page-write operations).
- `YYYYYY` — 4-bit or 6-bit ICSP command, entered as `00YYYY` for 4-bit commands.

```
# Read using 4-bit ICSP command 0x04 (load config on PIC16)
BUSPIRATE.RAWWIRE pic read:04

# Write ICSP command 0x04 with instruction 0x8000, no delay
BUSPIRATE.RAWWIRE pic write:048000

# Write ICSP command 0x08 (begin externally timed programming) with a 10ms delay
BUSPIRATE.RAWWIRE pic write:0A083FFF

# Read command 0x06 (increment address)
BUSPIRATE.RAWWIRE pic read:06
```

---

#### RAWWIRE · script — Run a command script

```
BUSPIRATE.RAWWIRE script <filename>
```

```
BUSPIRATE.RAWWIRE script pic_erase.txt
BUSPIRATE.RAWWIRE script prog_sequence.txt
```

---

## Script Files

Script files are plain text files stored under `ARTEFACTS_PATH`. They are executed by `CommScriptClient`, which reads the file line by line and sends/receives bytes according to a simple scripting syntax. The `SCRIPT_DELAY` INI value inserts a delay (in ms) between commands.

Each protocol module supports a `script` sub-command:

```
BUSPIRATE.SPI script my_test.txt
BUSPIRATE.I2C script sensor_probe.txt
BUSPIRATE.UART script at_modem.txt
BUSPIRATE.ONEWIRE script ds18b20_full_read.txt
BUSPIRATE.RAWWIRE script pic_program.txt
```

---

## Peripheral Control (per)

All five protocol modules share an identical peripheral control command. It sets four hardware signals using a 4-character binary string `wxyz` (`0` = off, `1` = on):

```
BUSPIRATE.<PROTO> per <wxyz>
```

The command is encoded as `0x40 | wxyz`:

| Bit position | Character | Pin | Function |
|---|---|---|---|
| bit 3 (w) | `w` / `W` | VPULLUP/VCC | Onboard 3.3 V / 5 V power supply |
| bit 2 (x) | `x` / `X` | pull-ups | Onboard I²C pull-up resistors |
| bit 1 (y) | `y` / `Y` | AUX | General-purpose auxiliary output |
| bit 0 (z) | `z` / `Z` | CS | Chip-select output |

```
# All off
BUSPIRATE.SPI per 0000

# Power supply on only
BUSPIRATE.SPI per 1000

# Power supply + pull-ups on (typical I2C setup)
BUSPIRATE.I2C per 1100

# Power supply + pull-ups + AUX on
BUSPIRATE.I2C per 1110

# CS asserted (low)
BUSPIRATE.SPI per 0001

# All on
BUSPIRATE.SPI per 1111
```

---

## Fault-Tolerant and Privileged Modes

- **Fault-tolerant mode** (`setFaultTolerant()` / `isFaultTolerant()`): when set, the plugin continues execution even after a sub-command returns `false`. This is useful in test scripts where non-fatal errors should not abort a sequence.
- **Privileged mode** (`isPrivileged()`): always returns `false` in this plugin. Reserved for future use in the plugin framework.

---

## Error Handling and Return Values

Every handler returns `bool`:
- `true` — command executed successfully (or argument validation passed in disabled mode).
- `false` — argument validation failed, an unknown sub-command was given, the UART send/receive returned an unexpected response, or a file was not found.

Errors and diagnostic messages are emitted via the `LOG_PRINT` macros at various severity levels (`LOG_ERROR`, `LOG_WARNING`, `LOG_DEBUG`, `LOG_VERBOSE`). The host application controls log verbosity through the shared `uLogger` configuration.

---

## Binary Protocol Quick Reference

All communication with the Bus Pirate uses its binary (bitbang) protocol over a serial UART at the configured baud rate. The general principle is: send a command byte, receive `0x01` as ACK.

| Command byte | Description |
|---|---|
| `0x00` (×20) | Enter bitbang mode |
| `0x01` | Query current mode version string |
| `0x0F` | Reset |
| `0x10\|N` | Bulk write N+1 bytes (shared: SPI/I2C/UART/1-Wire/Raw-Wire) |
| `0x40\|wxyz` | Configure peripherals |
| `0x60\|idx` | Set speed |
| `0x80\|bits` | Configure SPI / UART / Raw-Wire |
| `0x04` (SPI) | Write then read (up to 4096 bytes each) |
| `0x08` (I2C) | I2C write then read |
| `0x09` (I2C) | Extended AUX command |
| `0x02\|N` | SPI: CS low (N=0) / CS high (N=1) |
| `0x02` (I2C/RW) | START / data low |
| `0x03` (I2C) | STOP |
| `0x04` (1-Wire) | Read byte |
| `0x08` (1-Wire) | ROM search macro |
| `0x09` (1-Wire) | ALARM search macro |
| `0x20\|N` | Raw-Wire: N+1 bulk clock ticks |
| `0x30\|N`, byte | Raw-Wire: send N+1 bits of byte |
| `0xA4\|payload` | Raw-Wire PIC write (ICSP) |
| `0xA5\|payload` | Raw-Wire PIC read (ICSP) |

For the full protocol specification, see the official Bus Pirate documentation:
- SPI: http://dangerousprototypes.com/docs/SPI_(binary)
- I2C: http://dangerousprototypes.com/docs/I2C_(binary)
- UART: http://dangerousprototypes.com/docs/UART_(binary)
- 1-Wire: http://dangerousprototypes.com/docs/1-Wire_(binary)
- Raw-Wire: http://dangerousprototypes.com/docs/Raw-wire_(binary)
- Bitbang: http://dangerousprototypes.com/docs/Bitbang
