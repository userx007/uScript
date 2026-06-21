# pcan_plugin

CAN bus plugin backed by the **PEAK-System PCAN-Basic** API (`PCANBasic.h`).  
Command set is intentionally **identical** to `kvcan_plugin` and `slcan_plugin` so scripts are portable across all three plugins with minimal changes.

---

## Prerequisites

Install the PCAN-Basic SDK from PEAK-System:

- **Linux:** `peak-linux-driver` + `libpcanbasic` packages from https://www.peak-system.com/fileadmin/media/linux/index.htm
- **Windows:** PCAN-Basic SDK installer from https://www.peak-system.com/PCAN-Basic.239.0.html

The compiler must be able to find `PCANBasic.h` on the include path.

---

## INI File Configuration

```ini
[PLUGIN]
ARTEFACTS_PATH  = /path/to/scripts    ; base directory for SCRIPT command
PCAN_CHANNEL    = 0x51                ; PCAN_USBBUS1 — see channel table below
PCAN_BITRATE    = 500000              ; CAN bitrate in bps
PCAN_EXTENDED   = 0                   ; 0=auto (from TX ID), 1=force 29-bit EFF
PCAN_FD         = 0                   ; 0=classic CAN, 1=CAN FD
CAN_TX_ID       = 0x7FF               ; default TX CAN ID
CAN_FILTERS     = 0x100:0x7FF         ; optional: comma-separated id:mask list
READ_TIMEOUT    = 1000                ; ms
WRITE_TIMEOUT   = 1000                ; ms
READ_BUF_SIZE   = 8                   ; bytes (1–64)
```

### Common PCAN Channel Handles

| Handle  | Constant         | Description        |
|---------|------------------|--------------------|
| `0x51`  | `PCAN_USBBUS1`   | USB adapter #1     |
| `0x52`  | `PCAN_USBBUS2`   | USB adapter #2     |
| `0x81`  | `PCAN_PCIBUS1`   | PCI/PCIe channel 1 |
| `0x82`  | `PCAN_PCIBUS2`   | PCI/PCIe channel 2 |
| `0x41`  | `PCAN_ISABUS1`   | ISA channel 1      |

Full list in `PCANBasic.h`.

---

## Commands

### `PCAN.INFO`
Print plugin version, build date, and usage summary.  
No arguments.

---

### `PCAN.CONFIG`
Set parameters at runtime. Any subset of keys may be specified; omitted keys keep their current values. Changes take effect on the **next** CMD or SCRIPT call (the channel is reopened each time).

```
PCAN.CONFIG [i:channel] [b:bitrate] [x:tx_id] [r:read_tout] [w:write_tout] [s:recv_bufsize] [e:extended] [f:fd]
```

| Key | Description |
|-----|-------------|
| `i` | PCAN channel handle (decimal or `0x`-hex), e.g. `0x51` |
| `b` | CAN bitrate in bps: `1000000`, `800000`, `500000`, `250000`, `125000`, `100000`, `95000`, `83000`, `50000`, `47000`, `33000`, `20000`, `10000`, `5000` |
| `x` | TX CAN ID; EFF flag auto-set when ID > `0x7FF` |
| `r` | Read timeout in ms (default `1000`) |
| `w` | Write timeout in ms (default `1000`) |
| `s` | Read buffer size in bytes, `1`–`64` (default `8`) |
| `e` | Force extended (29-bit) frame format: `0`=auto, `1`=force EFF |
| `f` | CAN FD mode: `0`=classic CAN (default), `1`=CAN FD |

**Examples:**
```
PCAN.CONFIG i:0x51 b:500000 x:0x7FF r:2000 w:2000 s:8
PCAN.CONFIG i:0x51 b:500000 x:0x18DAF100
PCAN.CONFIG b:250000
```

---

### `PCAN.FILTER`
Set software acceptance filters. Syntax is **identical** to `KVCAN.FILTER`.

```
PCAN.FILTER <id>:<mask>[,<id>:<mask>…]
PCAN.FILTER                             ← clears all filters (accept everything)
```

- `id` and `mask` accept decimal or `0x`-prefixed hex.
- `CAN_EFF_FLAG` (`0x80000000`) is auto-set when `id > 0x7FF`.
- PCAN-Basic has one hardware filter slot; the first filter entry is applied there. Additional entries are matched in software in the driver's receive loop.

**Examples:**
```
PCAN.FILTER 0x100:0x7FF
PCAN.FILTER 0x100:0x7FF,0x18DAF100:0x1FFFFFFF
PCAN.FILTER
```

---

### `PCAN.CMD`
Execute a single send/receive (or both) operation. The channel is opened and closed automatically (RAII).

```
PCAN.CMD > H"AABBCCDD" | H"06"
PCAN.CMD < "Ready" | "Go!"
```

Payload must be ≤ 8 bytes (classic CAN) or ≤ 64 bytes (CAN FD).

---

### `PCAN.SCRIPT`
Execute a multi-command script file. The channel is opened once for the entire script.

```
PCAN.SCRIPT scriptfile.txt
PCAN.SCRIPT scriptfile.txt 10          ← 10 ms delay between commands
```

Script syntax is the same as for `KVCAN.SCRIPT` and `SLCAN.SCRIPT`.

---

## Portability vs KVCAN / SLCAN

| Feature              | KVCAN                   | SLCAN                       | PCAN                        |
|----------------------|-------------------------|-----------------------------|-----------------------------|
| `i:` key             | SocketCAN iface (`can0`) | UART device (`/dev/ttyACM0`) | PCAN channel (`0x51`)      |
| `b:` key             | *(not used)*            | CAN bitrate preset (0–13)   | CAN bitrate in bps          |
| `x:` TX ID           | ✓ same syntax           | ✓ same syntax               | ✓ same syntax               |
| `r/w/s` timeouts     | ✓ same syntax           | ✓ same syntax               | ✓ same syntax               |
| FILTER syntax        | ✓ identical             | one slot per format         | ✓ identical to KVCAN        |
| CMD / SCRIPT grammar | ✓ identical             | ✓ identical                 | ✓ identical                 |

To migrate a KVCAN script to PCAN, change only the CONFIG line:
```
# KVCAN:  KVCAN.CONFIG i:can0 x:0x123
# PCAN:   PCAN.CONFIG  i:0x51 b:500000 x:0x123
```
