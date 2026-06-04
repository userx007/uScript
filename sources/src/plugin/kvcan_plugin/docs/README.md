# CAN Plugin

A C++ shared-library plugin that exposes a general-purpose SocketCAN interface through a unified command dispatcher. The plugin supports sending and receiving data over any SocketCAN network interface — physical (`can0`, `can1` …) or virtual (`vcan0`, `vcan1` …) — using inline command expressions or external script files, and provides runtime reconfiguration of interface parameters and acceptance filters without reloading the plugin.

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
   - [FILTER](#filter)
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

The plugin loads as a dynamic shared library (`.so`). The host application calls the exported C entry points `pluginEntry()` / `pluginExit()` to create and destroy the plugin object. Once loaded, the host passes configuration settings (interface name, TX ID, filters, timeouts, buffer size) via `setParams()`, optionally calls `doInit()`, and then calls `doDispatch()` for every command it wants to execute.

All commands follow the pattern:

```
<PLUGIN>.<COMMAND> [arguments]
```

For example:

```
CAN.CONFIG i:vcan0 x:0x123 r:2000 w:2000 s:64
CAN.FILTER 0x100:0x7FF,0x200:0x7FF
CAN.CMD > H"AABBCCDD" | H"06"
CAN.SCRIPT obd_sequence.txt
```

---

## Project Structure

```
can_plugin/
├── CMakeLists.txt          # Build definition (shared library)
├── docs/
│   └── README.md           # This file
├── inc/
│   └── can_plugin.hpp      # Class definition, command table, public accessors
└── src/
    └── can_plugin.cpp      # Entry points, command handlers, init/cleanup, send/receive
```

---

## Architecture

### Plugin Lifecycle

```
pluginEntry()           → creates CANPlugin instance
  setParams()           → loads INI values (interface, TX ID, filters, timeouts, buffer size)
  doInit()              → marks plugin as initialized (no socket opened yet)
  doEnable()            → enables real execution (without this, commands validate args only)
  doDispatch(cmd, args) → routes a command string to the correct handler
  doCleanup()           → marks plugin as uninitialized and disabled
pluginExit(ptr)         → deletes the CANPlugin instance
```

> **Note:** `doInit()` does not open the CAN socket. The socket is opened on demand inside each `CMD` and `SCRIPT` call using RAII — the `CAN` driver object opens on construction and closes on destruction. The TX ID and acceptance filters stored in the plugin state are applied immediately after each open, so a single plugin instance can switch interfaces or IDs simply by calling `CONFIG` or `FILTER` between commands.

`doEnable()` controls a "dry-run / validation" mode: when not enabled, every command validates its arguments and returns `true` without performing any I/O.

### Command Dispatch Model

Commands are registered via a single-level `std::map` (`m_mapCmds`) populated in the constructor through an X-macro expansion:

```cpp
#define CAN_PLUGIN_COMMANDS_CONFIG_TABLE    \
CAN_PLUGIN_CMD_RECORD( INFO               ) \
CAN_PLUGIN_CMD_RECORD( CONFIG             ) \
CAN_PLUGIN_CMD_RECORD( FILTER             ) \
CAN_PLUGIN_CMD_RECORD( CMD                ) \
CAN_PLUGIN_CMD_RECORD( SCRIPT             )

// In the constructor:
#define CAN_PLUGIN_CMD_RECORD(a) \
    m_mapCmds.insert(std::make_pair(#a, &CANPlugin::m_CAN_##a));
CAN_PLUGIN_COMMANDS_CONFIG_TABLE
#undef CAN_PLUGIN_CMD_RECORD
```

Adding a new top-level command requires only a new entry in the config table and a corresponding handler implementation.

### INI Configuration Keys

The following keys are read from the host configuration/INI file at `setParams()` time:

| Key | Type | Description |
|---|---|---|
| `CAN_IFACE` | string | SocketCAN interface name (e.g. `vcan0`, `can1`) |
| `CAN_TX_ID` | uint32 | CAN ID stamped on outgoing frames; decimal or `0x`-prefixed hex. Add `0x80000000` for 29-bit extended IDs |
| `CAN_FILTERS` | string | Optional comma-separated acceptance filter list (see [FILTER](#filter)); empty = accept all |
| `READ_TIMEOUT` | uint32 | Per-read timeout in milliseconds |
| `WRITE_TIMEOUT` | uint32 | Per-write timeout in milliseconds |
| `READ_BUF_SIZE` | uint32 | Receive buffer size in bytes (max 64 for CAN FD, max 8 for classic CAN) |
| `ARTEFACTS_PATH` | string | Base directory from which script file paths are resolved |

All of these values can also be overridden at runtime using `CONFIG` or `FILTER` without reloading the plugin.

---

## Building

The plugin is built as a CMake shared library. It links against `uIPlugin`, `uICoreScript`, `uCommScriptClient`, `uCommScriptCommandInterpreter`, `uScriptReader`, `uPluginOps`, and `uKVCan`, which must be available in the CMake build tree.

```bash
mkdir build && cd build
cmake ..
make can_plugin
```

The output is `libcan_plugin.so`.

---

## Command Reference

### INFO

Prints version information and a concise usage summary of all supported commands directly to the logger. Takes **no arguments** and works even if `doInit()` failed.

```
CAN.INFO
```

---

### CONFIG

Overrides the CAN interface parameters at runtime. Any subset of parameters can be specified; omitted keys retain their current values.

```
CAN.CONFIG [i:<iface>] [x:<tx_id>] [r:<read_timeout>] [w:<write_timeout>] [s:<recv_bufsize>]
```

| Token | INI key | Description |
|---|---|---|
| `i:<iface>` | `CAN_IFACE` | SocketCAN interface name |
| `x:<id>` | `CAN_TX_ID` | CAN ID for outgoing frames (decimal or `0x`-prefixed hex) |
| `r:<ms>` | `READ_TIMEOUT` | Read timeout in milliseconds |
| `w:<ms>` | `WRITE_TIMEOUT` | Write timeout in milliseconds |
| `s:<bytes>` | `READ_BUF_SIZE` | Receive buffer size in bytes |

```
# Use virtual CAN, standard 11-bit ID 0x123
CAN.CONFIG i:vcan0 x:0x123 r:2000 w:2000 s:8

# Switch to physical CAN, extended 29-bit ID (ISO 15765-2 functional address)
CAN.CONFIG i:can0 x:0x18DB33F1

# Change only the read timeout
CAN.CONFIG r:5000

# Change interface and TX ID, keep other settings
CAN.CONFIG i:vcan1 x:0x456
```

---

### FILTER

Installs hardware acceptance filters on the SocketCAN socket via `setsockopt(SO_CAN_RAW_FILTER)`. Filters are stored in the plugin state and re-applied each time a `CMD` or `SCRIPT` opens a new socket. Calling `FILTER` with an empty argument removes all filters (accept everything).

```
CAN.FILTER [<id>:<mask>[,<id>:<mask>…]]
```

Both `id` and `mask` accept decimal or `0x`-prefixed hex values. The comparison performed by the kernel is: `received_id & mask == id & mask`.

```
# Accept only 11-bit ID 0x100
CAN.FILTER 0x100:0x7FF

# Accept IDs 0x100 and 0x200 (exact match on each)
CAN.FILTER 0x100:0x7FF,0x200:0x7FF

# Accept any ID in the range 0x700–0x7FF
CAN.FILTER 0x700:0x700

# Clear all filters — accept everything
CAN.FILTER
```

---

### CMD

Executes a single send/receive command over the CAN bus. The socket is opened for the duration of the call and closed automatically when the command completes. The TX ID and any configured filters are applied immediately after open.

Payload size constraints are enforced by the `CAN` driver: classic CAN frames carry at most **8 bytes**; CAN FD frames carry at most **64 bytes**. The driver selects the frame type automatically based on payload size.

```
CAN.CMD <expression>
```

```
# Transmit a 4-byte payload with ID 0x123, expect a 1-byte ACK
CAN.CMD > H"DEADBEEF" | H"06"

# Send an OBD-II engine RPM request, expect any response frame
CAN.CMD > H"02010C00000000" | H"04"

# Receive-only — wait for one frame into a fixed buffer
CAN.CMD < H"00000000"

# Send only — no response expected
CAN.CMD > H"FF"
```

---

### SCRIPT

Executes a multi-command script file from the `ARTEFACTS_PATH` directory. The CAN socket is opened once for the lifetime of the script. Each line contains one CMD expression. An optional inter-command delay (in milliseconds) can be specified.

```
CAN.SCRIPT <filename> [<delay>]
```

```
# Run an OBD-II diagnostic sequence with no delay
CAN.SCRIPT obd_pids.txt

# Run a UDS session script with 10 ms between each frame
CAN.SCRIPT uds_session.txt 10

# Run a firmware update sequence with a 50 ms delay
CAN.SCRIPT fw_update.txt 50
```

---

## CMD Expression Syntax

The `CMD` command (and each line of a `SCRIPT` file) uses a structured expression grammar parsed by the `CommScriptCommandValidator` / `CommScriptCommandInterpreter` components.

### Direction Operators

| Operator | Description |
|---|---|
| `>` | **Send** — pack buffer into a CAN frame payload and transmit |
| `<` | **Receive** — wait for a CAN frame and copy its payload into the buffer |

### Data Formats

| Format | Syntax | Direction | Description |
|---|---|---|---|
| Plain string | `Hello` | send / receive | Unquoted ASCII token |
| Quoted string | `"Hello\r\n"` | send / receive | Quoted ASCII string; escape sequences supported |
| Hex stream | `H"AABBCCDD"` | send / receive | Raw bytes as a hex string (max 8 or 64 bytes) |
| File | `F"filename, size"` | send / receive | Binary file from `ARTEFACTS_PATH`; `size` = byte count for receive |

### Composite Expressions

```
> <send_data> | <expected_response>
< <expected_receive> | <response_to_send>
```

**Send then expect:**

```
# Send OBD-II mode 01 PID 0C (engine RPM), expect a 4-byte response
CAN.CMD > H"02010C00000000" | H"04620C"

# Send a command token, expect an ACK byte
CAN.CMD > "START" | H"06"

# Send a binary file payload (≤8 bytes), then expect a hex token
CAN.CMD > F"request.bin" | H"AA"
```

**Receive then respond:**

```
# Wait for a seed frame, then send a key frame
CAN.CMD < H"6727" | H"272800000000"

# Wait for a prompt token, then send a reply
CAN.CMD < "Ready?" | "Go!\r\n"
```

**Send or receive only:**

```
# Transmit only
CAN.CMD > H"0201000000000000"

# Receive only — wait for one frame
CAN.CMD < H"00000000"
```

---

## Script Files

Plain text files under `ARTEFACTS_PATH`, one CMD expression per line.

**Example script (`uds_session.txt`):**
```
> H"0210030000000000" | H"5003"
> H"022703" | H"6727"
< H"6727" | H"272800000000"
> H"0229F101000000" | H"6929"
> F"ecu_data.bin" | H"7F2978"
```

Run it with:
```
CAN.SCRIPT uds_session.txt
CAN.SCRIPT uds_session.txt 10
```

---

## Fault-Tolerant and Privileged Modes

- **Fault-tolerant mode** (`isFaultTolerant()`): when set, the host framework continues after this plugin returns `false`.
- **Privileged mode** (`isPrivileged()`): always returns `false`. Reserved for future use.

---

## Error Handling and Return Values

Every command handler returns `bool`:
- `true` — command executed successfully, or argument validation passed in dry-run mode.
- `false` — argument validation failed, the CAN socket could not be opened or bound, a send or receive timed out, a filter string was malformed, a script file was not found, or a memory allocation failure occurred.

Errors are emitted via `LOG_PRINT` at `LOG_ERROR` severity. Configuration and parameter loading issues are logged at `LOG_WARNING` or `LOG_VERBOSE`.
