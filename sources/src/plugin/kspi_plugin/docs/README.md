# SPI Plugin

A C++ shared-library plugin that exposes a general-purpose SPI interface through a unified command dispatcher. The plugin supports sending and receiving data over a Linux SPI bus (`/dev/spidevB.C`) using inline command expressions or external script files, and provides runtime reconfiguration of device parameters without reloading the plugin.

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
5. [Command Reference](#command-reference)
   - [INFO](#info)
   - [CONFIG](#config)
   - [CMD](#cmd)
   - [SCRIPT](#script)
6. [CMD Expression Syntax](#cmd-expression-syntax)
   - [Direction Operators](#direction-operators)
   - [Data Formats](#data-formats)
   - [Composite Expressions](#composite-expressions)
7. [Script Files](#script-files)
8. [Fault-Tolerant and Privileged Modes](#fault-tolerant-and-privileged-modes)
9. [Error Handling and Return Values](#error-handling-and-return-values)

---

## Overview

The plugin loads as a dynamic shared library (`.so`). The host application calls the exported C entry points `pluginEntry()` / `pluginExit()` to create and destroy the plugin object. Once loaded, the host passes configuration settings (device path, mode, speed, bits-per-word, timeouts, buffer size) via `setParams()`, optionally calls `doInit()`, and then calls `doDispatch()` for every command it wants to execute.

All commands follow the pattern:

```
<PLUGIN>.<COMMAND> [arguments]
```

For example:

```
SPI.CONFIG d=/dev/spidev0.0 m=0 z=1000000 b=8 r=2000 w=2000 s=256
SPI.CMD > H"0102030405" | H"0000000000"
SPI.CMD < "Ready" | "Go!"
SPI.SCRIPT flash_init.txt
```

---

## Project Structure

```
kspi_plugin/
├── CMakeLists.txt          # Build definition (shared library)
├── docs/
│   └── README.md           # This file
├── inc/
│   └── kspi_plugin.hpp      # Class definition, command table, public accessors
└── src/
    └── kspi_plugin.cpp      # Entry points, command handlers, init/cleanup, send/receive
```

---

## Architecture

### Plugin Lifecycle

```
pluginEntry()           → creates SPIPlugin instance
  setParams()           → loads INI values (device, mode, speed, bpw, timeouts, buffer size)
  doInit()              → marks plugin as initialized (no hardware opened yet)
  doEnable()            → enables real execution (without this, commands validate args only)
  doDispatch(cmd, args) → routes a command string to the correct handler
  doCleanup()           → marks plugin as uninitialized and disabled
pluginExit(ptr)         → deletes the SPIPlugin instance
```

> **Note:** `doInit()` does not open the SPI device. The device is opened on demand inside each `CMD` and `SCRIPT` call using RAII — the `SPI` driver object opens on construction and closes on destruction. A `SPI::SpiConfig` struct is assembled from the current plugin settings (`m_u8SpiMode`, `m_u32SpiSpeedHz`, `m_u8SpiBitsPerWord`) at call time, so a single plugin instance can address different chipselects or modes simply by calling `CONFIG` between commands.

`doEnable()` controls a "dry-run / validation" mode: when not enabled, every command validates its arguments and returns `true` without performing any I/O.

### Command Dispatch Model

Commands are registered via a single-level `std::map` (`m_mapCmds`) populated in the constructor through an X-macro expansion:

```cpp
#define SPI_PLUGIN_COMMANDS_CONFIG_TABLE    \
SPI_PLUGIN_CMD_RECORD( INFO               ) \
SPI_PLUGIN_CMD_RECORD( CONFIG             ) \
SPI_PLUGIN_CMD_RECORD( CMD                ) \
SPI_PLUGIN_CMD_RECORD( SCRIPT             )

// In the constructor:
#define SPI_PLUGIN_CMD_RECORD(a) \
    m_mapCmds.insert(std::make_pair(#a, &SPIPlugin::m_SPI_##a));
SPI_PLUGIN_COMMANDS_CONFIG_TABLE
#undef SPI_PLUGIN_CMD_RECORD
```

Adding a new top-level command requires only a new entry in the config table and a corresponding handler implementation.

### INI Configuration Keys

The following keys are read from the host configuration/INI file at `setParams()` time:

| Key | Type | Description |
|---|---|---|
| `SPI_DEVICE` | string | SPI device node (e.g. `/dev/spidev0.0`) |
| `SPI_MODE` | uint8 | SPI mode 0–3 (CPOL/CPHA combination) |
| `SPI_SPEED_HZ` | uint32 | Bus clock frequency in Hz (e.g. `1000000` for 1 MHz) |
| `SPI_BITS_PER_WORD` | uint8 | Bits per word, typically `8` |
| `READ_TIMEOUT` | uint32 | Per-read timeout in milliseconds |
| `WRITE_TIMEOUT` | uint32 | Per-write timeout in milliseconds |
| `READ_BUF_SIZE` | uint32 | Receive buffer size in bytes (max 256) |
| `ARTEFACTS_PATH` | string | Base directory from which script file paths are resolved |

All of these values can also be overridden at runtime using the `CONFIG` command without reloading the plugin.

---

## Building

The plugin is built as a CMake shared library. It links against `uIPlugin`, `uICoreScript`, `uCommScriptClient`, `uCommScriptCommandInterpreter`, `uScriptReader`, `uPluginOps`, and `uKSpi`, which must be available in the CMake build tree.

```bash
mkdir build && cd build
cmake ..
make kspi_plugin
```

The output is `libspi_plugin.so`.

---

## Command Reference

### INFO

Prints version information and a concise usage summary of all supported commands directly to the logger. Takes **no arguments** and works even if `doInit()` failed.

```
SPI.INFO
```

**Example output:**
```
SPI         | SPI Vers: 1.0.0.0
SPI         | Build: May 31 2026 ...
SPI         | Description: communicate with devices via SPI (/dev/spidevB.C)
SPI         | CONFIG : set the SPI device and bus parameters
SPI         |   Args : [d=device] [m=mode] [z=speed_hz] [b=bits_per_word] [r=read_tout] [w=write_tout] [s=recv_bufsize]
SPI         |   Usage: SPI.CONFIG d=/dev/spidev0.0 m=0 z=1000000 b=8 r=2000 w=2000 s=256
SPI         | SCRIPT : send commands from a script file
SPI         |   Args : script
SPI         |   Usage: SPI.SCRIPT script.txt
SPI         | CMD    : send, receive or both
SPI         |   Args : direction message
SPI         |   Usage: SPI.CMD > H"AABBCCDD" | H"00000000"
SPI         |          SPI.CMD < "Please send!" | F"data.bin, 256"
```

---

### CONFIG

Overrides the SPI bus parameters at runtime. Any subset of parameters can be specified; omitted keys retain their current values.

```
SPI.CONFIG [d=<device>] [m=<mode>] [z=<speed_hz>] [b=<bits_per_word>] [r=<read_timeout>] [w=<write_timeout>] [s=<recv_bufsize>]
```

| Token | INI key | Description |
|---|---|---|
| `d=<device>` | `SPI_DEVICE` | SPI device node path |
| `m=<mode>` | `SPI_MODE` | SPI mode: `0` (CPOL=0 CPHA=0), `1` (CPOL=0 CPHA=1), `2` (CPOL=1 CPHA=0), `3` (CPOL=1 CPHA=1) |
| `z=<hz>` | `SPI_SPEED_HZ` | Bus clock frequency in Hz |
| `b=<bpw>` | `SPI_BITS_PER_WORD` | Bits per word (typically `8`) |
| `r=<ms>` | `READ_TIMEOUT` | Read timeout in milliseconds |
| `w=<ms>` | `WRITE_TIMEOUT` | Write timeout in milliseconds |
| `s=<bytes>` | `READ_BUF_SIZE` | Receive buffer size in bytes |

```
# Full configuration for a flash chip on CS0, mode 0, 4 MHz
SPI.CONFIG d=/dev/spidev0.0 m=0 z=4000000 b=8 r=2000 w=2000 s=256

# Switch to CS1 with mode 3 at 8 MHz
SPI.CONFIG d=/dev/spidev0.1 m=3 z=8000000

# Change only the clock speed
SPI.CONFIG z=1000000

# Change only the read timeout
SPI.CONFIG r=5000
```

---

### CMD

Executes a single send/receive command over the SPI bus. The device is opened for the duration of the call and closed automatically when the command completes. Because SPI is full-duplex, `tout_write()` discards the RX bytes and `tout_read()` transmits `0x00` padding — this is handled transparently by the `SPI` driver.

```
SPI.CMD <expression>
```

See [CMD Expression Syntax](#cmd-expression-syntax) for the full grammar.

```
# Send a 4-byte command and read back a 4-byte response simultaneously
SPI.CMD > H"0102030405" | H"0000000000"

# Send a JEDEC ID request byte, expect the 3-byte device ID
SPI.CMD > H"9F" | H"000000"

# Transmit only — no response capture
SPI.CMD > H"06"

# Receive-only — clock in 32 bytes (TX = 0x00 padding)
SPI.CMD < F"capture.bin, 32"
```

---

### SCRIPT

Executes a multi-command script file from the `ARTEFACTS_PATH` directory. Each line contains one CMD expression. An optional inter-command delay (in milliseconds) can be specified.

```
SPI.SCRIPT <filename> [<delay>]
```

- `filename` — script file name, resolved relative to `ARTEFACTS_PATH`.
- `delay` — optional delay in milliseconds between command lines. Defaults to `0`.

```
# Run a flash initialisation sequence with no delay
SPI.SCRIPT flash_init.txt

# Run a register write sequence with 10 ms between each command
SPI.SCRIPT reg_write.txt 10

# Run a firmware update script with a 100 ms delay
SPI.SCRIPT fw_update.txt 100
```

---

## CMD Expression Syntax

The `CMD` command (and each line of a `SCRIPT` file) uses a structured expression grammar parsed by the `CommScriptCommandValidator` / `CommScriptCommandInterpreter` components.

### Direction Operators

| Operator | Description |
|---|---|
| `>` | **Send** — transmit data to the slave; RX bytes discarded |
| `<` | **Receive** — clock in data; TX bytes are `0x00` padding |

### Data Formats

| Format | Syntax | Direction | Description |
|---|---|---|---|
| Plain string | `Hello` | send / receive | Unquoted ASCII token |
| Quoted string | `"Hello\r\n"` | send / receive | Quoted ASCII string; escape sequences supported |
| Hex stream | `H"AABBCCDD"` | send / receive | Raw bytes as a hex string |
| File | `F"filename, size"` | send / receive | Binary file from `ARTEFACTS_PATH`; `size` = byte count for receive |

### Composite Expressions

```
> <send_data> | <expected_response>
< <expected_receive> | <response_to_send>
```

**Send then expect:**

```
# Write-enable command (0x06), then expect a status byte of 0x02
SPI.CMD > H"06" | H"02"

# Send a named command string, expect an ACK token
SPI.CMD > "START" | "ACK"

# Send a binary payload from a file, then expect a hex response
SPI.CMD > F"page.bin" | H"00"
```

**Receive then respond:**

```
# Wait for a ready token, then send a configuration block
SPI.CMD < H"AA55" | F"config.bin"

# Wait for a prompt string, then send a reply
SPI.CMD < "Ready?" | "Go!\r\n"
```

**Send or receive only:**

```
# Transmit only
SPI.CMD > H"FF"

# Receive only — clock in 64 bytes
SPI.CMD < F"capture.bin, 64"
```

---

## Script Files

Plain text files under `ARTEFACTS_PATH`, one CMD expression per line.

**Example script (`flash_init.txt`):**
```
> H"06"
> H"01" | H"00"
> H"9F" | H"000000"
< H"AA55" | H"06"
> F"firmware_page0.bin" | H"00"
```

Run it with:
```
SPI.SCRIPT flash_init.txt
SPI.SCRIPT flash_init.txt 10
```

---

## Fault-Tolerant and Privileged Modes

- **Fault-tolerant mode** (`isFaultTolerant()`): when set, the host framework continues after this plugin returns `false`.
- **Privileged mode** (`isPrivileged()`): always returns `false`. Reserved for future use.

---

## Error Handling and Return Values

Every command handler returns `bool`:
- `true` — command executed successfully, or argument validation passed in dry-run mode.
- `false` — argument validation failed, the SPI device could not be opened, a transfer timed out or returned an error, a script file was not found, or a memory allocation failure occurred.

Errors are emitted via `LOG_PRINT` at `LOG_ERROR` severity. Configuration issues are logged at `LOG_WARNING` or `LOG_VERBOSE`.
