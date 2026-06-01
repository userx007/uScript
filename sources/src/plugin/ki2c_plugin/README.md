# I2C Plugin

A C++ shared-library plugin that exposes a general-purpose I2C interface through a unified command dispatcher. The plugin supports sending and receiving data over a Linux I2C bus (`/dev/i2c-N`) using inline command expressions or external script files, and provides runtime reconfiguration of device parameters without reloading the plugin.

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

The plugin loads as a dynamic shared library (`.so`). The host application calls the exported C entry points `pluginEntry()` / `pluginExit()` to create and destroy the plugin object. Once loaded, the host passes configuration settings (device path, slave address, timeouts, buffer size) via `setParams()`, optionally calls `doInit()`, and then calls `doDispatch()` for every command it wants to execute.

All commands follow the pattern:

```
<PLUGIN>.<COMMAND> [arguments]
```

For example:

```
I2C.CONFIG d:/dev/i2c-1 a:0x48 r:2000 w:2000 s:256
I2C.CMD > H"012345" | H"06"
I2C.CMD < "Ready" | "Go!"
I2C.SCRIPT sensor_init.txt
```

---

## Project Structure

```
i2c_plugin/
├── CMakeLists.txt          # Build definition (shared library)
├── docs/
│   └── README.md           # This file
├── inc/
│   └── i2c_plugin.hpp      # Class definition, command table, public accessors
└── src/
    └── i2c_plugin.cpp      # Entry points, command handlers, init/cleanup, send/receive
```

The plugin is intentionally compact: a single implementation file handles all four commands, send/receive helpers, parameter loading, and the script engine integration.

---

## Architecture

### Plugin Lifecycle

```
pluginEntry()           → creates I2CPlugin instance
  setParams()           → loads INI values (device, address, timeouts, buffer size)
  doInit()              → marks plugin as initialized (no hardware opened yet)
  doEnable()            → enables real execution (without this, commands validate args only)
  doDispatch(cmd, args) → routes a command string to the correct handler
  doCleanup()           → marks plugin as uninitialized and disabled
pluginExit(ptr)         → deletes the I2CPlugin instance
```

> **Note:** `doInit()` does not open the I2C device. The device is opened on demand inside each `CMD` and `SCRIPT` call using RAII — the `I2C` driver object opens on construction and closes on destruction. This means a single plugin instance can address different devices or slave addresses across different commands simply by calling `CONFIG` between them.

`doEnable()` controls a "dry-run / validation" mode: when not enabled, every command validates its arguments and returns `true` without performing any I/O. This allows test frameworks to verify command syntax before the device is connected.

### Command Dispatch Model

Commands are registered via a single-level `std::map` (`m_mapCmds`) populated in the constructor through an X-macro expansion:

```cpp
#define I2C_PLUGIN_COMMANDS_CONFIG_TABLE    \
I2C_PLUGIN_CMD_RECORD( INFO               ) \
I2C_PLUGIN_CMD_RECORD( CONFIG             ) \
I2C_PLUGIN_CMD_RECORD( CMD                ) \
I2C_PLUGIN_CMD_RECORD( SCRIPT             )

// In the constructor:
#define I2C_PLUGIN_CMD_RECORD(a) \
    m_mapCmds.insert(std::make_pair(#a, &I2CPlugin::m_I2C_##a));
I2C_PLUGIN_COMMANDS_CONFIG_TABLE
#undef I2C_PLUGIN_CMD_RECORD
```

Adding a new top-level command requires only a new entry in the config table and a corresponding handler implementation.

### INI Configuration Keys

The following keys are read from the host configuration/INI file at `setParams()` time:

| Key | Type | Description |
|---|---|---|
| `I2C_DEVICE` | string | I2C bus device node (e.g. `/dev/i2c-1`) |
| `I2C_ADDRESS` | uint8 | 7-bit slave address, decimal or `0x`-prefixed hex (e.g. `0x48`, `72`) |
| `READ_TIMEOUT` | uint32 | Per-read timeout in milliseconds |
| `WRITE_TIMEOUT` | uint32 | Per-write timeout in milliseconds |
| `READ_BUF_SIZE` | uint32 | Receive buffer size in bytes (max 256 for classic I2C) |
| `ARTEFACTS_PATH` | string | Base directory from which script file paths are resolved |

All of these values can also be overridden at runtime using the `CONFIG` command without reloading the plugin.

---

## Building

The plugin is built as a CMake shared library. It links against `uIPlugin`, `uICoreScript`, `uCommScriptClient`, `uCommScriptCommandInterpreter`, `uScriptReader`, `uPluginOps`, and `uI2C`, which must be available in the CMake build tree.

```bash
mkdir build && cd build
cmake ..
make i2c_plugin
```

The output is `libi2c_plugin.so`.

---

## Command Reference

### INFO

Prints version information and a concise usage summary of all supported commands directly to the logger. This command takes **no arguments** and works even if `doInit()` failed (i.e., no hardware is required).

```
I2C.INFO
```

**Example output:**
```
I2C         | I2C Vers: 1.0.0.0
I2C         | Build: May 31 2026 ...
I2C         | Description: communicate with devices via I2C (/dev/i2c-N)
I2C         | CONFIG : set the I2C device, slave address and transfer parameters
I2C         |   Args : [d:device] [a:address] [r:read_tout] [w:write_tout] [s:recv_bufsize]
I2C         |   Usage: I2C.CONFIG d:/dev/i2c-1 a:0x48 r:2000 w:2000 s:256
I2C         | SCRIPT : send commands from a script file
I2C         |   Args : script
I2C         |   Usage: I2C.SCRIPT script.txt
I2C         | CMD    : send, receive or both
I2C         |   Args : direction message
I2C         |   Usage: I2C.CMD > H"AABBCCDD" | ok
I2C         |          I2C.CMD < "Please send!" | F"data.bin, 256"
```

---

### CONFIG

Overrides the I2C connection parameters at runtime. Any subset of parameters can be specified; omitted keys retain their current values. This is particularly useful when switching between different I2C buses or slave addresses within the same test sequence.

```
I2C.CONFIG [d:<device>] [a:<address>] [r:<read_timeout>] [w:<write_timeout>] [s:<recv_bufsize>]
```

| Token | INI key | Description |
|---|---|---|
| `d:<device>` | `I2C_DEVICE` | I2C bus device node path |
| `a:<address>` | `I2C_ADDRESS` | 7-bit slave address (decimal or `0x`-prefixed hex) |
| `r:<ms>` | `READ_TIMEOUT` | Read timeout in milliseconds |
| `w:<ms>` | `WRITE_TIMEOUT` | Write timeout in milliseconds |
| `s:<bytes>` | `READ_BUF_SIZE` | Receive buffer size in bytes |

```
# Full configuration for a temperature sensor at address 0x48 on bus 1
I2C.CONFIG d:/dev/i2c-1 a:0x48 r:2000 w:2000 s:256

# Switch to a different slave on the same bus
I2C.CONFIG a:0x50

# Switch bus entirely
I2C.CONFIG d:/dev/i2c-0 a:0x60

# Change only the read timeout
I2C.CONFIG r:5000
```

---

### CMD

Executes a single send/receive command over the I2C bus. The device is opened for the duration of the call and closed automatically when the command completes. The expression syntax supports sending strings or hex data, and receiving into fixed buffers, token-matched data, or line-terminated responses.

```
I2C.CMD <expression>
```

See [CMD Expression Syntax](#cmd-expression-syntax) for the full grammar.

```
# Write a register address byte, then read back 2 bytes
I2C.CMD > H"01" | H"0000"

# Send a command string and expect an acknowledgement token
I2C.CMD > "RESET" | "ACK"

# Wait to receive a specific trigger, then respond
I2C.CMD < H"AA55" | H"06"

# Send a raw hex payload with no expected response
I2C.CMD > H"DEADBEEF"
```

---

### SCRIPT

Executes a multi-command script file from the `ARTEFACTS_PATH` directory. Each line in the file contains one CMD expression. An optional inter-command delay (in milliseconds) can be specified.

```
I2C.SCRIPT <filename> [<delay>]
```

- `filename` — script file name, resolved relative to `ARTEFACTS_PATH`.
- `delay` — optional delay in milliseconds inserted between each command line. Defaults to `0`.

```
# Run a sensor initialisation sequence with no delay
I2C.SCRIPT sensor_init.txt

# Run a polling loop with 50 ms between each command
I2C.SCRIPT poll_registers.txt 50

# Run a firmware-update sequence with a 200 ms delay
I2C.SCRIPT fw_update.txt 200
```

---

## CMD Expression Syntax

The `CMD` command (and each line of a `SCRIPT` file) uses a structured expression grammar parsed by the `CommScriptCommandValidator` / `CommScriptCommandInterpreter` components.

### Direction Operators

The first token of an expression sets the transfer direction:

| Operator | Description |
|---|---|
| `>` | **Send** — transmit data to the slave |
| `<` | **Receive** — wait to receive data from the slave |

A `|` (pipe) separator follows the first operand and introduces the second operand (e.g., what to expect after sending, or what to send after receiving).

### Data Formats

| Format | Syntax | Direction | Description |
|---|---|---|---|
| Plain string | `Hello` | send / receive | Unquoted ASCII token; spaces end the token |
| Quoted string | `"Hello\r\n"` | send / receive | Quoted ASCII string; escape sequences supported |
| Hex stream | `H"AABBCCDD"` | send / receive | Raw bytes expressed as a hex string |
| File | `F"filename, size"` | send / receive | Binary file from `ARTEFACTS_PATH`; `size` is byte count for receive |

### Composite Expressions

The `|` pipe operator chains a send with a receive (or vice versa) in a single atomic command:

```
> <send_data> | <expected_response>
< <expected_receive> | <response_to_send>
```

**Send then expect:**

```
# Write register pointer 0x00, then read back the 2-byte result
I2C.CMD > H"00" | H"0000"

# Send a named command, expect an ACK token
I2C.CMD > "START" | "ACK"

# Send a binary payload file, expect a specific hex response
I2C.CMD > F"command.bin" | H"06"
```

**Receive then respond:**

```
# Wait for a ready signal, then send a configuration block
I2C.CMD < H"AA55" | F"config.bin"

# Wait for a prompt string, then send a reply
I2C.CMD < "Ready?" | "Go!\r\n"
```

**Send or receive only (no pipe):**

```
# Transmit only — no response expected
I2C.CMD > H"FF"

# Receive only — read 32 bytes into a file
I2C.CMD < F"capture.bin, 32"
```

---

## Script Files

Script files are plain text files stored under `ARTEFACTS_PATH`. Each non-empty line contains one CMD expression using the same syntax as the `CMD` command argument. Lines are executed sequentially by `CommScriptClient`. The optional `delay` argument to `SCRIPT` inserts a pause between each line.

**Example script (`sensor_init.txt`):**
```
> H"01" | H"0000"
> H"02" | H"0000"
> "CONFIG" | "ACK"
< H"AA55" | H"06"
> F"calibration.bin" | "DONE"
```

Run it with:
```
I2C.SCRIPT sensor_init.txt
I2C.SCRIPT sensor_init.txt 50
```

---

## Fault-Tolerant and Privileged Modes

- **Fault-tolerant mode** (`isFaultTolerant()`): when set, the host framework continues executing subsequent commands even after this plugin returns `false`. Useful in sequences where a non-response from the device should be logged but not abort the entire test.
- **Privileged mode** (`isPrivileged()`): always returns `false` in this plugin. Reserved for future use in the plugin framework.

---

## Error Handling and Return Values

Every command handler returns `bool`:
- `true` — command executed successfully, or argument validation passed in disabled (dry-run) mode.
- `false` — argument validation failed, the I2C device could not be opened, a send or receive operation timed out or returned an unexpected result, a script file was not found or was empty, or a memory allocation failure occurred.

Errors are emitted via `LOG_PRINT` at `LOG_ERROR` severity. Configuration and parameter loading issues are logged at `LOG_WARNING` or `LOG_VERBOSE`. The host application controls log verbosity through the shared `uLogger` configuration.
