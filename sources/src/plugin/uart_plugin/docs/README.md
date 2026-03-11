# UART Plugin

A C++ shared-library plugin that exposes a general-purpose UART serial interface through a unified command dispatcher. The plugin supports sending and receiving data over a serial port using inline command expressions or external script files, and provides runtime reconfiguration of port parameters without reloading the plugin.

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

The plugin loads as a dynamic shared library (`.so` / `.dll`). The host application calls the exported C entry points `pluginEntry()` / `pluginExit()` to create and destroy the plugin object. Once loaded, the host passes configuration settings (serial port, baud rate, timeouts, buffer size…) via `setParams()`, optionally calls `doInit()`, and then calls `doDispatch()` for every command it wants to execute.

All commands follow the pattern:

```
<PLUGIN>.<COMMAND> [arguments]
```

For example:

```
UART.CONFIG p:/dev/ttyUSB0 b:115200 r:2000 w:2000 s:1024
UART.CMD > "AT\r\n" | "OK"
UART.CMD < "Please send!" | Sending...
UART.SCRIPT handshake.txt
```

---

## Project Structure

```
uart_plugin/
├── CMakeLists.txt          # Build definition (shared library)
├── inc/
│   └── uart_plugin.hpp     # Class definition, command table, public accessors
└── src/
    └── uart_plugin.cpp     # Entry points, command handlers, init/cleanup, send/receive
```

The plugin is intentionally compact: a single implementation file handles all four commands, send/receive helpers, parameter loading, and the script engine integration.

---

## Architecture

### Plugin Lifecycle

```
pluginEntry()           → creates UARTPlugin instance
  setParams()           → loads INI values (port, baud rate, timeouts, buffer size...)
  doInit()              → marks plugin as initialized (no hardware opened yet)
  doEnable()            → enables real execution (without this, commands validate args only)
  doDispatch(cmd, args) → routes a command string to the correct handler
  doCleanup()           → marks plugin as uninitialized and disabled
pluginExit(ptr)         → deletes the UARTPlugin instance
```

> **Note:** Unlike many other plugins, `doInit()` does not open the UART port. The port is opened on demand inside each `CMD` and `SCRIPT` call using RAII — the `UART` driver object opens on construction and closes on destruction. This means a single plugin instance can address different ports across different commands simply by calling `CONFIG` between them.

`doEnable()` controls a "dry-run / validation" mode: when not enabled, every command validates its arguments and returns `true` without performing any I/O. This allows test frameworks to verify command syntax before the device is connected.

### Command Dispatch Model

Commands are registered via a single-level `std::map` (`m_mapCmds`) populated in the constructor through an X-macro expansion:

```cpp
#define UART_PLUGIN_COMMANDS_CONFIG_TABLE    \
UART_PLUGIN_CMD_RECORD( INFO               ) \
UART_PLUGIN_CMD_RECORD( CONFIG             ) \
UART_PLUGIN_CMD_RECORD( CMD                ) \
UART_PLUGIN_CMD_RECORD( SCRIPT             )

// In the constructor:
#define UART_PLUGIN_CMD_RECORD(a) \
    m_mapCmds.insert(std::make_pair(#a, &UARTPlugin::m_UART_##a));
UART_PLUGIN_COMMANDS_CONFIG_TABLE
#undef UART_PLUGIN_CMD_RECORD
```

Adding a new top-level command requires only a new entry in the config table and a corresponding handler implementation.

The `CMD` command delegates argument parsing and execution to the `CommScriptCommandValidator` and `CommScriptCommandInterpreter` framework components, which handle the full send/receive expression grammar. The `SCRIPT` command similarly delegates multi-command file execution to `CommScriptClient`.

### INI Configuration Keys

The following keys are read from the host configuration/INI file at `setParams()` time:

| Key | Type | Description |
|---|---|---|
| `UART_PORT` | string | Serial port path (e.g., `/dev/ttyUSB0`, `COM3`) |
| `BAUDRATE` | uint32 | Baud rate (e.g., `9600`, `115200`, `921600`) |
| `READ_TIMEOUT` | uint32 | Per-read timeout in milliseconds |
| `WRITE_TIMEOUT` | uint32 | Per-write timeout in milliseconds |
| `READ_BUF_SIZE` | uint32 | Receive buffer size in bytes |
| `READ_BUF_TIMEOUT` | uint32 | Buffer-drain timeout for bulk receive operations |
| `ARTEFACTS_PATH` | string | Base directory from which script file paths are resolved |

All of these values can also be overridden at runtime using the `CONFIG` command without reloading the plugin.

---

## Building

The plugin is built as a CMake shared library. It links against `uIPlugin`, `uICoreScript`, `uCommScriptClient`, `uCommScriptCommandInterpreter`, `uScriptReader`, `uPluginOps`, and `uUart`, which must be available in the CMake build tree.

```bash
mkdir build && cd build
cmake ..
make uart_plugin
```

The output is `libuart_plugin.so` (Linux) or `uart_plugin.dll` (Windows).

---

## Command Reference

### INFO

Prints version information and a concise usage summary of all supported commands directly to the logger. This command takes **no arguments** and works even if `doInit()` failed (i.e., no hardware is required).

```
UART.INFO
```

**Example output:**
```
UART_PLUGIN| Version: 1.0.0.0
UART_PLUGIN| Description: communicate with other apps/devices via UART
UART_PLUGIN| CONFIG : overwrite the default UART port
UART_PLUGIN|   Args : [p:port] [b:baudrate] [r:read_tout] [w:write_tout] [s:recv_bufsize]
UART_PLUGIN|   Usage: UART.CONFIG p:COM2 b:115200 r:2000 w:2000 s:1024
UART_PLUGIN| SCRIPT : send commands from a file
UART_PLUGIN|   Args : script
UART_PLUGIN|   Usage: UART.SCRIPT script.txt
UART_PLUGIN| CMD    : send, receive or both
UART_PLUGIN|   Args : direction message
UART_PLUGIN|   Usage: UART.CMD > H"AABBCCDD" | ok
UART_PLUGIN|          UART.CMD < "Please send!" | F"data.bin, 1024"
```

---

### CONFIG

Overrides the UART connection parameters at runtime. Any subset of parameters can be specified; omitted keys retain their current values. This is particularly useful when switching between different serial ports or baud rates within the same test sequence.

```
UART.CONFIG [p:<port>] [b:<baudrate>] [r:<read_timeout>] [w:<write_timeout>] [s:<recv_bufsize>]
```

| Token | INI key | Description |
|---|---|---|
| `p:<port>` | `UART_PORT` | Serial port path or name |
| `b:<baudrate>` | `BAUDRATE` | Baud rate |
| `r:<ms>` | `READ_TIMEOUT` | Read timeout in milliseconds |
| `w:<ms>` | `WRITE_TIMEOUT` | Write timeout in milliseconds |
| `s:<bytes>` | `READ_BUF_SIZE` | Receive buffer size in bytes |

```
# Full reconfiguration for a Windows virtual COM port
UART.CONFIG p:COM2 b:115200 r:2000 w:2000 s:1024

# Linux USB serial device at a different baud rate
UART.CONFIG p:/dev/ttyUSB0 b:921600

# Change only the read timeout
UART.CONFIG r:5000

# Switch port and buffer size, keep other settings
UART.CONFIG p:/dev/ttyACM1 s:2048
```

---

### CMD

Executes a single send/receive command over the UART port. The port is opened for the duration of the call and closed automatically when the command completes. The expression syntax supports sending strings, hex data, or files, and receiving into fixed buffers, token-matched data, or line-terminated responses.

```
UART.CMD <expression>
```

See [CMD Expression Syntax](#cmd-expression-syntax) for the full grammar.

```
# Send "AT\r\n" and expect to read back "OK"
UART.CMD > "AT\r\n" | "OK"

# Wait to receive the string "Please send!" then transmit "Sending..."
UART.CMD < "Please send!" | Sending...

# Send a raw hex stream with no expected response
UART.CMD > H"AABBCCDD"

# Send a file and capture the response into another file
UART.CMD > F"command.bin" | F"response.bin, 1024"
```

---

### SCRIPT

Executes a multi-command script file from the `ARTEFACTS_PATH` directory. Each line in the file contains one CMD expression. An optional inter-command delay (in milliseconds) can be specified.

```
UART.SCRIPT <filename> [<delay>]
```

- `filename` — script file name, resolved relative to `ARTEFACTS_PATH`.
- `delay` — optional delay in milliseconds inserted between each command line. Defaults to `0`.

```
# Run a script with no delay between commands
UART.SCRIPT handshake.txt

# Run a script with a 100 ms delay between each command
UART.SCRIPT modem_init.txt 100

# Run a longer initialization sequence with a 500 ms delay
UART.SCRIPT firmware_update.txt 500
```

---

## CMD Expression Syntax

The `CMD` command (and each line of a `SCRIPT` file) uses a structured expression grammar parsed by the `CommScriptCommandValidator` / `CommScriptCommandInterpreter` components.

### Direction Operators

The first token of an expression sets the transfer direction:

| Operator | Description |
|---|---|
| `>` | **Send** — transmit data to the device |
| `<` | **Receive** — wait to receive data from the device |

A `|` (pipe) separator follows the first operand and introduces the second operand, which describes the counterpart operation (e.g., what to expect after sending, or what to send after receiving).

### Data Formats

The following data formats are supported as operands:

| Format | Syntax | Direction | Description |
|---|---|---|---|
| Plain string | `Hello` | send / receive | Unquoted ASCII token; spaces end the token |
| Quoted string | `"Hello World\r\n"` | send / receive | Quoted ASCII string; escape sequences supported |
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
# Send a string literal, then expect a plain token response
UART.CMD > "AT\r\n" | "OK\r\n"

# Send a hex command byte, then expect a hex acknowledgement
UART.CMD > H"01" | H"06"

# Send a file payload, then expect a specific response token
UART.CMD > F"payload.bin" | "ACK"
```

**Receive then respond:**

```
# Wait for a specific prompt string, then send a response
UART.CMD < "login: " | "admin\r\n"

# Wait to receive a hex token, then send a file
UART.CMD < H"AA55" | F"firmware.bin"

# Wait for a prompt, then send a plain reply
UART.CMD < "Ready?" | "Go!\r\n"
```

**Send or receive only (no pipe):**

```
# Transmit only — no response expected
UART.CMD > "RESET\r\n"

# Receive only — read into a file (1024 bytes)
UART.CMD < F"capture.bin, 1024"
```

---

## Script Files

Script files are plain text files stored under `ARTEFACTS_PATH`. Each non-empty line contains one CMD expression using the same syntax as the `CMD` command argument. Lines are executed sequentially by `CommScriptClient`. The optional `delay` argument to `SCRIPT` inserts a pause between each line.

**Example script (`handshake.txt`):**
```
> "AT\r\n" | "OK\r\n"
> "AT+GMR\r\n" | "version"
< "Ready" | "GO\r\n"
> H"01020304" | H"06"
```

Run it with:
```
UART.SCRIPT handshake.txt
UART.SCRIPT handshake.txt 50
```

---

## Fault-Tolerant and Privileged Modes

- **Fault-tolerant mode** (`setFaultTolerant()` / `isFaultTolerant()`): when set, the host framework continues executing subsequent commands even after this plugin returns `false`. Useful in sequences where a non-response from the device should be logged but not abort the entire test.
- **Privileged mode** (`isPrivileged()`): always returns `false` in this plugin. Reserved for future use in the plugin framework.

---

## Error Handling and Return Values

Every command handler returns `bool`:
- `true` — command executed successfully, or argument validation passed in disabled (dry-run) mode.
- `false` — argument validation failed, the UART port could not be opened, a send or receive operation timed out or returned an unexpected result, a script file was not found or was empty, or a memory allocation failure occurred.

Errors are emitted via `LOG_PRINT` at `LOG_ERROR` severity. Configuration and parameter loading issues are logged at `LOG_WARNING` or `LOG_VERBOSE`. The host application controls log verbosity through the shared `uLogger` configuration.
