# KCAN Plugin

SocketCAN plugin for **real CAN adapters** — anything the Linux kernel exposes as a network interface
`can0`, `can1`, … (PEAK PCAN-USB in SocketCAN mode, Kvaser, candleLight/gs_usb, on-board controllers) —
**including setting the bus speed**. Virtual interfaces (`vcan0`) work too.

KCAN is the [KVCAN](../../kvcan_plugin/docs/README.md) plugin (same CMD / SCRIPT / CYCLIC / FILTER commands,
same TX/RX ids, same ISO-TP / J1939 / CANopen / NMEA2000 transport options) plus what a real bus needs:

| | KVCAN | KCAN |
|---|---|---|
| frame I/O through `PF_CAN` socket | yes | yes |
| set bitrate / sample point | — | **`b=`, `sp=`** |
| CAN FD data bitrate | — | **`fd=on db=`** |
| listen-only, loopback, one-shot, triple-sampling, bus-error reporting | — | yes |
| bring link up / down, read state, restart from BUS-OFF | — | **`UP` `DOWN` `STATUS` `RESTART`** |
| write waits for free TX queue up to the write timeout | blocks | yes (`WRITE_TIMEOUT`) |
| bus error frames (bus-off, …) logged | — | `e=` / `CAN_ERR_MASK` |

Bitrate and link state are changed over **rtnetlink** directly — no `ip` binary, no libsocketcan.

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
| `x=` `y=` `r=` `w=` `s=` `t=` … | `CAN_TX_ID` `CAN_RX_ID` `READ_TIMEOUT` … | as in KVCAN |

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

### FILTER, CMD, SCRIPT, CYCLIC
Unchanged from KVCAN — see its README. Notes specific to real hardware:

* A frame nobody acknowledges (no other node, wrong bitrate) stays in the TX queue. With KCAN a write returns
  `WRITE_TIMEOUT` after `WRITE_TIMEOUT` ms instead of hanging. `0` = wait until a stop is requested.
* Remote frames (RTR) are skipped on receive; error frames are logged (if `e=` set) and never returned as data.

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

Linux only. Added next to KVCAN: `libs/drivers/kcan` (static lib `uKCan`, frame driver `uKCan.hpp` +
link control `uKCanLink.hpp`) and `plugins/kcan_plugin` (`libkcan_plugin.so`). The helper
`testers/kcan_link` (`kcan_link status|up|down|restart can0 500k`) exercises the link layer standalone.
