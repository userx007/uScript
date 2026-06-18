# SLCAN Plugin

A C++ shared-library plugin that exposes a WeActStudio USB2CANFDV1-style SLCAN adapter (ASCII protocol over UART) through the same unified command dispatcher used by the KVCAN plugin. The plugin supports sending and receiving data over the CAN bus reachable through the adapter, using inline command expressions or external script files, and provides runtime reconfiguration of the UART device, CAN bus parameters and acceptance filters without reloading the plugin.

**Version:** 1.0.0.0

---

## Table of Contents

1. [Overview](#overview)
2. [Project Structure](#project-structure)
3. [Architecture](#architecture)
   - [Plugin Lifecycle](#plugin-lifecycle)
   - [Command Dispatch Model](#command-dispatch-model)
   - [INI Configuration Keys](#ini-configuration-keys)
   - [Why CMD Doesn't Use CommScriptCommandInterpreter](#why-cmd-doesnt-use-commscriptcommandinterpreter)
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

The plugin loads as a dynamic shared library (`.so`). The host application calls the exported C entry points `pluginEntry()` / `pluginExit()` to create and destroy the plugin object. Once loaded, the host passes configuration settings (UART device, CAN bus parameters, TX ID, filters, timeouts, buffer size) via `setParams()`, optionally calls `doInit()`, and then calls `doDispatch()` for every command it wants to execute.

All commands follow the pattern:

```
<PLUGIN>.<COMMAND> [arguments]
```

For example:

```
SLCAN.CONFIG i:/dev/ttyACM0 p:115200 b:6 x:0x123 r:2000 w:2000 s:64
SLCAN.FILTER 0x100:0x7FF,0x18DAF100:0x1FFFFFFF
SLCAN.CMD > H"AABBCCDD" | H"06"
SLCAN.SCRIPT obd_sequence.txt
```

---

## Project Structure

```
slcan_plugin/
├── CMakeLists.txt          # Build definition (shared library)
├── docs/
│   └── README.md           # This file
├── inc/
│   ├── slcan_plugin.hpp    # Class definition, command table, public accessors
│   └── private/
│       └── slcan_setup.hpp # CONFIG token-parsing helper (key:value → setter dispatch)
└── src/
    └── slcan_plugin.cpp    # Entry points, command handlers, init/cleanup, send/receive
```

---

## Architecture

### Plugin Lifecycle

```
pluginEntry()           → creates SLCANPlugin instance
  setParams()           → loads INI values (device, bus params, TX ID, filters, timeouts, buffer size)
  doInit()              → marks plugin as initialized (UART not opened yet)
  doEnable()            → enables real execution (without this, commands validate args only)
  doDispatch(cmd, args) → routes a command string to the correct handler
  doCleanup()           → marks plugin as uninitialized and disabled
pluginExit(ptr)         → deletes the SLCANPlugin instance
```

> **Note:** `doInit()` does not open the UART. The port — and the CAN channel itself — is opened on demand inside each `CMD` and `SCRIPT` call using RAII: `m_OpenAndConfigure()` constructs the `SLCAN` driver, pushes the configured bit rate / FD data rate / mode / auto-retransmission / filters (all of which the adapter only accepts while the channel is **closed**), opens the channel, and hands back the driver. When the `shared_ptr<SLCAN>` goes out of scope at the end of the call, `SLCAN`'s own destructor closes the channel and the port. A single plugin instance can switch devices, bit rates or IDs simply by calling `CONFIG` or `FILTER` between commands.

`doEnable()` controls a "dry-run / validation" mode: when not enabled, every command validates its arguments and returns `true` without performing any I/O.

### Command Dispatch Model

Commands are registered via a single-level `std::map` (`m_mapCmds`) populated in the constructor through an X-macro expansion — identical in shape to the KVCAN plugin:

```cpp
#define SLCAN_PLUGIN_COMMANDS_CONFIG_TABLE    \
SLCAN_PLUGIN_CMD_RECORD( INFO               ) \
SLCAN_PLUGIN_CMD_RECORD( CONFIG             ) \
SLCAN_PLUGIN_CMD_RECORD( FILTER             ) \
SLCAN_PLUGIN_CMD_RECORD( CMD                ) \
SLCAN_PLUGIN_CMD_RECORD( SCRIPT             )

// In the constructor:
#define SLCAN_PLUGIN_CMD_RECORD(a, ...) m_mapCmds.insert( std::make_pair( #a, \
    PluginCommandEntry<SLCANPlugin>{&SLCANPlugin::m_SLCAN_##a, SLCAN_GET_BLOCKING(a, ##__VA_ARGS__, false)} ));
SLCAN_PLUGIN_COMMANDS_CONFIG_TABLE
#undef SLCAN_PLUGIN_CMD_RECORD
```

Adding a new top-level command requires only a new entry in the config table and a corresponding handler implementation.

### INI Configuration Keys

The following keys are read from the host configuration/INI file at `setParams()` time:

| Key | Type | Description |
|---|---|---|
| `SLCAN_DEVICE` | string | UART device path (e.g. `/dev/ttyACM0`, `COM3`) |
| `SLCAN_UART_BAUD` | uint32 | UART baud rate used to talk to the adapter (not the CAN bus bit rate) |
| `SLCAN_BITRATE` | uint32 | Nominal CAN bit rate preset, 0–13 (`S0`–`SD`; 4 = 125 kbit/s, adapter default) |
| `SLCAN_FD_DATARATE` | uint32 | CAN-FD data segment bit rate preset, 1–5 (`Y1`–`Y5`; 2 = 2 Mbit/s, adapter default) |
| `SLCAN_MODE` | uint32 | Bus mode: 0 = normal (default), 1 = silent/listen-only |
| `SLCAN_AUTO_RETX` | uint32 | Auto-retransmission: 0 = off (default), 1 = on |
| `SLCAN_FD_BRS` | uint32 | Bit Rate Switch on outgoing CAN-FD frames: 0 = off, 1 = on (default) |
| `CAN_TX_ID` | uint32 | CAN ID stamped on outgoing frames; decimal or `0x`-prefixed hex. Add `0x80000000` for 29-bit extended IDs |
| `CAN_FILTERS` | string | Optional comma-separated acceptance filter list (see [FILTER](#filter)); empty = accept all |
| `READ_TIMEOUT` | uint32 | Per-read timeout in milliseconds |
| `WRITE_TIMEOUT` | uint32 | Per-write timeout in milliseconds |
| `READ_BUF_SIZE` | uint32 | Receive buffer size in bytes (max 64 for CAN FD, max 8 for classic CAN) |
| `ARTEFACTS_PATH` | string | Base directory from which script/file paths are resolved |

All of these values can also be overridden at runtime using `CONFIG` or `FILTER` without reloading the plugin.

### Why CMD Doesn't Use CommScriptCommandInterpreter

The KVCAN plugin's `CMD`/`SCRIPT` handlers hand the open driver straight to `CommScriptCommandInterpreter<KVCAN>` / `CommScriptClient<KVCAN>`, which talk to the driver only through the generic `ICommDriver::tout_write()`/`tout_read()` interface. That works for KVCAN because SocketCAN's frame construction (stamping the `can_id` configured via `set_tx_id()`) happens inside KVCAN's own `tout_write()`.

`uSlcan.cpp`'s `tout_write()`/`tout_read()` are a verbatim/raw byte passthrough — `xtra_params` is accepted but never used to build or parse a `CanFrame`. `uSlcan.hpp`'s own class comment says as much: *"the richer typed API (send_frame / receive_frame) is strongly preferred."* So rather than routing through `CommScriptCommandInterpreter<SLCAN>`/`CommScriptClient<SLCAN>`, this plugin's `m_Send()`/`m_Receive()` call `SLCAN::send_frame()`/`receive_frame()` directly, and a small self-contained interpreter (`m_ExecuteExpression`/`m_ParseDatum`/`m_ExpectReceive` in `slcan_plugin.cpp`) implements the exact same `"> data [| expect]"` / `"< expect [| reply]"` grammar documented for `KVCAN.CMD`, so usage from the operator's point of view is unchanged.

If your real `CommScriptCommandInterpreter`/`CommScriptClient` turn out to only require `is_open()`/`tout_write()`/`tout_read()` from the driver type (i.e. they're already frame-agnostic at that layer), the cleaner long-term fix is to make `uSlcan`'s generic interface frame-aware — using `xtra_params` for the TX id as its header already documents — and delete the local interpreter in favour of the shared one.

---

## Building

The plugin is built as a CMake shared library. It links against `uIPlugin`, `uPluginOps`, and `uSlcan`, which must be available in the CMake build tree. Unlike `kvcan_plugin`, it does **not** link `uCommScriptClient`, `uCommScriptCommandInterpreter`, or `uScriptReader` — see [Why CMD Doesn't Use CommScriptCommandInterpreter](#why-cmd-doesnt-use-commscriptcommandinterpreter).

```bash
mkdir build && cd build
cmake ..
make slcan_plugin
```

The output is `libslcan_plugin.so`.

---

## Command Reference

### INFO

Prints version information and a concise usage summary of all supported commands directly to the logger. Takes **no arguments** and works even if `doInit()` failed.

```
SLCAN.INFO
```

---

### CONFIG

Overrides the UART/CAN parameters at runtime. Any subset of parameters can be specified; omitted keys retain their current values.

```
SLCAN.CONFIG [i:<device>] [p:<uart_baud>] [b:<bitrate>] [y:<fd_rate>] [m:<mode>] [a:<auto_retx>] [z:<fd_brs>] [x:<tx_id>] [r:<read_timeout>] [w:<write_timeout>] [s:<recv_bufsize>]
```

| Token | INI key | Description |
|---|---|---|
| `i:<device>` | `SLCAN_DEVICE` | UART device path |
| `p:<baud>` | `SLCAN_UART_BAUD` | UART baud rate |
| `b:<0-13>` | `SLCAN_BITRATE` | CAN bit rate preset (`S0`-`SD`) |
| `y:<1-5>` | `SLCAN_FD_DATARATE` | CAN-FD data rate preset (`Y1`-`Y5`) |
| `m:<0\|1>` | `SLCAN_MODE` | Bus mode: normal / silent |
| `a:<0\|1>` | `SLCAN_AUTO_RETX` | Auto-retransmission off/on |
| `z:<0\|1>` | `SLCAN_FD_BRS` | CAN-FD Bit Rate Switch off/on |
| `x:<id>` | `CAN_TX_ID` | CAN ID for outgoing frames (decimal or `0x`-prefixed hex) |
| `r:<ms>` | `READ_TIMEOUT` | Read timeout in milliseconds |
| `w:<ms>` | `WRITE_TIMEOUT` | Write timeout in milliseconds |
| `s:<bytes>` | `READ_BUF_SIZE` | Receive buffer size in bytes |

```
# 500 kbit/s classic CAN over /dev/ttyACM0, standard 11-bit ID 0x123
SLCAN.CONFIG i:/dev/ttyACM0 p:115200 b:6 x:0x123 r:2000 w:2000 s:8

# 125 kbit/s (adapter default), extended 29-bit ID (ISO 15765-2 functional address)
SLCAN.CONFIG i:/dev/ttyACM0 b:4 x:0x18DB33F1

# Change only the read timeout
SLCAN.CONFIG r:5000

# Switch device and bit rate, keep other settings
SLCAN.CONFIG i:/dev/ttyACM1 b:8
```

---

### FILTER

Installs the adapter's acceptance filters via the SLCAN `f` (standard) and `F` (extended) commands. Unlike SocketCAN's arbitrary-length kernel filter list, the adapter exposes **exactly one standard and one extended filter slot**, so at most one entry of each kind is accepted. Filters are stored in the plugin state and re-applied — while the channel is closed, as the adapter requires — every time `CMD` or `SCRIPT` opens a new channel. Calling `FILTER` with an empty argument clears both slots (accept everything).

```
SLCAN.FILTER [<id>:<mask>[,<id>:<mask>]]
```

Both `id` and `mask` accept decimal or `0x`-prefixed hex values. An `id` above `0x7FF` is treated as extended automatically (with a warning) even without `0x80000000` set, mirroring `CONFIG`'s `x:` token.

```
# Accept only standard 11-bit ID 0x100
SLCAN.FILTER 0x100:0x7FF

# Accept standard ID 0x100 and extended ID 0x18DAF100 simultaneously
SLCAN.FILTER 0x100:0x7FF,0x18DAF100:0x1FFFFFFF

# Accept any standard ID in the range 0x700-0x7FF
SLCAN.FILTER 0x700:0x700

# Clear both filter slots — accept everything
SLCAN.FILTER
```

---

### CMD

Executes a single send/receive operation over the CAN bus. The UART is opened, bus parameters and filters are pushed, and the channel is opened for the duration of the call — everything closes automatically when the command completes (RAII).

Payload size constraints are enforced the same way as KVCAN: classic CAN frames carry at most **8 bytes**; CAN-FD frames carry at most **64 bytes**. The frame type (classic vs. FD) and standard-vs-extended addressing are both selected automatically — by payload size and by the configured TX ID, respectively.

```
SLCAN.CMD <expression>
```

```
# Transmit a 4-byte payload with the configured TX ID, expect a 1-byte ACK
SLCAN.CMD > H"DEADBEEF" | H"06"

# Send an OBD-II engine RPM request, expect a specific 3-byte response prefix
SLCAN.CMD > H"02010C00000000" | H"04620C"

# Receive-only — wait for a frame whose first 4 bytes match this template
SLCAN.CMD < H"00000000"

# Send only — no response expected
SLCAN.CMD > H"FF"
```

> **Note on receive matching:** an expected-receive token (`< token` or the `| token` after `>`) sets both the byte count to read **and** the content to compare against; a mismatch fails the command. This is stricter than "just a buffer-size hint" so that handshakes like the UDS seed/key exchange below are actually verified. Use an `F"name,size"` token instead of `H"..."`/`".."` if you only want to capture whatever bytes arrive (see [Data Formats](#data-formats)).

---

### SCRIPT

Executes a multi-command script file from the `ARTEFACTS_PATH` directory. The UART/CAN channel is opened once for the lifetime of the script. Each non-blank, non-`#`-comment line contains one `CMD` expression. An optional inter-command delay (in milliseconds) can be specified.

```
SLCAN.SCRIPT <filename> [<delay>]
```

```
# Run an OBD-II diagnostic sequence with no delay
SLCAN.SCRIPT obd_pids.txt

# Run a UDS session script with 10 ms between each frame
SLCAN.SCRIPT uds_session.txt 10

# Run a firmware update sequence with a 50 ms delay
SLCAN.SCRIPT fw_update.txt 50
```

---

## CMD Expression Syntax

The `CMD` command (and each line of a `SCRIPT` file) uses the same expression grammar as KVCAN, implemented here by a small self-contained interpreter (`m_ExecuteExpression` / `m_ParseDatum` / `m_ExpectReceive` in `slcan_plugin.cpp`) instead of `CommScriptCommandValidator`/`CommScriptCommandInterpreter` — see [Why CMD Doesn't Use CommScriptCommandInterpreter](#why-cmd-doesnt-use-commscriptcommandinterpreter).

### Direction Operators

| Operator | Description |
|---|---|
| `>` | **Send** — pack the parsed bytes into a CAN frame payload and transmit |
| `<` | **Receive** — wait for a CAN frame and compare/copy its payload |

### Data Formats

| Format | Syntax | Direction | Description |
|---|---|---|---|
| Plain string | `Hello` | send / receive | Unquoted ASCII token |
| Quoted string | `"Hello\r\n"` | send / receive | Quoted ASCII string; `\r \n \t \\ \"` escapes supported |
| Hex stream | `H"AABBCCDD"` | send / receive | Raw bytes as a hex string (max 8 or 64 bytes) |
| File | `F"filename"` (send) / `F"filename,size"` (receive) | send / receive | **Send:** binary file read from `ARTEFACTS_PATH`, its length is the payload. **Receive:** `size` sets the byte count to read; whatever is actually received is written to `ARTEFACTS_PATH/filename` instead of being content-compared |

### Composite Expressions

```
> <send_data> | <expected_response>
< <expected_receive> | <response_to_send>
```

**Send then expect:**

```
# Send OBD-II mode 01 PID 0C (engine RPM), expect a response starting 04 62 0C
SLCAN.CMD > H"02010C00000000" | H"04620C"

# Send a command token, expect an ACK byte
SLCAN.CMD > "START" | H"06"

# Send a binary file payload (<=8 bytes), then expect a hex token
SLCAN.CMD > F"request.bin" | H"AA"

# Send a request, capture whatever 8-byte response arrives into response.bin
SLCAN.CMD > H"3E00" | F"response.bin,8"
```

**Receive then respond:**

```
# Wait for a seed frame, then send a key frame (UDS Security Access)
SLCAN.CMD < H"6727" | H"272800000000"

# Wait for a prompt token, then send a reply
SLCAN.CMD < "Ready?" | "Go!\r\n"
```

**Send or receive only:**

```
# Transmit only
SLCAN.CMD > H"0201000000000000"

# Receive only — wait for one frame matching this 4-byte template
SLCAN.CMD < H"00000000"
```

---

## Script Files

Plain text files under `ARTEFACTS_PATH`, one CMD expression per line. Blank lines and lines beginning with `#` are skipped.

**Example script (`uds_session.txt`):**
```
# Start diagnostic session, extended
> H"0210030000000000" | H"5003"
> H"022703" | H"6727"
< H"6727" | H"272800000000"
> H"0229F101000000" | H"6929"
> F"ecu_data.bin" | H"7F2978"
```

Run it with:
```
SLCAN.SCRIPT uds_session.txt
SLCAN.SCRIPT uds_session.txt 10
```

---

## Fault-Tolerant and Privileged Modes

- **Fault-tolerant mode** (`isFaultTolerant()`): when set, the host framework continues after this plugin returns `false`.
- **Privileged mode** (`isPrivileged()`): always returns `false`. Reserved for future use.

---

## Error Handling and Return Values

Every command handler returns `bool`:
- `true` — command executed successfully, or argument validation passed in dry-run mode.
- `false` — argument validation failed, the UART or CAN channel could not be opened/configured, a send or receive timed out, a received frame's content didn't match what was expected, a filter string was malformed (or asked for a second filter of a kind that only has one slot), a script file was not found, or a memory allocation failure occurred.

Errors are emitted via `LOG_PRINT` at `LOG_ERROR` severity. Configuration and parameter loading issues are logged at `LOG_WARNING` or `LOG_VERBOSE`.
