# CP2112 Plugin

A C++ shared-library plugin that exposes the [Silicon Labs CP2112](https://www.silabs.com/interface/usb-bridges/classic/device.cp2112?tab=specs) USB-HID-to-I²C/SMBus bridge as two independent command modules — **I2C** and **GPIO** — through the same string-command dispatch architecture used by the CH347 and BusPirate plugins.

The CP2112 is a single USB-HID device (VID `0x10C4` / PID `0xEA90`) that provides:
- One I²C/SMBus master port (up to 400 kHz standard; any raw Hz value is accepted)
- Eight general-purpose GPIO pins (GPIO.0–GPIO.7) with configurable direction, push-pull/open-drain drive, and optional special functions (TX/RX LEDs, interrupt output, clock output)

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
   - [HID Transport and Chunking](#hid-transport-and-chunking)
   - [INI Configuration Keys](#ini-configuration-keys)
4. [Building](#building)
5. [Platform Notes](#platform-notes)
6. [Command Reference](#command-reference)
   - [INFO](#info)
   - [I2C](#i2c)
   - [GPIO](#gpio)
7. [I2C Speed Reference](#i2c-speed-reference)
8. [GPIO Special-Function Pin Reference](#gpio-special-function-pin-reference)
9. [Script Files](#script-files)
10. [Fault-Tolerant and Dry-Run Modes](#fault-tolerant-and-dry-run-modes)
11. [Error Handling and Return Values](#error-handling-and-return-values)

---

## Overview

The plugin loads as a dynamic shared library (`.so` / `.dll`). The host calls `pluginEntry()` / `pluginExit()` to manage the object lifetime. Settings are pushed via `setParams()`, hardware is initialized with `doInit()`, execution is enabled with `doEnable()`, and commands are dispatched with `doDispatch()`.

All commands follow the pattern:

```
<PLUGIN>.<COMMAND> [subcommand] [arguments]
```

For example:

```
CP2112.I2C open addr=0x50 clock=400000
CP2112.I2C scan
CP2112.I2C wrrd 0000:2
CP2112.GPIO open dir=0xFF pp=0xFF
CP2112.GPIO set 0x01
CP2112.GPIO read
```

**Important differences from CH347 and BusPirate:**

- The CP2112 has only **two** modules: `I2C` and `GPIO`. There is no SPI or JTAG.
- The I2C `open` targets a specific 7-bit **slave address**. To talk to a different device, call `open` again with the new address (or use `scan` to discover addresses first).
- `scan` does **not** require the I2C port to be open first — it opens a temporary handle per address internally.
- `cfg` changes to I2C take effect on the **next `open`**, not immediately. `cfg` changes to GPIO take effect **immediately** if the interface is already open.
- The I2C maximum read length per transaction is **512 bytes** (enforced by the CP2112 driver). The `wrrdf` helper hard-caps `rdchunk` at 512 to prevent driver errors.
- There is no `toggle` sub-command for GPIO (use `write VALUE MASK` instead).

---

## Project Structure

```
cp2112_plugin/
├── CMakeLists.txt                  # Build definition (shared library, C++20)
├── inc/
│   ├── cp2112_plugin.hpp           # Main class + command tables + pending config structs
│   ├── cp2112_generic.hpp          # Generic template helpers + CP2112-specific size constants
│   └── private/
│       ├── i2c_config.hpp          # I2C_COMMANDS_CONFIG_TABLE + I2C_SPEED_CONFIG_TABLE
│       └── gpio_config.hpp         # GPIO_COMMANDS_CONFIG_TABLE + special-pin documentation
└── src/
    ├── cp2112_plugin.cpp           # Entry points, init/cleanup, INFO, setParams, setModuleSpeed
    ├── cp2112_i2c.cpp              # I2C sub-command implementations
    └── cp2112_gpio.cpp             # GPIO sub-command implementations + parseGpioKv helper
```

Each module is implemented in its own `.cpp` file. Command tables are driven entirely by X-macros in the `*_config.hpp` headers, consistent with the rest of the plugin family.

---

## Architecture

### Plugin Lifecycle

```
pluginEntry()               → creates CP2112Plugin instance
  setParams()               → loads INI values (device index, I2C clock, address, timeouts…)
  doInit()                  → seeds I2cPendingCfg and GpioPendingCfg from INI values; marks initialized
  doEnable()                → enables real execution (without this, commands only validate args)
  doDispatch(cmd, args)     → routes to I2C or GPIO handler
  CP2112.I2C open ...       → opens CP2112 I2C HID handle for a specific slave address
  CP2112.GPIO open ...      → opens CP2112Gpio HID handle and pushes initial pin config
  CP2112.I2C close          → releases I2C HID handle
  CP2112.GPIO close         → releases GPIO HID handle
  doCleanup()               → closes both handles, resets state
pluginExit(ptr)             → deletes the CP2112Plugin instance
```

`doInit()` does **not** open any hardware handle. Both interfaces are opened explicitly via their respective `open` sub-commands. The two handles are independent: GPIO and I2C can be open simultaneously on the same physical chip.

`setModuleSpeed()` for I2C performs a **close and reopen** at the new clock frequency, preserving the current slave address. For GPIO it returns `false` with a warning (GPIO has no data-rate concept; use `cfg clkdiv=N` for the clock-output function on GPIO.6).

### Command Dispatch Model

Two layers of `std::map` drive dispatch:

1. **Top-level map** (`m_mapCmds`): `INFO`, `I2C`, `GPIO` → member-function pointers.
2. **Module-level maps** (`m_mapCmds_I2C`, `m_mapCmds_GPIO`): sub-command name → handler pointer.

A meta-map (`m_mapCommandsMaps`) maps module name strings to their sub-maps. Registration is driven entirely by X-macros:

```cpp
// In i2c_config.hpp:
#define I2C_COMMANDS_CONFIG_TABLE  \
I2C_CMD_RECORD( open   )           \
I2C_CMD_RECORD( close  )           \
...

// In constructor (cp2112_plugin.hpp):
#define I2C_CMD_RECORD(a) \
    m_mapCmds_I2C.insert({#a, &CP2112Plugin::m_handle_i2c_##a});
I2C_COMMANDS_CONFIG_TABLE
#undef I2C_CMD_RECORD
```

### Generic Template Helpers

`cp2112_generic.hpp` provides the same stateless template functions used across all plugins:

| Function | Purpose |
|---|---|
| `generic_module_dispatch<T>()` | Splits `"subcmd args"` and routes to the module map |
| `generic_module_set_speed<T>()` | Looks up a speed label or raw Hz value and calls `setModuleSpeed()` |
| `generic_write_data<T>()` | Unhexlifies a hex string and calls a write callback (up to 4096 bytes) |
| `generic_write_read_data<T>()` | Parses `HEXDATA:rdlen` and calls a write-then-read callback |
| `generic_write_read_file<T>()` | Reads write data from a binary file, hard-caps rdchunk at 512 bytes |
| `generic_execute_script<T>()` | Runs a `CommScriptClient` script on an open driver handle |
| `generic_module_list_commands<T>()` | Logs all registered sub-command names (used by `help`) |

**CP2112-specific constants** defined in `cp2112_generic.hpp`:

| Constant | Value | Meaning |
|---|---|---|
| `CP2112_WRITE_CHUNK_SIZE` | 512 | Default chunk size for `wrrdf` (equals `CP2112::MAX_I2C_READ_LEN`) |
| `CP2112_BULK_MAX_BYTES` | 4096 | Upper bound for a single `generic_write_data` call |

The 512-byte read cap is enforced at the `generic_write_read_file` level to produce a clear error message before the CP2112 driver returns `INVALID_PARAM`.

### Pending Configuration Structs

| Struct | Fields | Default |
|---|---|---|
| `I2cPendingCfg` | `address`, `clockHz` | `0x50`, `100000` Hz |
| `GpioPendingCfg` | `directionMask`, `pushPullMask`, `specialFuncMask`, `clockDivider` | all `0x00` (all inputs, open-drain, no special functions) |

The I2C pending config is applied on the **next `open`**. The GPIO pending config is applied immediately if the interface is already open; otherwise it is stored for the next `open`.

### HID Transport and Chunking

The CP2112 communicates over USB-HID. Each HID report carries a maximum of 61 bytes of I²C payload. The underlying `CP2112` driver handles this transparently — the plugin can pass payloads up to 512 bytes for reads and up to 4096 bytes for writes without manual chunking. The driver automatically splits writes into multiple 61-byte HID reports.

The `scan` sub-command opens a **new HID handle for every address** it probes (0x08–0x77), sends a zero-byte write, and closes the handle. This is required because the CP2112 HID session is address-specific — the target 7-bit address is encoded at open time.

### INI Configuration Keys

| Key | Type | Default | Description |
|---|---|---|---|
| `ARTEFACTS_PATH` | string | `""` | Base directory for script and binary data files |
| `DEVICE_INDEX` | uint8 | `0` | Zero-based index when multiple CP2112 devices are attached |
| `I2C_CLOCK` | uint32 (Hz) | `100000` | Initial I2C clock frequency |
| `I2C_ADDRESS` | uint8 (hex) | `0x50` | Default I2C slave address |
| `READ_TIMEOUT` | uint32 (ms) | `1000` | Per-operation read timeout for script execution |
| `SCRIPT_DELAY` | uint32 (ms) | `0` | Inter-command delay during script execution |

---

## Building

```bash
mkdir build && cd build
cmake ..
make cp2112_plugin
```

The output is `cp2112_plugin.so` (Linux) or `cp2112_plugin.dll` (Windows). Note the `PREFIX ""` in `CMakeLists.txt` — no `lib` prefix is added.

**Required libraries:**

- `cp2112` — Silicon Labs CP2112 driver abstraction (CP2112Base, CP2112, CP2112Gpio)
- `uIPlugin`, `uPluginOps` — plugin framework
- `uSharedConfig`, `uICoreScript`, `uCommScriptClient`, `uCommScriptCommandInterpreter`, `uScriptReader` — scripting engine
- `uUtils` — uString, uNumeric, uHexlify, uHexdump, uLogger

**Windows only:** links `hid` and `setupapi` for HID access.

---

## Platform Notes

**Linux:**
- The CP2112 HID device appears as `/dev/hidraw*`. The driver is identified by VID `0x10C4` / PID `0xEA90`.
- The Silicon Labs Linux HID library or `hidapi` must be available.
- Multiple CP2112 devices are distinguished by `DEVICE_INDEX` (0 = first device).

**Windows:**
- Access is via the Windows HID stack. The Silicon Labs CP2112 DLL or `hidapi` must be available.
- `DEVICE_INDEX=0` opens the first CP2112 device enumerated by the OS.

---

## Command Reference

### INFO

Prints version, VID/PID/device index, and a complete command reference. Takes **no arguments** and works even before `doInit()`.

```
CP2112.INFO
```

**Example output (abbreviated):**
```
CP2112     | Vers: 1.0.0.0
CP2112     | Description: Silicon Labs CP2112 USB-HID to I2C/SMBus bridge with 8 GPIO pins
CP2112     |   VID 0x10C4 / PID 0xEA90  DeviceIndex: 0
...
```

---

### I2C

I²C/SMBus master port. Each session targets one 7-bit slave address. **Must call `open` before any data transfer.** `scan` is the only exception — it does not require a prior `open`.

```
CP2112.I2C <subcommand> [arguments]
```

---

#### I2C · open — Open interface for a specific slave address

Opens the CP2112 HID handle and configures it for the given slave address and clock frequency. If the interface is already open it is closed first.

```
CP2112.I2C open [addr=0xNN] [clock=N] [device=N]
```

| Parameter | Values | Default | Description |
|---|---|---|---|
| `addr` / `address` | `0x00`–`0x7F` | from INI / `0x50` | 7-bit I2C slave address |
| `clock` | any Hz value | from INI / `100000` | I2C clock in Hz (presets: `10kHz`, `100kHz`, `400kHz`) |
| `device` | 0, 1, 2… | from INI / `0` | Zero-based device index when multiple CP2112 are attached |

```
# Open for a 24C EEPROM at 0x50, 100 kHz
CP2112.I2C open addr=0x50 clock=100000

# Open for a DS1307 RTC at 0x68, standard speed
CP2112.I2C open addr=0x68 clock=100000

# Open for TMP102 temperature sensor at 0x48, fast mode
CP2112.I2C open addr=0x48 clock=400000

# Open on the second CP2112 device
CP2112.I2C open addr=0x50 device=1

# Open with a custom clock (150 kHz)
CP2112.I2C open addr=0x20 clock=150000
```

---

#### I2C · close — Release interface

```
CP2112.I2C close
```

---

#### I2C · cfg — Update pending address/clock

Updates the pending I2C configuration. **Changes take effect on the next `open`**, not immediately. To apply a new clock immediately while preserving the address, use `setModuleSpeed` (called indirectly via the speed map) which triggers a close-and-reopen.

```
CP2112.I2C cfg [addr=0xNN] [clock=N]
CP2112.I2C cfg ?
```

```
# Change the pending target address to a different device
CP2112.I2C cfg addr=0x68

# Change the pending clock
CP2112.I2C cfg clock=400000

# Change both at once
CP2112.I2C cfg addr=0x48 clock=400000

# Print current pending configuration
CP2112.I2C cfg ?
```

---

#### I2C · write — Write bytes to the slave

Sends a complete `START + addr+W + data + STOP` transaction. Payloads up to 512 bytes are supported; the driver auto-chunks at 61-byte HID boundaries. The slave address used is the one set at `open` time.

```
CP2112.I2C write <HEXDATA>
```

```
# Write register 0x00 with value 0x00 (2 bytes)
CP2112.I2C write 0000

# Write to register 0x50, value 0xFF
CP2112.I2C write 50FF

# Write 3 bytes: register 0x07 + 16-bit value 0x1234
CP2112.I2C write 071234

# Write a 4-byte configuration word
CP2112.I2C write A5B6C7D8

# Write 8 bytes at once
CP2112.I2C write 0102030405060708
```

---

#### I2C · read — Read N bytes from the slave

Reads N bytes from the slave address configured at `open`. Maximum 512 bytes per call. Results are printed as a hex dump.

```
CP2112.I2C read <N>
```

```
# Read 2 bytes (e.g., a 16-bit register)
CP2112.I2C read 2

# Read 1 byte (e.g., a status register)
CP2112.I2C read 1

# Read 16 bytes
CP2112.I2C read 16

# Read maximum 512 bytes
CP2112.I2C read 512
```

---

#### I2C · wrrd — Write then read in one sequence

Performs a write phase immediately followed by a read phase against the configured slave address. The write phase sends the given hex bytes; the read phase reads back `rdlen` bytes. The read length must not exceed 512 bytes.

```
CP2112.I2C wrrd <HEXDATA>:<rdlen>
```

Both the write and read phases are optional. Use `:N` for a read-only operation, or omit the colon for write-only.

```
# Write register address 0x00 0x00, read back 2 bytes (e.g., EEPROM read)
CP2112.I2C wrrd 0000:2

# Write a single register pointer byte, read 1 byte
CP2112.I2C wrrd 50:1

# Read-only: skip the write phase, read 4 bytes
CP2112.I2C wrrd :4

# Write-only: send 2 bytes, no read phase
CP2112.I2C wrrd A550

# Write 3 bytes of config, read back 1 byte status
CP2112.I2C wrrd 07 1234:1

# Write DS1307 register pointer, read 7 bytes of time/date registers
CP2112.I2C wrrd 00:7
```

---

#### I2C · wrrdf — Write/read using binary files

Same as `wrrd` but write data comes from a binary file in `ARTEFACTS_PATH`. The read chunk size is hard-capped at **512 bytes** (the CP2112's `MAX_I2C_READ_LEN`). Specifying a larger `rdchunk` produces an error before the driver is called.

```
CP2112.I2C wrrdf <filename>[:<wrchunk>][:<rdchunk>]
```

```
# Send file contents, use default chunk sizes (512 bytes each)
CP2112.I2C wrrdf i2c_sequence.bin

# Write in 16-byte chunks, read 16 bytes per chunk
CP2112.I2C wrrdf eeprom_write.bin:16:16

# Write-only (rdchunk = 0 disables the read phase)
CP2112.I2C wrrdf config_data.bin:64:0
```

---

#### I2C · scan — Probe bus for devices

Probes every 7-bit I2C address in the range `0x08`–`0x77` for ACK by sending a zero-byte write. **No prior `open` is required** — the scan opens a temporary HID handle per address using the current pending `clock` and `device` index. Prints the address of every responding device.

```
CP2112.I2C scan
```

```
# Basic scan (uses clock and device index from INI or last cfg)
CP2112.I2C scan

# Recommended: set the clock first, then scan
CP2112.I2C cfg clock=100000
CP2112.I2C scan
# Output example:
# CP2112_I2C | Found device at 0x48
# CP2112_I2C | Found device at 0x50
# CP2112_I2C | Found device at 0x68
```

---

#### I2C · script — Execute a command script

Runs a `CommScriptClient` script from `ARTEFACTS_PATH`. **I2C must be open first.**

```
CP2112.I2C script <filename>
```

```
CP2112.I2C open addr=0x50 clock=100000
CP2112.I2C script eeprom_test.txt

CP2112.I2C open addr=0x48 clock=400000
CP2112.I2C script tmp102_read.txt
```

---

### GPIO

8-pin GPIO port (GPIO.0–GPIO.7). All operations use **8-bit bitmasks** where bit N corresponds to GPIO.N. **Must call `open` before any operation.**

```
CP2112.GPIO <subcommand> [arguments]
```

---

#### GPIO · open — Open interface and apply initial pin configuration

Opens the CP2112Gpio HID handle and immediately pushes the initial pin configuration to the hardware. All configuration parameters are optional; omitted ones use the current pending values (defaults: all inputs, open-drain, no special functions).

```
CP2112.GPIO open [device=N] [dir=0xNN] [pp=0xNN] [special=0xNN] [clkdiv=N]
```

| Parameter | Aliases | Values | Default | Description |
|---|---|---|---|---|
| `device` | — | 0, 1, 2… | `0` | Device index |
| `dir` | `direction` | `0x00`–`0xFF` | `0x00` | Direction mask: bit=1 → output, bit=0 → input |
| `pp` | `pushpull` | `0x00`–`0xFF` | `0x00` | Drive mode: bit=1 → push-pull, bit=0 → open-drain |
| `special` | `sf` | `0x00`–`0xFF` | `0x00` | Special function enable (see [Special-Function Pins](#gpio-special-function-pin-reference)) |
| `clkdiv` | `divider` | `0x00`–`0xFF` | `0x00` | Clock divider for GPIO.6 clock output |

```
# All outputs, push-pull (e.g., driving LEDs directly)
CP2112.GPIO open dir=0xFF pp=0xFF

# Lower nibble as push-pull outputs, upper nibble as inputs
CP2112.GPIO open dir=0x0F pp=0x0F

# All inputs (default state)
CP2112.GPIO open

# Enable IRQ output on GPIO.1 and clock output on GPIO.6
CP2112.GPIO open special=0x42 clkdiv=4

# Second CP2112 device
CP2112.GPIO open device=1 dir=0x0F pp=0x0F

# Mix: GPIO.0–3 push-pull outputs, GPIO.4–7 open-drain outputs
CP2112.GPIO open dir=0xFF pp=0x0F
```

---

#### GPIO · close — Release interface

```
CP2112.GPIO close
```

---

#### GPIO · cfg — Update pin configuration

Updates the pending GPIO configuration. **If GPIO is already open, the configuration is applied immediately to the hardware.** If not open, it is stored for the next `open`.

```
CP2112.GPIO cfg [dir=0xNN] [pp=0xNN] [special=0xNN] [clkdiv=N]
CP2112.GPIO cfg ?
```

```
# Change direction: lower nibble as outputs
CP2112.GPIO cfg dir=0x0F

# Enable push-pull drive for GPIO.0 and GPIO.1
CP2112.GPIO cfg pp=0x03

# Disable all special functions
CP2112.GPIO cfg special=0x00

# Enable TX LED on GPIO.0 and RX LED on GPIO.7
CP2112.GPIO cfg special=0x81

# Enable clock output on GPIO.6 with divider 8
CP2112.GPIO cfg special=0x40 clkdiv=8

# Print current pending configuration
CP2112.GPIO cfg ?
```

---

#### GPIO · write — Drive output pins with selective mask

Drives specified pins to specified levels. The `VALUE` byte sets the desired state; the `MASK` byte selects which pins are updated. Unmasked pins retain their current state.

```
CP2112.GPIO write <VALUE> <MASK>
```

Both arguments are hex bytes (`0x00`–`0xFF` or bare decimal/hex without prefix).

```
# Set GPIO.0 high, leave all other pins unchanged
CP2112.GPIO write 0x01 0x01

# Clear GPIO.0 low, leave all other pins unchanged
CP2112.GPIO write 0x00 0x01

# Apply alternating pattern 0xAA to all 8 pins
CP2112.GPIO write 0xAA 0xFF

# Set lower nibble to 0x05, leave upper nibble unchanged
CP2112.GPIO write 0x05 0x0F

# Drive GPIO.4-7 all high, leave GPIO.0-3 unchanged
CP2112.GPIO write 0xF0 0xF0

# Clear all pins to 0
CP2112.GPIO write 0x00 0xFF
```

---

#### GPIO · set — Drive masked pins HIGH

Convenience wrapper for `write MASK MASK`. All pins in the mask are driven HIGH; unmasked pins are unchanged.

```
CP2112.GPIO set <MASK>
```

```
# GPIO.0 high (e.g., turn on LED)
CP2112.GPIO set 0x01

# GPIO.0 and GPIO.2 high
CP2112.GPIO set 0x05

# All GPIO.0–3 high
CP2112.GPIO set 0x0F

# All 8 pins high
CP2112.GPIO set 0xFF

# GPIO.7 high (RX LED when not in special-function mode)
CP2112.GPIO set 0x80
```

---

#### GPIO · clear — Drive masked pins LOW

Convenience wrapper for `write 0x00 MASK`. All pins in the mask are driven LOW; unmasked pins are unchanged.

```
CP2112.GPIO clear <MASK>
```

```
# GPIO.0 low (e.g., turn off LED)
CP2112.GPIO clear 0x01

# Clear GPIO.0 and GPIO.2
CP2112.GPIO clear 0x05

# Clear upper nibble (GPIO.4–7)
CP2112.GPIO clear 0xF0

# Clear all pins
CP2112.GPIO clear 0xFF
```

---

#### GPIO · read — Read current pin levels

Reads the current logic level of all 8 GPIO pins from the hardware and prints the result as a hex byte and a binary representation. Input and output pins are both returned.

```
CP2112.GPIO read
```

**Output format:**
```
CP2112_GPIO| GPIO: 0x3F  [00111111]
```

- The hex value is the raw 8-bit port register.
- The binary string has **bit 7 on the left** (GPIO.7) and **bit 0 on the right** (GPIO.0).

```
CP2112.GPIO open dir=0x0F pp=0x0F
CP2112.GPIO set 0x05
CP2112.GPIO read
# → GPIO: 0x05  [00000101]  — GPIO.0 and GPIO.2 high, all others low
```

---

## I2C Speed Reference

The CP2112 supports any raw Hz clock value up to the hardware maximum. Three preset labels are registered:

| Label | Frequency | Use case |
|---|---|---|
| `10kHz` | 10,000 Hz | Very long cables, highly capacitive buses |
| `100kHz` | 100,000 Hz | Standard mode (most common) |
| `400kHz` | 400,000 Hz | Fast mode (short cables, capable devices) |

Raw Hz values are accepted directly in both `open` and `cfg`:

```
CP2112.I2C open clock=100000    # standard mode
CP2112.I2C open clock=400000    # fast mode
CP2112.I2C open clock=50000     # custom: 50 kHz
CP2112.I2C open clock=10000     # low speed: 10 kHz
```

Changing speed while the interface is open: close and reopen with the new value.

```
CP2112.I2C close
CP2112.I2C open addr=0x50 clock=400000
```

---

## GPIO Special-Function Pin Reference

The CP2112 allows four GPIO pins to operate as hardware special functions (configured via the `special` mask in `open` / `cfg`). When a bit in `specialFuncMask` is set, the corresponding pin operates in special-function mode and cannot be used as a general-purpose GPIO.

| Bit | Pin | Special function | Description |
|---|---|---|---|
| 0 | GPIO.0 | TX LED | Active-low LED driver; pulses when I2C transaction is active |
| 1 | GPIO.1 | IRQ output | Open-drain interrupt output; driven by the CP2112 firmware |
| 6 | GPIO.6 | CLK output | Programmable clock output; frequency set by `clkdiv` |
| 7 | GPIO.7 | RX LED | Active-low LED driver; pulses when data is received |

Bits 2–5 are standard GPIO and have no special functions.

**Clock output frequency formula (GPIO.6):**
```
F_clk = 48 MHz / (2 × clockDivider)
```

| `clkdiv` | Clock output |
|---|---|
| 1 | 24 MHz |
| 2 | 12 MHz |
| 4 | 6 MHz |
| 8 | 3 MHz |
| 48 | 500 kHz |
| 255 | ~94.1 kHz |

```
# Enable LED indicators: TX on GPIO.0, RX on GPIO.7
CP2112.GPIO open special=0x81

# Enable 6 MHz clock output on GPIO.6 (clkdiv=4)
CP2112.GPIO open special=0x40 clkdiv=4

# Enable all special functions simultaneously
CP2112.GPIO open special=0xC3 clkdiv=2

# Disable all special functions (all pins as GPIO)
CP2112.GPIO cfg special=0x00
```

---

## Script Files

Script files are plain text files stored under `ARTEFACTS_PATH`. They are executed by the `CommScriptClient` engine line by line. The `SCRIPT_DELAY` INI key inserts a per-command delay in milliseconds.

**I2C must be open before calling `script`.** Unlike the BusPirate plugin, this plugin passes the existing open driver handle directly to `CommScriptClient` (no new handle is opened).

```
CP2112.I2C open addr=0x50 clock=100000
CP2112.I2C script eeprom_dump.txt

CP2112.I2C open addr=0x68 clock=100000
CP2112.I2C script rtc_read.txt

CP2112.I2C open addr=0x48 clock=400000
CP2112.I2C script sensor_calibrate.txt
```

---

## Fault-Tolerant and Dry-Run Modes

- **Dry-run mode**: when `doEnable()` has not been called, every command validates its arguments and returns `true` without touching hardware. The generic dispatcher detects `isEnabled() == false` and returns early.

- **Fault-tolerant mode** (`setFaultTolerant()` / `isFaultTolerant()`): when set, the plugin framework can be configured to continue executing past command failures. Useful in production test scripts where a non-critical probe failure should not abort a longer sequence.

- **Privileged mode** (`isPrivileged()`): always returns `false`.

---

## Error Handling and Return Values

Every handler returns `bool`:
- `true` — success (or argument validation passed in disabled mode).
- `false` — bad argument, unknown sub-command, HID handle open failed, hardware operation returned a non-`SUCCESS` status, or read/write length limit exceeded.

Notable limit enforcement:
- `read`: rejects N > 512 before calling the driver.
- `wrrd` read phase: rejects rdlen > 512 before calling the driver.
- `wrrdf`: hard-caps `rdchunk` at 512 with an explicit error message.
- `write`: accepts up to 4096 bytes (driver auto-chunks at 61 bytes per HID report).

Diagnostic messages are emitted via `LOG_PRINT`:

| Level | Usage |
|---|---|
| `LOG_ERROR` | Command failed, invalid argument, hardware error |
| `LOG_WARNING` | Non-fatal issue (e.g., closing a port that was not open, GPIO speed not supported) |
| `LOG_INFO` | Successful operations (bytes written, device opened, clock updated) |
| `LOG_FIXED` | Help text and scan results |
