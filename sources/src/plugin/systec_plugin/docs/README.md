# SYSTEC CAN Plugin

A C++ shared-library plugin that exposes SocketCAN interfaces registered by SYS TEC electronic's `systec_can.ko` kernel driver (USB-CANmodul family) through the unified command dispatcher used across this codebase's CAN plugins. The plugin supports sending and receiving data over any SocketCAN network interface the driver creates — physical (`can0`, `can1` …) or, for testing, virtual (`vcan0`, `vcan1` …) — using inline command expressions or external script files, plus runtime reconfiguration of interface parameters, acceptance filters, and SYS TEC-specific hardware controls without reloading the plugin.

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
   - [HWCTRL](#hwctrl)
   - [CMD](#cmd)
   - [SCRIPT](#script)
   - [CYCLIC](#cyclic)
6. [CMD Expression Syntax](#cmd-expression-syntax)
7. [Script Files](#script-files)
8. [Why Classic CAN Only](#why-classic-can-only)
9. [Fault-Tolerant and Privileged Modes](#fault-tolerant-and-privileged-modes)
10. [Error Handling and Return Values](#error-handling-and-return-values)

---

## Overview

The plugin loads as a dynamic shared library (`.so`). The host application calls the exported C entry points `pluginEntry()` / `pluginExit()` to create and destroy the plugin object. Once loaded, the host passes configuration settings (interface name, TX ID, filters, timeouts, buffer size) via `setParams()`, optionally calls `doInit()`, and then calls `doDispatch()` for every command it wants to execute.

All commands follow the pattern:

```
SYSTEC.<COMMAND> [arguments]
```

For example:

```
SYSTEC.CONFIG i=can0 x=0x123 r=2000 w=2000 s=8
SYSTEC.FILTER 0x100:0x7FF,0x200:0x7FF
SYSTEC.HWCTRL status_timeout=1500
SYSTEC.CMD > H"AABBCCDD" | H"06"
SYSTEC.SCRIPT obd_sequence.txt
```

---

## Project Structure

```
systec_plugin/
├── CMakeLists.txt          # Build definition (shared library)
├── docs/
│   └── README.md           # This file
├── inc/
│   ├── systec_plugin.hpp   # Class definition, command table, public accessors
│   └── private/
│       └── systec_setup.hpp# CONFIG key table (generic_can_set_params)
├── src/
│   └── systec_plugin.cpp   # Entry points, command handlers, init/cleanup, send/receive
└── test/                   # vcan-based smoke test harness (see test/README.md)
```

The plugin sits on top of a dedicated driver library, `uSystecCan` (`sources/src/lib/drivers/systec/`), which wraps the raw SocketCAN socket API plus SYS TEC's hardware-specific sysfs controls — analogous to how `kvcan_plugin` sits on `uKVCan` and `pcan_plugin` sits on `uPcan`.

---

## Architecture

### Plugin Lifecycle

```
pluginEntry()           → creates SYSTECPlugin instance
  setParams()           → loads INI values (interface, TX ID, filters, timeouts, buffer size)
  doInit()              → marks plugin as initialized (no socket opened yet)
  doEnable()            → enables real execution (without this, commands validate args only)
  doDispatch(cmd, args) → routes a command string to the correct handler
  doCleanup()           → marks plugin as uninitialized and disabled
pluginExit(ptr)         → deletes the SYSTECPlugin instance
```

> **Note:** `doInit()` does not open the CAN socket. The socket is opened on demand inside each `CMD`, `SCRIPT` and `CYCLIC` call using RAII — the `SYSTECCAN` driver object opens on construction and closes on destruction. The TX ID and acceptance filters stored in the plugin state are applied immediately after each open, so a single plugin instance can switch interfaces or IDs simply by calling `CONFIG` or `FILTER` between commands. `HWCTRL`, by contrast, talks to sysfs directly and needs no open socket at all.

`doEnable()` controls a "dry-run / validation" mode: when not enabled, every command validates its arguments and returns `true` without performing any I/O.

### Command Dispatch Model

Commands are registered via a single-level `std::map` (`m_mapCmds`) populated in the constructor through an X-macro expansion:

```cpp
#define SYSTEC_PLUGIN_COMMANDS_CONFIG_TABLE    \
SYSTEC_PLUGIN_CMD_RECORD( INFO               ) \
SYSTEC_PLUGIN_CMD_RECORD( CONFIG             ) \
SYSTEC_PLUGIN_CMD_RECORD( FILTER             ) \
SYSTEC_PLUGIN_CMD_RECORD( HWCTRL             ) \
SYSTEC_PLUGIN_CMD_RECORD( CMD                ) \
SYSTEC_PLUGIN_CMD_RECORD( SCRIPT             ) \
SYSTEC_PLUGIN_CMD_RECORD( CYCLIC             )

// In the constructor:
#define SYSTEC_PLUGIN_CMD_RECORD(a) \
    m_mapCmds.insert(std::make_pair(#a, &SYSTECPlugin::m_SYSTEC_##a));
SYSTEC_PLUGIN_COMMANDS_CONFIG_TABLE
#undef SYSTEC_PLUGIN_CMD_RECORD
```

Adding a new top-level command requires only a new entry in the config table and a corresponding handler implementation.

### INI Configuration Keys

The following keys are read from the host configuration/INI file at `setParams()` time:

| Key | Type | Description |
|---|---|---|
| `CAN_IFACE` | string | SocketCAN interface name registered by `systec_can.ko` (e.g. `can0`, `can1`) |
| `CAN_TX_ID` | uint32 | CAN ID stamped on outgoing frames; decimal or `0x`-prefixed hex. Add `0x80000000` for 29-bit extended IDs |
| `CAN_FILTERS` | string | Optional comma-separated acceptance filter list (see [FILTER](#filter)); empty = accept all |
| `READ_TIMEOUT` | uint32 | Per-read timeout in milliseconds |
| `WRITE_TIMEOUT` | uint32 | Per-write timeout in milliseconds |
| `READ_BUF_SIZE` | uint32 | Receive buffer size in bytes (**max 8** — `systec_can.ko` is classic CAN only, no CAN FD) |
| `ARTEFACTS_PATH` | string | Base directory from which script file paths are resolved |

All of these values can also be overridden at runtime using `CONFIG` or `FILTER` without reloading the plugin.

---

## Building

The plugin is built as a CMake shared library. It links against `uIPlugin`, `uICoreScript`, `uCommScriptClient`, `uCommScriptCommandInterpreter`, `uScriptReader`, `uPluginOps`, and `uSystecCan`, which must be available in the CMake build tree.

```bash
mkdir build && cd build
cmake ..
make systec_plugin
```

The output is `libsystec_plugin.so`.

---

## Command Reference

### INFO

Prints version information and a concise usage summary of all supported commands directly to the logger. Takes **no arguments** and works even if `doInit()` failed.

```
SYSTEC.INFO
```

---

### CONFIG

Overrides the CAN interface parameters at runtime. Any subset of parameters can be specified; omitted keys retain their current values.

```
SYSTEC.CONFIG [i=<iface>] [x=<tx_id>] [r=<read_timeout>] [w=<write_timeout>] [s=<recv_bufsize>]
```

| Token | INI key | Description |
|---|---|---|
| `i=<iface>` | `CAN_IFACE` | SocketCAN interface name |
| `x=<id>` | `CAN_TX_ID` | CAN ID for outgoing frames (decimal or `0x`-prefixed hex) |
| `r=<ms>` | `READ_TIMEOUT` | Read timeout in milliseconds |
| `w=<ms>` | `WRITE_TIMEOUT` | Write timeout in milliseconds |
| `s=<bytes>` | `READ_BUF_SIZE` | Receive buffer size in bytes (1–8) |

```
# Use physical CAN, standard 11-bit ID 0x123
SYSTEC.CONFIG i=can0 x=0x123 r=2000 w=2000 s=8

# Switch to the second channel of a dual-channel USB-CANmodul, extended 29-bit ID
SYSTEC.CONFIG i=can1 x=0x18DB33F1

# Change only the read timeout
SYSTEC.CONFIG r=5000

# Change interface and TX ID, keep other settings
SYSTEC.CONFIG i=can0 x=0x456
```

---

### FILTER

Installs hardware acceptance filters on the SocketCAN socket via `setsockopt(SO_CAN_RAW_FILTER)`. Filters are stored in the plugin state and re-applied each time a `CMD`, `SCRIPT` or `CYCLIC` opens a new socket. Calling `FILTER` with an empty argument removes all filters (accept everything).

```
SYSTEC.FILTER [<id>:<mask>[,<id>:<mask>…]]
```

Both `id` and `mask` accept decimal or `0x`-prefixed hex values. The comparison performed by the kernel is: `received_id & mask == id & mask`.

```
# Accept only 11-bit ID 0x100
SYSTEC.FILTER 0x100:0x7FF

# Accept IDs 0x100 and 0x200 (exact match on each)
SYSTEC.FILTER 0x100:0x7FF,0x200:0x7FF

# Clear all filters — accept everything
SYSTEC.FILTER
```

---

### HWCTRL

Gets or sets SYS TEC device- and channel-specific hardware controls exposed by `systec_can.ko` through sysfs — there is no SocketCAN-generic equivalent for these, so this command talks to sysfs directly rather than the `CAN_RAW` socket, and needs no open connection. `CONFIG`'s `i=<iface>` must be set first.

```
SYSTEC.HWCTRL <key>[=<value>]
```

| Key | Scope | Access | Description |
|---|---|---|---|
| `devicenr` | device (both channels) | rw | USB-CANmodul device number, 0–254 |
| `reset` | device | write-only trigger | Issues `USBCAN_CMD_RESET_HW` to the hardware |
| `dual_channel` | device | ro | `1` if this is a dual-channel unit, else `0` |
| `status_timeout` | device | rw | Status-poll timeout, milliseconds |
| `high_performance` | device | rw | High-performance mode flag (`0`/`1`) |
| `channel` | this netdev | ro | Channel index (0 or 1) of `i=<iface>` on its device |
| `tx_timeout_ms` | this netdev | rw | TX timeout, milliseconds — **dual-channel units only** |

```
# Read the device number
SYSTEC.HWCTRL devicenr

# Assign device number 5
SYSTEC.HWCTRL devicenr=5

# Power-cycle the USB-CANmodul
SYSTEC.HWCTRL reset

# Check whether this is a dual-channel unit
SYSTEC.HWCTRL dual_channel

# Read/write the status timeout
SYSTEC.HWCTRL status_timeout
SYSTEC.HWCTRL status_timeout=1500
```

`devicenr`, `reset`, `status_timeout` and `high_performance` are **device-scoped**: on a dual-channel unit they affect both channels' netdevs at once, since they live under the shared USB interface's sysfs node rather than under either individual `can*` netdev.

---

### CMD

Executes a single send/receive command over the CAN bus. The socket is opened for the duration of the call and closed automatically when the command completes. The TX ID and any configured filters are applied immediately after open.

Payload size is capped at **8 bytes**: `systec_can.ko` is classic-CAN only (see [Why Classic CAN Only](#why-classic-can-only)).

```
SYSTEC.CMD <expression>
```

```
# Transmit a 4-byte payload with ID 0x123, expect a 1-byte ACK
SYSTEC.CMD > H"DEADBEEF" | H"06"

# Send an OBD-II engine RPM request, expect any response frame
SYSTEC.CMD > H"02010C00000000" | H"04"

# Receive-only — wait for one frame into a fixed buffer
SYSTEC.CMD < H"00000000"

# Send only — no response expected
SYSTEC.CMD > H"FF"
```

---

### SCRIPT

Executes a multi-command script file from the `ARTEFACTS_PATH` directory. The CAN socket is opened once for the lifetime of the script. Each line contains one CMD expression. An optional inter-command delay (in milliseconds) can be specified.

```
SYSTEC.SCRIPT <filename> [<delay>]
```

```
# Run an OBD-II diagnostic sequence with no delay
SYSTEC.SCRIPT obd_pids.txt

# Run a UDS session script with 10 ms between each frame
SYSTEC.SCRIPT uds_session.txt 10
```

---

### CYCLIC

Sends one or more periodic CAN messages in the background (see the CYCLIC section of `SYSTEC.INFO` for the exact argument grammar used by this codebase's command interpreter).

```
SYSTEC.CYCLIC <expression>
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
| Hex stream | `H"AABBCCDD"` | send / receive | Raw bytes as a hex string (max 8 bytes) |
| File | `F"filename, size"` | send / receive | Binary file from `ARTEFACTS_PATH`; `size` = byte count for receive |

### Composite Expressions

```
> <send_data> | <expected_response>
< <expected_receive> | <response_to_send>
```

**Send then expect:**
```
SYSTEC.CMD > H"02010C00000000" | H"04620C"
SYSTEC.CMD > "START" | H"06"
```

**Receive then respond:**
```
SYSTEC.CMD < H"6727" | H"272800000000"
SYSTEC.CMD < "Ready?" | "Go!\r\n"
```

**Send or receive only:**
```
SYSTEC.CMD > H"0201000000000000"
SYSTEC.CMD < H"00000000"
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
```

Run it with:
```
SYSTEC.SCRIPT uds_session.txt
SYSTEC.SCRIPT uds_session.txt 10
```

---

## Why Classic CAN Only

Unlike `kvcan_plugin` (which enables `CAN_RAW_FD_FRAMES` and supports up to 64-byte payloads), this plugin never enables CAN FD. That mirrors a hardware/driver fact, not a plugin limitation: `systec_can.c` sets

```c
chan->can.ctrlmode_supported = CAN_CTRLMODE_3_SAMPLES | CAN_CTRLMODE_LISTENONLY;
/* … | CAN_CTRLMODE_ONE_SHOT for some variants … */
```

and never adds `CAN_CTRLMODE_FD`, nor raises `netdev->mtu` past `CAN_MTU`. Every `can*` interface `systec_can.ko` registers is therefore classic-CAN-only at the kernel level; the `uSystecCan` driver's `CAN_DRV_MAX_DLEN = 8` reflects that, and `READ_BUF_SIZE`/`s=` are capped accordingly.

---

## Fault-Tolerant and Privileged Modes

- **Fault-tolerant mode** (`isFaultTolerant()`): when set, the host framework continues after this plugin returns `false`.
- **Privileged mode** (`isPrivileged()`): always returns `false`. Reserved for future use.

---

## Error Handling and Return Values

Every command handler returns `bool`:
- `true` — command executed successfully, or argument validation passed in dry-run mode.
- `false` — argument validation failed, the CAN socket could not be opened or bound, a send or receive timed out, a filter string was malformed, a `HWCTRL` sysfs read/write failed, a script file was not found, or a memory allocation failure occurred.

Errors are emitted via `LOG_PRINT` at `LOG_ERROR` severity. Configuration and parameter loading issues are logged at `LOG_WARNING` or `LOG_VERBOSE`.
