# Candlelight Plugin

A C++ shared-library plugin that exposes a **Candlelight** (a.k.a. **gs_usb**) adapter — a native-USB CAN/CAN-FD adapter protocol used by CANable, candleLight-fw, and Elmue's CANable 2.5 firmware (https://github.com/Elmue/CANable-2.5-firmware-Slcan-and-Candlelight), among others — through the same unified command dispatcher used by the KVCAN, SLCAN and UCAN plugins. Unlike those three, there is no serial link underneath at all: the adapter enumerates as its own USB device, configuration goes over USB control transfers, and CAN frames travel over a bulk IN/OUT endpoint pair. See `uCandlelight.hpp` (in `sources/src/lib/drivers/candlelight/inc/`) for the full protocol reference this plugin implements.

**Compatibility note:** this driver speaks the "legacy Geschwister Schneider protocol" — the original, standard gs_usb wire format that the Linux kernel's own `gs_usb` driver and every candleLight-fw derivative implement. Elmue's CANable 2.5 firmware's own User & Developer Manual documents keeping this mode available on USB interface 0 specifically for backward compatibility, alongside that project's own extended "ElmüSoft" protocol (per-frame USB overhead reduction, multi-frame "blob" packing, on-device hardware filters, bus-load reporting, and more). This driver does not speak that extended protocol — see `uCandlelight.hpp`'s "Compatibility with Elmue's CANable 2.5 firmware" section for the two real interop details (little-endian-always framing, a possibly-short `DEVICE_CONFIG` reply) confirmed against that firmware's manual and the upstream kernel driver's history.

**Version:** 1.0.0.0

---

## Table of Contents

1. [Overview](#overview)
2. [Project Structure](#project-structure)
3. [Architecture](#architecture)
4. [Building](#building)
5. [Command Reference](#command-reference)
6. [CMD Expression Syntax](#cmd-expression-syntax)
7. [Script Files](#script-files)
8. [Error Handling and Return Values](#error-handling-and-return-values)
9. [Candlelight vs. SLCAN/UCAN](#candlelight-vs-slcanucan)

---

## Overview

The plugin loads as a dynamic shared library (`.so`). The host application calls the exported C entry points `pluginEntry()` / `pluginExit()` to create and destroy the plugin object. Once loaded, the host passes configuration settings (USB device selection, CAN bus timing, TX ID, filters, timeouts, buffer size) via `setParams()`, optionally calls `doInit()`, and then calls `doDispatch()` for every command it wants to execute.

All commands follow the pattern:

```
<PLUGIN>.<COMMAND> [arguments]
```

For example:

```
CANDLELIGHT.CONFIG vid=0x1209 pid=0x2323 b=500000 x=0x123 r=2000 w=2000 s=8
CANDLELIGHT.FILTER 0x100:0x7FF,0x18DAF100:0x1FFFFFFF
CANDLELIGHT.CMD > H"AABBCCDD" | H"06"
CANDLELIGHT.SCRIPT obd_sequence.txt
CANDLELIGHT.CYCLIC 100 AABBCCDD 0x100, 250 1122 0x200
```

---

## Project Structure

```
candlelight_plugin/
├── CMakeLists.txt          # Build definition (shared library)
├── docs/
│   └── README.md           # This file
├── inc/
│   ├── candlelight_plugin.hpp     # Class definition, command table, public accessors
│   └── private/
│       ├── candlelight_setup.hpp         # CONFIG token-parsing helper (key=value -> setter dispatch)
│       └── candlelight_frame_driver.hpp  # CandlelightFrameDriver: ICommDriver adapter over Candlelight
└── src/
    └── candlelight_plugin.cpp     # Entry points, command handlers, init/cleanup
```

The gs_usb wire protocol itself (USB control-transfer structs, bulk `gs_host_frame` layout, echo_id TX-completion handling, bit-timing calculator) lives one layer down, in the driver library `sources/src/lib/drivers/candlelight/` (`uCandlelight.hpp`/`uCandlelight.cpp`) — the plugin only ever calls the driver's typed API (`send_frame()`, `set_bitrate()`, `open_channel()`, ...), never touches a raw USB transfer directly.

---

## Architecture

### Plugin Lifecycle

```
pluginEntry()           -> creates CandlelightPlugin instance
  setParams()           -> loads INI values (USB vid/pid, bus timing, TX ID, filters, timeouts, buffer size)
  doInit()              -> marks plugin as initialized (USB device not opened yet)
  doEnable()            -> enables real execution (without this, commands validate args only)
  doDispatch(cmd, args) -> routes a command string to the correct handler
  doCleanup()           -> marks plugin as uninitialized and disabled
pluginExit(ptr)         -> deletes the CandlelightPlugin instance
```

> **Note:** `doInit()` does not open the USB device. The device -- and the CAN channel itself -- is opened on demand inside each `CMD`/`SCRIPT`/`CYCLIC` call using RAII: `m_OpenAndConfigure()` constructs a `CandlelightFrameDriver` (which opens the USB device and runs the gs_usb probe sequence), pushes the configured bit timing (computed from `b=`/`sp=`, or a raw register override -- see CONFIG) and the software filter list, opens the CAN channel with the configured mode flags, and hands back the ready driver. When the `shared_ptr<CandlelightFrameDriver>` goes out of scope at the end of the call, `Candlelight`'s destructor closes the channel and releases the USB interface automatically.

`doEnable()` controls a "dry-run / validation" mode: when not enabled, every command validates its arguments and returns `true` without performing any I/O.

### Command Dispatch Model

Commands are registered via a single-level `std::map` (`m_mapCmds`) populated in the constructor through an X-macro expansion -- identical in shape to the KVCAN, SLCAN and UCAN plugins:

```cpp
#define CANDLELIGHT_PLUGIN_COMMANDS_CONFIG_TABLE    \
CANDLELIGHT_PLUGIN_CMD_RECORD( INFO               ) \
CANDLELIGHT_PLUGIN_CMD_RECORD( CONFIG             ) \
CANDLELIGHT_PLUGIN_CMD_RECORD( FILTER             ) \
CANDLELIGHT_PLUGIN_CMD_RECORD( CMD                ) \
CANDLELIGHT_PLUGIN_CMD_RECORD( SCRIPT             ) \
CANDLELIGHT_PLUGIN_CMD_RECORD( CYCLIC             )
```

### INI Configuration Keys

| Key | Type | Description |
|---|---|---|
| `CANDLE_USB_VID` | uint16 (hex/dec) | USB vendor id of the adapter to open (default `0x1209`) |
| `CANDLE_USB_PID` | uint16 (hex/dec) | USB product id of the adapter to open (default `0x2323`) |
| `CANDLE_USB_INDEX` | uint32 | Which Nth device matching vid/pid to open (default 0) |
| `CANDLE_BITRATE` | uint32 | Target nominal CAN bit rate in bit/s (default 500000) |
| `CANDLE_SAMPLE_POINT` | double | Target nominal sample point, 0-1 (default 0.875) |
| `CANDLE_FD_BITRATE` | uint32 | Target CAN-FD data-phase bit rate in bit/s (default 2000000) |
| `CANDLE_FD_SAMPLE_POINT` | double | Target CAN-FD data-phase sample point, 0-1 (default 0.75) |
| `CANDLE_FD_BRS` | bool | Bit Rate Switch on outgoing CAN-FD frames (default true) |
| `CANDLE_MODE_FLAGS` | uint32 | Raw `GS_CAN_MODE_*` bitmask applied when the channel opens (default 0 = normal) |
| `CAN_TX_ID` | uint32 | CAN ID stamped on outgoing frames; decimal or `0x`-prefixed hex |
| `CAN_RX_ID` | uint32 | Optional; CAN id expected for peer responses when a transport protocol is active |
| `CAN_FILTERS` | string | Optional comma-separated software acceptance filter list; empty = accept all |
| `READ_TIMEOUT` | uint32 | Per-read timeout in milliseconds |
| `WRITE_TIMEOUT` | uint32 | Per-write timeout in milliseconds |
| `READ_BUF_SIZE` | uint32 | Receive buffer size in bytes (max 64 for CAN FD, max 8 for classic CAN) |
| `CAN_TP_PROTOCOL` | string | Optional; `none`/`isotp`/`j1939` |
| `ARTEFACTS_PATH` | string | Base directory from which script/file paths are resolved |
| `RAW_RESULT` | bool | Skip hexlification of `CMD`'s captured result |

### CommScriptClient Integration

This plugin uses the same `CommScriptCommandInterpreter<TDriver>` / `CommScriptClient<TDriver>` stack as the KVCAN, SLCAN and UCAN plugins. `CMD`, `SCRIPT` and `CYCLIC` all hand the open driver directly to these shared components.

The bridge is **`CandlelightFrameDriver`** (`inc/private/candlelight_frame_driver.hpp`), which inherits from `ICommDriver` and holds a `Candlelight` instance by composition:

```
CommScriptCommandInterpreter<CandlelightFrameDriver>
        |
        |  calls tout_write() / tout_read() on shared_ptr<const CandlelightFrameDriver>
        v
CandlelightFrameDriver : public ICommDriver
        |
        |  tout_write() -> builds CanFrame from TX id, calls m_candle.send_frame(frame, timeout)
        |  tout_read()  -> calls m_candle.receive_frame(frame, timeout), applies the software
        |                  filter list, copies payload out
        v
Candlelight (composition member m_candle)
        |
        |  send_frame() / receive_frame() -> USB control transfers (setup) + bulk transfers (frames)
        v
libusb-1.0
```

**Why not inherit from Candlelight?** `Candlelight::tout_write`/`tout_read` are not `virtual` in `Candlelight` itself (only in `ICommDriver`), so a subclass of `Candlelight` cannot `override` them. Inheriting from `ICommDriver` directly and delegating to a composed `Candlelight` member is the correct solution -- the same reasoning as `SLCANFrameDriver`/`UCANFrameDriver`.

**Software acceptance filtering:** unlike UCAN (one hardware standard + one hardware extended filter slot), gs_usb has **no filtering USB request at all** -- every frame the bus carries reaches the host, always. `CandlelightFrameDriver::tout_read()` applies `FILTER`'s configured list entirely in software, after each frame is decoded -- and, unlike SLCAN/UCAN's one-slot-per-kind limitation, accepts an unlimited number of `(id, mask)` entries, exactly like SocketCAN's `CAN_RAW_FILTER` list.

**`echo_id` and TX-completion:** every `send_frame()` call writes the frame, then reads bulk-IN packets -- silently absorbing any RX frames that arrive first -- until it sees the device's TX-completion echo (matching `echo_id`) or times out. See `uCandlelight.hpp`'s "echo_id" section for why gs_usb needs this extra round-trip that SLCAN/UCAN's simpler ack schemes don't.

---

## Building

The plugin is built as a CMake shared library. It links against `uIPlugin`, `uPluginOps`, `uCandlelight`, `uCommScriptClient`, and `uCommScriptCommandInterpreter`. `uCandlelight` in turn links `libusb-1.0` (see `sources/src/lib/drivers/candlelight/CMakeLists.txt`) -- install `libusb-1.0-0-dev` (Linux) or the equivalent development package before building.

```bash
mkdir build && cd build
cmake ..
make candlelight_plugin
```

The output is `libcandlelight_plugin.so`.

**Runtime USB permissions (Linux):** opening the adapter via `libusb` typically requires either running as root or a udev rule granting the invoking user access to the device (matching its vid:pid) -- the same requirement any other libusb-based gs_usb host tool has.

---

## Command Reference

### INFO

Prints version information and a usage summary. No arguments.

```
CANDLELIGHT.INFO
```

### CONFIG

```
CANDLELIGHT.CONFIG [vid=usb_vid] [pid=usb_pid] [idx=dev_index] [b=bitrate] [sp=sample_point] [fb=fd_bitrate] [fp=fd_sample_point] [z=fd_brs] [m=mode_flags] [x=tx_id] [v=rx_id] [r=read_tout] [w=write_tout] [s=recv_bufsize] [t=tp_protocol]
```

| Token | INI key | Description |
|---|---|---|
| `vid=<hex>` | `CANDLE_USB_VID` | USB vendor id |
| `pid=<hex>` | `CANDLE_USB_PID` | USB product id |
| `idx=<n>` | `CANDLE_USB_INDEX` | Nth matching device to open |
| `b=<bps>` | `CANDLE_BITRATE` | Target nominal CAN bit rate, e.g. `500000` |
| `sp=<0-1>` | `CANDLE_SAMPLE_POINT` | Target nominal sample point (default 0.875) |
| `fb=<bps>` | `CANDLE_FD_BITRATE` | Target CAN-FD data-phase bit rate, e.g. `2000000` |
| `fp=<0-1>` | `CANDLE_FD_SAMPLE_POINT` | Target CAN-FD data-phase sample point (default 0.75) |
| `z=<0\|1>` | `CANDLE_FD_BRS` | CAN-FD Bit Rate Switch off/on |
| `m=<bitmask>` | `CANDLE_MODE_FLAGS` | Raw `GS_CAN_MODE_*` bitmask (see below) |
| `x=<id>` | `CAN_TX_ID` | CAN ID for outgoing frames |
| `v=<id>` | `CAN_RX_ID` | Expected peer response id (transport protocol only) |
| `r=<ms>` | `READ_TIMEOUT` | Read timeout in milliseconds |
| `w=<ms>` | `WRITE_TIMEOUT` | Write timeout in milliseconds |
| `s=<bytes>` | `READ_BUF_SIZE` | Receive buffer size in bytes |
| `t=<proto>` | `CAN_TP_PROTOCOL` | `none`/`isotp`/`j1939` |

Power users can bypass the `b=`/`sp=` bit-timing calculator with raw register values:

| Token | Description |
|---|---|
| `ps=`, `p1=`, `p2=`, `sw=`, `bp=` | `prop_seg`, `phase_seg1`, `phase_seg2`, `sjw`, `brp` (nominal phase) |
| `dps=`, `dp1=`, `dp2=`, `dsw=`, `dbp=` | Same, for the CAN-FD data phase |

`GS_CAN_MODE_*` bitmask values for `m=`: 1=listen-only, 2=loopback, 4=triple-sample, 8=one-shot, 128=pad-to-max, 256=CAN-FD, 4096=berr-reporting. Combine by adding, e.g. `m=257` = listen-only + FD.

```
# 500 kbit/s classic CAN on the default candleLight-fw adapter, standard 11-bit ID 0x123
CANDLELIGHT.CONFIG b=500000 x=0x123 r=2000 w=2000 s=8

# 1 Mbit/s nominal / 4 Mbit/s data phase CAN-FD, extended 29-bit ID
CANDLELIGHT.CONFIG b=1000000 sp=0.8 fb=4000000 fp=0.7 z=1 m=256 x=0x18DAF100

# Select a specific adapter by vid/pid (e.g. the reference gs_usb firmware)
CANDLELIGHT.CONFIG vid=0x1D50 pid=0x606F b=500000

# Listen-only mode
CANDLELIGHT.CONFIG m=1
```

### FILTER

Software acceptance-filter list (unlimited entries, unlike SLCAN/UCAN's one-hardware-slot-per-kind).

```
CANDLELIGHT.FILTER [<id>:<mask>[,<id>:<mask>...]]
```

```
CANDLELIGHT.FILTER 0x100:0x7FF
CANDLELIGHT.FILTER 0x100:0x7FF,0x200:0x7FF,0x18DAF100:0x1FFFFFFF
CANDLELIGHT.FILTER
```

### CMD

```
CANDLELIGHT.CMD <expression>
```

```
CANDLELIGHT.CMD > H"DEADBEEF" | H"06"
CANDLELIGHT.CMD > H"02010C00000000" | H"04620C"
CANDLELIGHT.CMD < H"00000000"
CANDLELIGHT.CMD > H"FF"
```

### SCRIPT

```
CANDLELIGHT.SCRIPT <filename> [<delay_ms>]
```

### CYCLIC

```
CANDLELIGHT.CYCLIC <period1> <payload1> [id1] [, <period2> <payload2> [id2] ...] [&]
```

```
CANDLELIGHT.CYCLIC 100 AABBCCDD
CANDLELIGHT.CYCLIC 100 AABBCCDD 0x100, 250 1122 0x200 &
```

---

## CMD Expression Syntax

Identical grammar to the KVCAN/SLCAN/UCAN plugins -- direction operators (`>` send, `<` receive), data formats (plain string, quoted string, `H"..."` hex stream, `F"..."` file), and composite `send | expect` / `receive | respond` expressions. See the UCAN or SLCAN plugin's README for the full syntax reference.

---

## Script Files

Plain text files under `ARTEFACTS_PATH`, one CMD expression per line. Blank lines and lines beginning with `#` are skipped.

---

## Error Handling and Return Values

Every command handler returns `bool`. `false` covers: argument validation failure, the USB device could not be opened/claimed, bit timing could not be computed for the requested rate (no exact `(brp, tseg1, tseg2)` combination exists within this adapter's limits -- try a different rate or supply raw `ps=`/`p1=`/... values directly), a send or receive timed out or was NAK'd, a received frame didn't match, a filter string was malformed, a script file was not found, or an allocation failure.

---

## Candlelight vs. SLCAN/UCAN

| | SLCAN | UCAN | Candlelight |
|---|---|---|---|
| Transport | UART, ASCII lines | UART, binary packets | native USB (control + bulk transfers) |
| Device selection | serial device path | serial device path | USB vid:pid (+ device index) |
| Bit rate | fixed preset table | fixed preset table | bit rate + sample point -> calculated registers, or raw override |
| Bus mode | separate mode/auto-retx settings | separate mode/auto-retx settings | one combined `GS_CAN_MODE_*` bitmask |
| Acceptance filtering | 1 std + 1 ext hardware slot | 1 std + 1 ext hardware slot | no hardware filtering -- unlimited software filter list |
| TX acknowledgement | CR / BEL | ACK/NAK packet | TX-completion echo frame (`echo_id`) |

See `uCandlelight.hpp` (in `sources/src/lib/drivers/candlelight/inc/`) for the full protocol reference, and https://github.com/Elmue/CANable-2.5-firmware-Slcan-and-Candlelight for the specific firmware this plugin was written against.
