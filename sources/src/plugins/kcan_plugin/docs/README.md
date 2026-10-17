# KCAN Plugin

SocketCAN plugin for **every** CAN interface the Linux kernel exposes as a network device:

* **virtual** interfaces - `vcan0`, `vxcan` ... (no bus, nothing to configure), and
* **real** adapters - `can0`, `can1`, ... created by `peak_usb` (PEAK PCAN-USB), `gs_usb` (candleLight and clones),
  `kvaser_usb`, `mcp251xfd`/`mcp251x` (SPI), on-board controllers, `slcan` via `slcand` ...

**including setting the bus speed** of a real adapter. One script and one ini file work on both: on a virtual
interface the bit-timing keys (`b=`, `sp=`, `fd=`, `db=` ...) are accepted but there is nothing to program, so the
interface is only brought up if it is down.

Frame I/O is the same for virtual and real interfaces; what a real bus needs on top is optional:

| feature | how |
|---|---|
| frame I/O through a `PF_CAN` socket | `CMD`, `SCRIPT`, `CYCLIC`, `FILTER` |
| set bitrate / sample point | **`b=`, `sp=`** |
| CAN FD data bitrate | **`fd=on db=`** |
| listen-only, loopback, one-shot, triple-sampling, bus-error reporting | `lo=` `lb=` `os=` `ts=` `berr=` |
| bring link up / down, read state, restart from BUS-OFF | **`UP` `DOWN` `STATUS` `RESTART`** |
| write waits for a free TX queue up to the write timeout | `WRITE_TIMEOUT` |
| bus error frames (bus-off, ...) logged | `e=` / `CAN_ERR_MASK` |

Bitrate and link state are changed over **rtnetlink** directly - no `ip` binary, no libsocketcan.

## Quick start (PEAK PCAN-USB)

```
sudo modprobe peak_usb          # once; the adapter then shows up as can0 (see ip link)
```

```
KCAN.CONFIG i=can0 b=500000 x=0x123      # remember interface, bus speed, TX id
KCAN.CMD > H"DEADBEEF" | H"06"           # first use programs 500 kbit/s, brings can0 up, sends
```

Equivalent explicit form:

```
KCAN.UP i=can0 b=500000
KCAN.STATUS                               # can0: kind=can up=1 carrier=1 mtu=16 state=ERROR-ACTIVE bitrate=500000 ...
```

### Privileges

Changing bitrate / link state needs `CAP_NET_ADMIN` (root, or `sudo setcap cap_net_admin+ep <binary>`).
Frame I/O and `STATUS` do not. If the interface is **already up with the requested settings, KCAN touches
nothing** — so you can also let the system configure the adapter (script `testers/scripts/kcan_setup.sh`,
udev, systemd-networkd) and run KCAN unprivileged with or without `b=`.
Omit `b=` altogether and KCAN uses the interface exactly as configured.

## Commands

All commands: `KCAN.<COMMAND> [arguments]`.

### CONFIG
`KCAN.CONFIG [token=value …]` — any subset; omitted keys keep their value.

| token | INI key | meaning |
|---|---|---|
| `i=` | `CAN_IFACE` | interface name (`can0`, `vcan0`, …) |
| `b=` | `CAN_BITRATE` | bus speed in bit/s: `500000`, `500k`, `1M`, `83.333k` (1 000 … 16 000 000) |
| `sp=` | `CAN_SAMPLE_POINT` | sample point: `0.875`, `87.5` or `875` (all = 87.5 %); default: kernel's choice |
| `fd=` | `CAN_FD` | `on`/`off` — CAN FD mode (requires `db=`) |
| `db=` | `CAN_DATA_BITRATE` | CAN FD data-phase bitrate, e.g. `2M` |
| `dsp=` | `CAN_DATA_SAMPLE_POINT` | CAN FD data-phase sample point |
| `lo=` | `CAN_LISTEN_ONLY` | silent mode — never transmits, never ACKs (passive sniffing) |
| `lb=` | `CAN_LOOPBACK` | controller internal loopback — frames reach other host processes (candump), not the bus |
| `os=` | `CAN_ONE_SHOT` | no automatic retransmission |
| `ts=` | `CAN_TRIPLE_SAMPLING` | sample each bit 3× |
| `berr=` | `CAN_BERR_REPORTING` | controller reports bus errors as error frames |
| `rs=` | `CAN_RESTART_MS` | automatic BUS-OFF recovery after N ms (0 = off) |
| `auto=` | `CAN_AUTO_LINK` | apply the settings automatically before first use (default `on`) |
| `e=` | `CAN_ERR_MASK` | `CAN_ERR_*` classes of error frames to log (`0x1FFFFFFF` = all) |
| `x=` `y=` `r=` `w=` `s=` `t=` … | `CAN_TX_ID` `CAN_RX_ID` `READ_TIMEOUT` … | see `KCAN.INFO` |

The link settings are only *stored* by CONFIG. They are applied to the adapter when it is next used
(`auto=on`) or when `UP` is issued. Changing bit-timing briefly takes the link down; an interface that already
matches is left alone.

### UP
`KCAN.UP [same tokens as CONFIG]` — apply bitrate/modes now and bring the interface up.

### DOWN
`KCAN.DOWN` — bring the interface down.

### STATUS
`KCAN.STATUS` — logs and returns e.g.
`can0: kind=can up=1 carrier=1 mtu=16 state=ERROR-ACTIVE bitrate=500000 sample_point=87.5% clock=8000000 brp=1 … ctrlmode=<> restart_ms=0 txerr=0 rxerr=0`.
`state` is one of ERROR-ACTIVE, ERROR-WARNING, ERROR-PASSIVE, BUS-OFF, STOPPED, SLEEPING.
`ERROR-ACTIVE` with `carrier=1` and a second node present means the bus is healthy.

### RESTART
`KCAN.RESTART` — recover from BUS-OFF (interface up, `rs=0`).

### Notes for real hardware (FILTER / CMD / SCRIPT / CYCLIC)

* A frame nobody acknowledges (no other node, wrong bitrate) stays in the TX queue. A write returns
  `WRITE_TIMEOUT` after `WRITE_TIMEOUT` ms instead of hanging. `0` = wait until a stop is requested.
* Remote frames (RTR) are skipped on receive; error frames are logged (if `e=` set) and never returned as data.
* Frames longer than 8 bytes are CAN FD frames (reachable through a segmented transport, `t=`; without one the plugin
  accepts at most 8 bytes per frame). The kernel only takes them on an interface with mtu 72: a real adapter must
  be configured `fd=on db=<rate>`; for a vcan check the `mtu=` in `STATUS` and use `ip link set <iface> mtu 72`
  if it shows 16. The error message names this when the kernel refuses such a frame.

### CYCLIC
`KCAN.CYCLIC time1 val1 [id1], time2 val2 [id2], ... [&]` - send periodic frames (`time` in ms, `val` hex, `id`
optional: defaults to the TX id from `CONFIG`).

```
KCAN.CYCLIC 100 AABBCCDD 0x100, 250 1122 0x200       # one full pattern (lcm of the periods), then return
KCAN.CYCLIC 100 AABBCCDD 0x100, 250 1122 0x200 &     # repeat in a thread until the script is stopped
```
`CYCLIC_CACHED` (ini / `CONFIG`) controls whether every entry is parsed once per session or on every tick (needed
for volatile `?=` macros).

### FILTER

Installs hardware acceptance filters on the SocketCAN socket via `setsockopt(SO_CAN_RAW_FILTER)`. Filters are stored in the plugin state and re-applied each time a `CMD` or `SCRIPT` opens a new socket. Calling `FILTER` with an empty argument removes all filters (accept everything).

```
KCAN.FILTER [<id>:<mask>[,<id>:<mask>…]]
```

Both `id` and `mask` accept decimal or `0x`-prefixed hex values. The comparison performed by the kernel is: `received_id & mask == id & mask`.

```
# Accept only 11-bit ID 0x100
KCAN.FILTER 0x100:0x7FF

# Accept IDs 0x100 and 0x200 (exact match on each)
KCAN.FILTER 0x100:0x7FF,0x200:0x7FF

# Accept any ID in the range 0x700–0x7FF
KCAN.FILTER 0x700:0x700

# Clear all filters — accept everything
KCAN.FILTER
```

---

### CMD

Executes a single send/receive command over the CAN bus (virtual or real). The socket is opened for the duration of the call and closed automatically when the command completes. The TX ID and any configured filters are applied immediately after open.

Payload size constraints are enforced by the driver: classic CAN frames carry at most **8 bytes**; CAN FD frames carry at most **64 bytes**. The driver selects the frame type automatically based on payload size.

```
KCAN.CMD <expression>
```

```
# Transmit a 4-byte payload with ID 0x123, expect a 1-byte ACK
KCAN.CMD > H"DEADBEEF" | H"06"

# Send an OBD-II engine RPM request, expect any response frame
KCAN.CMD > H"02010C00000000" | H"04"

# Receive-only — wait for one frame into a fixed buffer
KCAN.CMD < H"00000000"

# Send only — no response expected
KCAN.CMD > H"FF"
```

---

### SCRIPT

Executes a multi-command script file from the `ARTEFACTS_PATH` directory. The CAN socket is opened once for the lifetime of the script. Each line contains one CMD expression. An optional inter-command delay (in milliseconds) can be specified.

```
KCAN.SCRIPT <filename> [<delay>]
```

```
# Run an OBD-II diagnostic sequence with no delay
KCAN.SCRIPT obd_pids.txt

# Run a UDS session script with 10 ms between each frame
KCAN.SCRIPT uds_session.txt 10

# Run a firmware update sequence with a 50 ms delay
KCAN.SCRIPT fw_update.txt 50
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
KCAN.CMD > H"02010C00000000" | H"04620C"

# Send a command token, expect an ACK byte
KCAN.CMD > "START" | H"06"

# Send a binary file payload (≤8 bytes), then expect a hex token
KCAN.CMD > F"request.bin" | H"AA"
```

**Receive then respond:**

```
# Wait for a seed frame, then send a key frame
KCAN.CMD < H"6727" | H"272800000000"

# Wait for a prompt token, then send a reply
KCAN.CMD < "Ready?" | "Go!\r\n"
```

**Send or receive only:**

```
# Transmit only
KCAN.CMD > H"0201000000000000"

# Receive only — wait for one frame
KCAN.CMD < H"00000000"
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
KCAN.SCRIPT uds_session.txt
KCAN.SCRIPT uds_session.txt 10
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

## INI file

```
[KCAN]
ARTEFACTS_PATH     =
CAN_IFACE          = can0
CAN_BITRATE        = 500000
CAN_SAMPLE_POINT   = 0.875
CAN_TX_ID          = 0x123
READ_TIMEOUT       = 2000
WRITE_TIMEOUT      = 2000
# CAN FD:  CAN_FD = on, CAN_DATA_BITRATE = 2M
```

Run `KCAN.INFO` for the complete key list.

## Troubleshooting

| message | meaning |
|---|---|
| `Operation not permitted - … needs root or CAP_NET_ADMIN` | run privileged, or configure the interface externally (see Privileges) |
| `No such device` | adapter not plugged in / driver not loaded (`modprobe peak_usb`) |
| `send() failed … errno 100 (interface is down …)` | interface is down: set `b=` (auto link) or `KCAN.UP` |
| `TX queue … stayed full` | nobody ACKs: no other node, wrong bitrate, cable, termination (120 Ω each end) |
| `Invalid value … bitrate not achievable` | controller clock cannot generate that bitrate/sample point |
| `fd=on requires a data bitrate` | add `db=` |
| BUS-OFF | wrong bitrate or bus fault; `KCAN.RESTART`, or `rs=100` for auto-recovery |

## Building

Linux only. `libs/drivers/kcan` (static lib `uKCan`: frame driver `uKCan.hpp` + link control `uKCanLink.hpp`) and
`plugins/kcan_plugin` (`libkcan_plugin.so`). Enable with `-DUSCRIPT_PLUGIN_KCAN=ON` (on by default). The helper
`testers/kcan_link` (`kcan_link status|up|down|restart can0 500k`) exercises the link layer standalone;
`testers/simpleloopback/kcan_loopback` mirrors frames on a `vcan0`/`canN` for loopback tests.
