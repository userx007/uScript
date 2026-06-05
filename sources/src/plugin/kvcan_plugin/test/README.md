# vcan_mirror

A Linux userspace utility that mirrors CAN frames received on a virtual CAN
interface (`vcan`) back to the same interface — useful for testing CAN
applications without any physical hardware or CAN adapter.

---

## How It Works

The `vcan` kernel module creates a virtual CAN network interface that behaves
like a real CAN bus entirely in software.  Any frame written to the interface
is immediately visible to all listeners on the same interface.  `vcan_mirror`
sits on the bus, reads every incoming frame, and writes it back — simulating
an echo slave or a loopback peer.

```
[your CAN app]
      │  write frame
      ▼
  vcan0  (kernel virtual CAN bus)
      │
      ▼
[vcan_mirror]  ──── reads frame, writes it back ────▶  vcan0
                                                          │
                                                          ▼
                                                   [your CAN app]
                                                   receives echo
```

`CAN_RAW_RECV_OWN_MSGS` is disabled so the mirrored echo is not re-read by
the mirror process itself, preventing an infinite echo storm.

---

## Prerequisites

| Package | Purpose |
|---------|---------|
| Linux kernel ≥ 3.x | `vcan` and SocketCAN built in |
| `can-utils` | `cansend`, `candump`, `cangen` CLI tools |
| `gcc` | Build the binary |

```bash
# Debian / Ubuntu
sudo apt install can-utils gcc

# Fedora / RHEL
sudo dnf install can-utils gcc
```

---

## Setup

```bash
# 1. Load the virtual CAN module
sudo modprobe vcan

# 2. Create the virtual interface
sudo ip link add dev vcan0 type vcan

# 3. Bring it up
sudo ip link set up vcan0

# 4. Verify
ip link show vcan0
# 5: vcan0: <NOARP,UP,LOWER_UP> mtu 72 ...
```

Multiple interfaces can be created independently:

```bash
sudo ip link add dev vcan1 type vcan && sudo ip link set up vcan1
sudo ip link add dev vcan2 type vcan && sudo ip link set up vcan2
```

---

## Build

```bash
gcc -o vcan_mirror vcan_mirror.c
```

---

## Usage

```
./vcan_mirror [interface]
```

| Argument | Default | Description |
|----------|---------|-------------|
| `interface` | `vcan0` | Virtual CAN interface to listen and echo on |

---

## Examples

### 1. Basic mirror on vcan0

```bash
./vcan_mirror
```

```
vcan_mirror: listening on vcan0 — press Ctrl-C to stop
DIR         ID      DLC   DATA
--------------------------------------------------
```

---

### 2. Send a frame and watch it echo

In a second terminal:

```bash
cansend vcan0 123#DEADBEEF
```

The mirror process prints:

```
RX   123  [4] DE AD BE EF
TX   123  [4] DE AD BE EF
```

And `candump` in a third terminal shows both the original and the echo:

```bash
candump vcan0
# vcan0  123   [4]  DE AD BE EF    <- original from cansend
# vcan0  123   [4]  DE AD BE EF    <- echo from vcan_mirror
```

---

### 3. Mirror on a different interface

```bash
./vcan_mirror vcan1
```

---

### 4. Generate random traffic and observe echoes

```bash
# Terminal 1 — run the mirror
./vcan_mirror vcan0

# Terminal 2 — generate random frames at 100 fps
cangen vcan0 -g 10 -I r -L r -D r

# Terminal 3 — watch all traffic (originals + echoes)
candump vcan0
```

---

### 5. Extended frame IDs (29-bit)

`vcan` and `vcan_mirror` handle both standard (11-bit) and extended (29-bit)
CAN IDs transparently:

```bash
cansend vcan0 1FFFFFFF#0102030405060708
```

```
RX   1FFFFFFF  [8] 01 02 03 04 05 06 07 08
TX   1FFFFFFF  [8] 01 02 03 04 05 06 07 08
```

---

### 6. Test with a CAN application

Run your own CAN node alongside the mirror to simulate a bus with a peer that
always acknowledges:

```bash
# Terminal 1
./vcan_mirror vcan0

# Terminal 2 — your application sending requests
cansend vcan0 700#01        # e.g. CANopen NMT start
candump vcan0               # watch your app receive the echo as if a node replied
```

---

## Teardown

```bash
# Stop vcan_mirror with Ctrl-C, then:
sudo ip link set down vcan0
sudo ip link delete vcan0
sudo rmmod vcan
```

---

## can-utils Quick Reference

| Command | Description |
|---------|-------------|
| `cansend vcan0 123#DEADBEEF` | Send a single CAN frame |
| `candump vcan0` | Dump all frames on the bus |
| `cangen vcan0` | Generate random CAN traffic |
| `canbusload vcan0` | Show bus load percentage |
| `canfdtest vcan0` | Full loopback test (requires mirror or real node) |
| `i2cdetect -l` | List CAN interfaces (use `ip link` instead) |

---

## Comparison with Real Hardware Testing

| Feature | vcan + vcan_mirror | Real CAN bus |
|---------|-------------------|-------------|
| Hardware needed | None | CAN adapter (e.g. USB-CAN) |
| Setup time | < 1 minute | Wiring + driver install |
| Bus speed simulation | No timing | Real bit timing |
| Multi-node simulation | Multiple vcan interfaces | Physical nodes |
| CI/CD friendly | ✅ Yes | ❌ No |
| CAN FD support | ✅ via `vcan` | Adapter dependent |

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| `ip link add` fails with `RTNETLINK: Operation not supported` | `vcan` module not loaded | `sudo modprobe vcan` |
| `socket: Protocol not supported` | SocketCAN not in kernel | Recompile kernel with `CONFIG_CAN=y` or use a distro kernel |
| No echo seen in `candump` | Mirror not running, or wrong interface name | Check `./vcan_mirror vcan0` is running and interface matches |
| Double echo storm | `CAN_RAW_RECV_OWN_MSGS` accidentally enabled | Recompile with the default (disabled) setting |
| `cansend: No such device` | Interface not up | `sudo ip link set up vcan0` |
