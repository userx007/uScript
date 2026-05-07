# Digispark Plugin

A C++ shared-library plugin that exposes I2C and SPI bus operations through a
Digispark ATtiny85 USB bridge, using the same plugin lifecycle and command
dispatch model as the UART plugin.

**Version:** 1.0.0.0

---

## Table of Contents

1. [Overview](#overview)
2. [Project Structure](#project-structure)
3. [Architecture](#architecture)
   - [Plugin Lifecycle](#plugin-lifecycle)
   - [Command Dispatch Model](#command-dispatch-model)
   - [INI Configuration Keys](#ini-configuration-keys)
4. [Building](#building)
5. [Hardware Setup](#hardware-setup)
6. [Command Reference](#command-reference)
   - [INFO](#info)
   - [CONFIG](#config)
   - [I2C_SCAN](#i2c_scan)
   - [I2C_WRITE](#i2c_write)
   - [I2C_READ](#i2c_read)
   - [I2C_WRRD](#i2c_wrrd)
   - [SPI_CFG](#spi_cfg)
   - [SPI_WRITE](#spi_write)
   - [SPI_READ](#spi_read)
   - [SPI_XFER](#spi_xfer)
   - [SPI_WRREG](#spi_wrreg)
   - [SPI_RDREG](#spi_rdreg)
   - [SCRIPT](#script)
7. [Script Files](#script-files)
8. [Data Format](#data-format)
9. [Fault-Tolerant Mode](#fault-tolerant-mode)

---

## Overview

The plugin loads as a dynamic shared library (`.so` / `.dll`). The host calls
`pluginEntry()` / `pluginExit()` to create and destroy the plugin. Once loaded,
INI settings are applied via `setParams()`, the plugin is armed with `doInit()`
and `doEnable()`, and commands are dispatched via `doDispatch()`.

Two independent firmware images exist for the Digispark:

| Firmware | Protocol | Library used |
|---|---|---|
| `i2c_bridge.ino` | I2C master | `I2CBridge` (TinyWireM + DigiUSB) |
| `spi_bridge.ino` | SPI master | `SPIBridge` (TinySPI + DigiUSB) |

Flash only the firmware that matches the protocol you intend to use. Both share
the same USB VID/PID (`0x16C0` / `0x05DF`).

All commands follow the pattern:

```
DIGISPARK.<COMMAND> [arguments]
```

---

## Project Structure

```
digispark_plugin/
├── CMakeLists.txt
├── docs/
│   └── README.md
├── inc/
│   └── digispark_plugin.hpp   # Class definition, X-macro command table
└── src/
    └── digispark_plugin.cpp   # Entry points, all command handlers,
                               # driver helpers, argument parsers
```

The plugin depends on two static libraries built alongside it:

```
i2c_bridge/    → libi2c_bridge.a   (uI2C.hpp + uI2CCommon.cpp + uI2CLinux.cpp)
spi_bridge/    → libspi_bridge.a   (uSPI.hpp + uSPICommon.cpp + uSPILinux.cpp)
```

---

## Architecture

### Plugin Lifecycle

```
pluginEntry()           → creates DigisparkPlugin instance; populates m_mapCmds
  setParams()           → loads INI values (VID, PID, timeouts, SPI mode/div)
  doInit()              → marks plugin as initialized (no HID device opened yet)
  doEnable()            → enables real execution (without this: validate-only mode)
  doDispatch(cmd, args) → routes command string to the correct handler
  doCleanup()           → marks plugin as uninitialized and disabled
pluginExit(ptr)         → deletes the DigisparkPlugin instance
```

> **RAII device management:** The HID device is **not** opened at `doInit()`.
> Instead, each command handler opens the device on entry and closes it on
> return — exactly like the UART plugin opens and closes the serial port per
> command. `SCRIPT` is the exception: it holds the device open across all lines
> in the file by dispatching each line through `doDispatch()` without
> re-entering the RAII block.

### Command Dispatch Model

Commands are registered via a single X-macro table at construction:

```cpp
#define DIGISPARK_PLUGIN_COMMANDS_CONFIG_TABLE  \
DIGISPARK_PLUGIN_CMD_RECORD( INFO      )        \
DIGISPARK_PLUGIN_CMD_RECORD( CONFIG    )        \
DIGISPARK_PLUGIN_CMD_RECORD( I2C_SCAN  )        \
...

// Constructor:
#define DIGISPARK_PLUGIN_CMD_RECORD(a) \
    m_mapCmds.insert(std::make_pair(#a, &DigisparkPlugin::m_DIGISPARK_##a));
DIGISPARK_PLUGIN_COMMANDS_CONFIG_TABLE
#undef DIGISPARK_PLUGIN_CMD_RECORD
```

Adding a new command requires only one new `DIGISPARK_PLUGIN_CMD_RECORD` entry
and a matching `m_DIGISPARK_<CMD>` implementation.

### INI Configuration Keys

| Key | Type | Default | Description |
|---|---|---|---|
| `VID` | hex string | `0x16C0` | USB Vendor ID of the Digispark |
| `PID` | hex string | `0x05DF` | USB Product ID of the Digispark |
| `READ_TIMEOUT` | uint32 ms | `2000` | Per-read HID timeout |
| `WRITE_TIMEOUT` | uint32 ms | `2000` | Per-write HID timeout |
| `SPI_MODE` | uint32 0–3 | `0` | SPI CPOL/CPHA mode |
| `SPI_CLK_DIV` | uint32 0–3 | `1` | SPI clock divider (1 = DIV4 ≈ 4 MHz) |
| `ARTEFACTS_PATH` | string | `""` | Base directory for SCRIPT file resolution |

All values can be overridden at runtime via `DIGISPARK.CONFIG`.

---

## Building

```bash
# Install hidapi
sudo apt install libhidapi-dev

# Build (from the top-level CMake project that includes all sub-libraries)
mkdir build && cd build
cmake ..
make digispark_plugin
```

Output: `libdigispark_plugin.so`

---

## Hardware Setup

### I2C (flash `i2c_bridge.ino`)

| Signal | Digispark pin | Note |
|---|---|---|
| SDA | PB0 (pin 0) | 4.7 kΩ pull-up to 3.3 V / 5 V |
| SCL | PB2 (pin 2) | 4.7 kΩ pull-up to 3.3 V / 5 V |

### SPI (flash `spi_bridge.ino`)

| Signal | Digispark pin | Note |
|---|---|---|
| MOSI | PB0 (pin 0) | |
| MISO | PB1 (pin 1) | |
| SCK | PB2 (pin 2) | |
| CS | hardwired GND | Single-slave use; no free GPIO for CS |

### Linux udev rules

```bash
sudo tee /etc/udev/rules.d/49-digispark.rules <<'EOF'
SUBSYSTEMS=="usb", ATTRS{idVendor}=="16d0", ATTRS{idProduct}=="0753", MODE:="0666"
SUBSYSTEMS=="usb", ATTRS{idVendor}=="16c0", ATTRS{idProduct}=="05df", MODE:="0666"
EOF
sudo udevadm control --reload-rules && sudo udevadm trigger
```

---

## Command Reference

### INFO

Print plugin version and usage summary. No arguments required. Works even if
`doInit()` has not been called.

```
DIGISPARK.INFO
```

---

### CONFIG

Override connection parameters at runtime. Any subset of keys may be given;
unspecified keys retain their current values.

```
DIGISPARK.CONFIG [v:<vid>] [p:<pid>] [r:<read_ms>] [w:<write_ms>]
```

| Token | Description |
|---|---|
| `v:<hex>` | HID Vendor ID (decimal or `0x`-prefixed hex) |
| `p:<hex>` | HID Product ID (decimal or `0x`-prefixed hex) |
| `r:<ms>` | Read timeout in milliseconds |
| `w:<ms>` | Write timeout in milliseconds |

```
DIGISPARK.CONFIG v:0x16C0 p:0x05DF r:2000 w:2000
DIGISPARK.CONFIG r:5000
DIGISPARK.CONFIG v:0x16C0 p:0x05DF
```

---

### I2C_SCAN

Probe all 7-bit I2C addresses (1–126). Found addresses are printed to the log
and stored in `getData()` as a comma-separated hex list.

```
DIGISPARK.I2C_SCAN
```

**Example output:**
```
DIGISPARK   | I2C_SCAN: found 3 device(s)
DIGISPARK   |   0x3C
DIGISPARK   |   0x68
DIGISPARK   |   0x76
```

---

### I2C_WRITE

Write 1–5 data bytes to an I2C slave.

```
DIGISPARK.I2C_WRITE <addr_hex> <byte0_hex> [<byte1_hex> ...]
```

```
# SSD1306 OLED: command byte + display-ON
DIGISPARK.I2C_WRITE 3C 00 AF

# MPU-6050: write 0x00 to PWR_MGMT_1 (wake-up)
DIGISPARK.I2C_WRITE 0x68 6B 00

# BMP280: soft-reset (reg=0xE0, val=0xB6)
DIGISPARK.I2C_WRITE 76 E0 B6
```

---

### I2C_READ

Read 1–6 bytes from an I2C slave. The received bytes are logged and stored in
`getData()` as space-separated hex values.

```
DIGISPARK.I2C_READ <addr_hex> <n_bytes>
```

```
DIGISPARK.I2C_READ 68 1
DIGISPARK.I2C_READ 3C 6
```

---

### I2C_WRRD

Write a register address (1 byte), issue a repeated START, then read 1–5 bytes.
This is the standard register-read pattern used by most I2C sensors.

```
DIGISPARK.I2C_WRRD <addr_hex> <reg_hex> <n_bytes>
```

```
# MPU-6050 WHO_AM_I (reg=0x75) → expect 0x68
DIGISPARK.I2C_WRRD 68 75 1

# MPU-6050 accel XYZ first 5 bytes (reg=0x3B)
DIGISPARK.I2C_WRRD 68 3B 5

# BMP280 chip-ID (reg=0xD0) → expect 0x60
DIGISPARK.I2C_WRRD 76 D0 1
```

---

### SPI_CFG

Send `CMD_SPI_CONFIG` to the firmware to set the clock mode and divider.
Parameters are also stored locally and applied to every subsequent SPI command.

```
DIGISPARK.SPI_CFG [m:<mode_0_3>] [d:<div_0_3>]
```

| Divider | Value | Approx. SCK (16.5 MHz base) |
|---|---|---|
| DIV2 | `0` | 8.25 MHz |
| DIV4 | `1` | 4.1 MHz (default) |
| DIV8 | `2` | 2.0 MHz |
| DIV16 | `3` | 1.0 MHz |

```
DIGISPARK.SPI_CFG m:0 d:1     # MODE0, ~4 MHz
DIGISPARK.SPI_CFG m:3 d:3     # MODE3, ~1 MHz
DIGISPARK.SPI_CFG d:2         # change divider only
```

---

### SPI_WRITE

Write 1–6 bytes on MOSI. MISO data is discarded.

```
DIGISPARK.SPI_WRITE <byte0_hex> [<byte1_hex> ...]
```

```
DIGISPARK.SPI_WRITE DE AD BE EF
DIGISPARK.SPI_WRITE 0x01 0x02 0x03
```

---

### SPI_READ

Clock in N bytes with MOSI driven as `0x00`. Received bytes are logged and
stored in `getData()`.

```
DIGISPARK.SPI_READ <n_bytes>
```

```
DIGISPARK.SPI_READ 4
DIGISPARK.SPI_READ 1
```

---

### SPI_XFER

Full-duplex transfer: clock MOSI bytes out while capturing the same number of
MISO bytes in. MISO bytes are logged and stored in `getData()`.

```
DIGISPARK.SPI_XFER <byte0_hex> [<byte1_hex> ...]
```

```
# JEDEC ID (0x9F + 2 dummy bytes → 3-byte manufacturer/device ID)
DIGISPARK.SPI_XFER 9F 00 00

# MCP3204 ADC single-ended CH0
DIGISPARK.SPI_XFER 06 00 00
```

---

### SPI_WRREG

Write one byte to a register using the **MSB=0 → write** convention common to
SPI sensor register maps (BME280, ICM-42688-P, LIS3DH, MAX31865, …).

Internally sends `[reg & 0x7F, value]`.

```
DIGISPARK.SPI_WRREG <reg_hex> <val_hex>
```

```
# BME280 soft-reset
DIGISPARK.SPI_WRREG E0 B6

# LIS3DH CTRL_REG1: ODR=1.344 kHz, all axes enabled
DIGISPARK.SPI_WRREG 20 97
```

---

### SPI_RDREG

Read 1–5 bytes starting at a register using the **MSB=1 → read** convention.

Internally sends `[reg | 0x80, 0x00 × N]` and returns the N MISO bytes that
follow the address byte. Result is logged and stored in `getData()`.

```
DIGISPARK.SPI_RDREG <reg_hex> <n_bytes>
```

```
# BME280 chip-ID (reg=0xD0) → expect 0x60
DIGISPARK.SPI_RDREG D0 1

# LIS3DH OUT_X_L + 4 following bytes (multi-byte read with auto-increment)
DIGISPARK.SPI_RDREG 28 5
```

---

### SCRIPT

Execute a sequence of Digispark commands from a plain-text file. Each
non-empty, non-comment line is dispatched as a command. Lines beginning with
`#` are skipped. An optional inter-command delay can be inserted between lines.

```
DIGISPARK.SCRIPT <filename> [<delay_ms>]
```

- `filename` is resolved relative to `ARTEFACTS_PATH`.
- `delay_ms` defaults to `0`.
- In **fault-tolerant** mode a failing line is logged but execution continues.
- In normal mode a failing line aborts the script and returns `false`.

```
DIGISPARK.SCRIPT i2c_oled_init.txt
DIGISPARK.SCRIPT spi_bme280_test.txt 50
```

---

## Script Files

Script files are plain text stored under `ARTEFACTS_PATH`. Each line is one
command. The plugin prefix is optional: both full and bare forms are accepted.

**`i2c_oled_init.txt`:**
```
# SSD1306 128×64 OLED initialisation sequence
I2C_WRITE 3C 00 AE    # display off
I2C_WRITE 3C 00 D5    # set display clock
I2C_WRITE 3C 00 80
I2C_WRITE 3C 00 A8    # set multiplex ratio
I2C_WRITE 3C 00 3F
I2C_WRITE 3C 00 D3    # set display offset
I2C_WRITE 3C 00 00
I2C_WRITE 3C 00 8D    # charge pump
I2C_WRITE 3C 00 14
I2C_WRITE 3C 00 AF    # display on
```

**`spi_bme280_test.txt`:**
```
# BME280 SPI test sequence
SPI_CFG m:0 d:1              # MODE0, ~4 MHz
SPI_RDREG D0 1               # chip-ID → expect 0x60
SPI_WRREG E0 B6              # soft-reset
SPI_WRREG F2 01              # humidity oversampling ×1
SPI_WRREG F4 27              # temp/press oversampling ×1, normal mode
SPI_RDREG F3 1               # status register
```

**`mpu6050_read.txt`:**
```
# MPU-6050 I2C — wake and read accelerometer
I2C_WRITE 68 6B 00           # wake up (clear sleep bit)
I2C_WRRD  68 3B 5            # read accel X + Y (5 bytes)
I2C_WRRD  68 75 1            # WHO_AM_I → expect 0x68
```

Run with:
```
DIGISPARK.SCRIPT i2c_oled_init.txt
DIGISPARK.SCRIPT spi_bme280_test.txt 10
DIGISPARK.SCRIPT mpu6050_read.txt 5
```

---

## Data Format

All byte arguments are given as hexadecimal values, either bare (`3C`, `AF`) or
with a `0x` prefix (`0x3C`, `0xAF`). Upper and lower case are both accepted.

Read results are stored in `getData()` as space-separated `0xNN` values:

```
"0x60"           # one byte
"0x68 0x00 0x00 0x44 0xA0"  # five bytes
```

Scan results are stored as comma-separated `0xNN` values:

```
"0x3C,0x68,0x76"
```

---

## Fault-Tolerant Mode

When `isFaultTolerant()` returns `true` (set via `setParams()`), a failing
command inside a `SCRIPT` file is logged at `LOG_ERROR` severity but execution
continues with the next line. This is useful in test sequences where a missing
optional device should not abort the entire run.

In normal mode (default), any failing command in a script immediately returns
`false` and stops the script.
