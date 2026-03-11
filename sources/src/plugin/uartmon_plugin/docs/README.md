# UARTMon Plugin

A C++ shared-library plugin that monitors the system for UART serial port insertions and removals. The plugin wraps the `uart::PortMonitor` background monitor, exposing commands to start and stop monitoring, enumerate currently present ports, and block (or non-block) until a port event occurs — with optional timeout and background-thread execution.

**Version:** 2.0.0.0

---

## Table of Contents

1. [Overview](#overview)
2. [Project Structure](#project-structure)
3. [Architecture](#architecture)
   - [Plugin Lifecycle](#plugin-lifecycle)
   - [Command Dispatch Model](#command-dispatch-model)
   - [Background Thread Model](#background-thread-model)
   - [INI Configuration Keys](#ini-configuration-keys)
4. [Building](#building)
5. [Command Reference](#command-reference)
   - [INFO](#info)
   - [LIST\_PORTS](#list_ports)
   - [START](#start)
   - [STOP](#stop)
   - [WAIT\_INSERT](#wait_insert)
   - [WAIT\_REMOVE](#wait_remove)
6. [Return Data](#return-data)
7. [Typical Usage Sequences](#typical-usage-sequences)
8. [Fault-Tolerant and Privileged Modes](#fault-tolerant-and-privileged-modes)
9. [Error Handling and Return Values](#error-handling-and-return-values)

---

## Overview

The plugin loads as a dynamic shared library (`.so` / `.dll`). The host application calls the exported C entry points `pluginEntry()` / `pluginExit()` to create and destroy the plugin object. Once loaded, the host passes configuration settings (polling interval) via `setParams()`, calls `doInit()` to configure the monitor, then calls `doDispatch()` to control monitoring and wait for port events.

All commands follow the pattern:

```
<PLUGIN>.<COMMAND> [arguments]
```

For example:

```
UARTMON.START
UARTMON.WAIT_INSERT 5000
NEW_PORT ?= UARTMON.WAIT_INSERT
UARTMON.STOP
```

---

## Project Structure

```
uartmon_plugin/
├── CMakeLists.txt              # Build definition (shared library)
├── inc/
│   └── uartmon_plugin.hpp      # Class definition, command table, thread vector
└── src/
    └── uartmon_plugin.cpp      # Entry points, command handlers, init/cleanup
```

The plugin has a single implementation file. Protocol-level port detection is fully delegated to the `uart::PortMonitor` component from the `uUtils` library; the plugin itself only handles argument parsing, thread management, and result routing.

---

## Architecture

### Plugin Lifecycle

```
pluginEntry()              → creates UartmonPlugin instance
  setParams()              → loads INI values (polling interval)
  doInit()                 → configures uart::PortMonitor polling interval
  doEnable()               → enables real execution (without this, commands validate args only)
  doDispatch(cmd, args)    → routes a command string to the correct handler
  doCleanup()              → stops monitoring if running, marks plugin uninitialized
pluginExit(ptr)            → joins all background threads, deletes UartmonPlugin instance
```

`doEnable()` controls a "dry-run / validation" mode: when not enabled, every command validates its arguments and returns `true` without performing any monitoring or blocking. This allows test frameworks to verify command syntax before the device is connected.

`doInit()` calls `uart::PortMonitor::setPollingInterval()` to configure how often the underlying monitor checks for port changes. It does **not** start monitoring — that is a separate explicit step via `START`.

### Command Dispatch Model

Commands are registered via a single-level `std::map` (`m_mapCmds`) populated in the constructor through an X-macro expansion:

```cpp
#define UARTMON_PLUGIN_COMMANDS_CONFIG_TABLE   \
UARTMON_PLUGIN_CMD_RECORD( INFO              ) \
UARTMON_PLUGIN_CMD_RECORD( START             ) \
UARTMON_PLUGIN_CMD_RECORD( STOP              ) \
UARTMON_PLUGIN_CMD_RECORD( LIST_PORTS        ) \
UARTMON_PLUGIN_CMD_RECORD( WAIT_INSERT       ) \
UARTMON_PLUGIN_CMD_RECORD( WAIT_REMOVE       ) \

// In the constructor:
#define UARTMON_PLUGIN_CMD_RECORD(a) \
    m_mapCmds.insert(std::make_pair(#a, &UartmonPlugin::m_Uartmon_##a));
UARTMON_PLUGIN_COMMANDS_CONFIG_TABLE
#undef UARTMON_PLUGIN_CMD_RECORD
```

`WAIT_INSERT` and `WAIT_REMOVE` both resolve to the same private helper `m_GenericWaitFor()`, parameterised by a boolean `bInsert` flag.

### Background Thread Model

Both `WAIT_INSERT` and `WAIT_REMOVE` accept an optional `&` suffix that causes the wait to run in a detached background thread rather than blocking the caller. Threads are stored in `m_vThreads` (a `std::vector<std::thread>`) and are joined in the destructor. This allows a host sequence to fire off a port-event wait and continue issuing other commands while the monitor runs concurrently.

The three possible outcomes from a wait, as defined by the `uart::WaitResult` enum, are:

| Result | Description | `getData()` return |
|---|---|---|
| `Success` | A port event was detected | Inserted or removed port name |
| `Timeout` | The timeout elapsed with no event | Empty string |
| `Stopped` | `STOP` was called during the wait | Empty string |

### INI Configuration Keys

| Key | Type | Description |
|---|---|---|
| `POLLING_INTERVAL` | uint32 | How often (in milliseconds) the port monitor polls the system for changes. Defaults to `PLUGIN_DEFAULT_UARTMON_POLLING_INTERVAL`. |

---

## Building

The plugin is built as a CMake shared library. It links against `uSharedConfig`, `uIPlugin`, `uPluginOps`, and `uUtils` (which provides `uart::PortMonitor`), all of which must be available in the CMake build tree.

```bash
mkdir build && cd build
cmake ..
make uartmon_plugin
```

The output is `libuartmon_plugin.so` (Linux) or `uartmon_plugin.dll` (Windows).

---

## Command Reference

### INFO

Prints version information and a concise usage summary of all supported commands to the logger. Takes **no arguments** and works even if `doInit()` failed.

```
UARTMON.INFO
```

**Example output (abbreviated):**
```
UARTMON   | Vers: 2.0.0.0
UARTMON   | Description: UART port monitor - detect insertions and removals
UARTMON   |
UARTMON   | LIST_PORTS : list the UART ports currently reported by the system
UARTMON   |   Usage: UARTMON.LIST_PORTS
UARTMON   |
UARTMON   | START : start monitoring UART port insertions and removals
UARTMON   |   Usage: UARTMON.START
...
```

---

### LIST\_PORTS

Enumerates all UART serial ports currently visible to the operating system and prints them to the logger as a comma-separated list. Monitoring does **not** need to be started before calling this command.

```
UARTMON.LIST_PORTS
```

This command takes no arguments.

**Example output:**
```
UART_MON   | Ports: /dev/ttyUSB0, /dev/ttyACM0
UART_MON   | Ports: COM3, COM7
UART_MON   | Ports: (no ports found)
```

---

### START

Starts the background port monitor. The monitor begins tracking which UART ports are present so that subsequent `WAIT_INSERT` and `WAIT_REMOVE` calls can detect changes. Must be called before any `WAIT_INSERT` or `WAIT_REMOVE` command.

```
UARTMON.START
```

This command takes no arguments. Returns `false` if monitoring is already running.

```
UARTMON.START
```

---

### STOP

Stops the background port monitor. Any `WAIT_INSERT` or `WAIT_REMOVE` calls that are currently blocking (or running in background threads) will return with a `Stopped` result and clear the result data.

```
UARTMON.STOP
```

This command takes no arguments. Returns `false` if monitoring is not currently running.

```
UARTMON.STOP
```

---

### WAIT\_INSERT

Blocks until a new UART port appears on the system, then stores the port name in the plugin's result data. Requires monitoring to be running (`START` must have been called first).

```
UARTMON.WAIT_INSERT [<timeout_ms>] [&]
```

| Argument | Description |
|---|---|
| `timeout_ms` | Maximum time to wait in milliseconds. `0` or omitted means wait indefinitely. |
| `&` | Run in a background thread (non-blocking). The caller returns immediately; the result is written asynchronously. |

**Return data:** The name of the inserted port on success (e.g., `/dev/ttyUSB0`, `COM3`), or an empty string on timeout or if monitoring was stopped.

```
# Block indefinitely until any port is inserted
UARTMON.WAIT_INSERT

# Block for up to 5 seconds
UARTMON.WAIT_INSERT 5000

# Capture the inserted port name into a variable
NEW_PORT ?= UARTMON.WAIT_INSERT

# Capture with timeout
NEW_PORT ?= UARTMON.WAIT_INSERT 5000

# Fire and forget — non-blocking background wait
UARTMON.WAIT_INSERT &

# Background wait with timeout, result captured later
NEW_PORT ?= UARTMON.WAIT_INSERT 5000 &
```

---

### WAIT\_REMOVE

Blocks until an existing UART port disappears from the system, then stores the port name in the plugin's result data. Requires monitoring to be running (`START` must have been called first).

```
UARTMON.WAIT_REMOVE [<timeout_ms>] [&]
```

| Argument | Description |
|---|---|
| `timeout_ms` | Maximum time to wait in milliseconds. `0` or omitted means wait indefinitely. |
| `&` | Run in a background thread (non-blocking). The caller returns immediately; the result is written asynchronously. |

**Return data:** The name of the removed port on success, or an empty string on timeout or if monitoring was stopped.

```
# Block indefinitely until any port is removed
UARTMON.WAIT_REMOVE

# Block for up to 10 seconds
UARTMON.WAIT_REMOVE 10000

# Capture the removed port name into a variable
REMOVED_PORT ?= UARTMON.WAIT_REMOVE

# Non-blocking background wait with timeout
REMOVED_PORT ?= UARTMON.WAIT_REMOVE 5000 &
```

---

## Return Data

`WAIT_INSERT` and `WAIT_REMOVE` write their result into the plugin's internal result string, accessible via `getData()`. The host framework typically exposes this through a variable-capture syntax such as:

```
VARIABLE ?= UARTMON.WAIT_INSERT [timeout] [&]
```

| Scenario | `getData()` content |
|---|---|
| Port event detected | Port name string (e.g., `/dev/ttyUSB1`, `COM4`) |
| Timeout elapsed | Empty string |
| Monitoring stopped during wait | Empty string |

`LIST_PORTS` prints directly to the logger and does not write to `getData()`.

---

## Typical Usage Sequences

**Wait for a USB-serial device to be plugged in, use it, then wait for it to be removed:**

```
UARTMON.START
NEW_PORT ?= UARTMON.WAIT_INSERT 10000
UART.CONFIG p:$(NEW_PORT) b:115200
UART.CMD > "AT\r\n" | "OK"
UARTMON.WAIT_REMOVE
UARTMON.STOP
```

**Start a background listener before a long operation, check afterward:**

```
UARTMON.START
NEW_PORT ?= UARTMON.WAIT_INSERT &
# ... other commands run here while waiting ...
UARTMON.STOP
```

**Enumerate available ports before deciding which one to use:**

```
UARTMON.LIST_PORTS
UARTMON.START
NEW_PORT ?= UARTMON.WAIT_INSERT 3000
UARTMON.STOP
```

---

## Fault-Tolerant and Privileged Modes

- **Fault-tolerant mode** (`setFaultTolerant()` / `isFaultTolerant()`): when set, the host framework continues executing subsequent commands even after this plugin returns `false` (e.g., on timeout or if monitoring was not started). Useful when port insertion is optional within a test sequence.
- **Privileged mode** (`isPrivileged()`): always returns `false` in this plugin. Reserved for future use in the plugin framework.

---

## Error Handling and Return Values

Every command handler returns `bool`:
- `true` — command executed successfully, or argument validation passed in disabled (dry-run) mode.
- `false` — argument validation failed, monitoring was not started before a `WAIT_*` call, `START` was called when already running, `STOP` was called when not running, or `doInit()` failed due to an invalid polling interval.

Errors are emitted via `LOG_PRINT` at `LOG_ERROR` severity. Timeout and stopped-monitor events are logged at `LOG_INFO` or `LOG_WARNING`. The host application controls log verbosity through the shared `uLogger` configuration.
