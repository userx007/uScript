# vector_eth_plugin

Communicate with Vector Informatik's port-based Ethernet interfaces
(VN5610(A), VN7610, VN7570, VX1135, ...) through Vector's XL Driver Library
(XL-API) direct Ethernet API (`xlEthTransmit`/`xlEthReceive`/`xlEthSetConfig`).
**Windows only** — see `uVectorEth.hpp` for why: Vector does not ship a Linux
driver for these devices.

Sibling to `vector_plugin` (CAN/CAN-FD) — same device enumeration and direct
channel selection (`hw=`/`serial=`/`name=`/`hwidx=`/`hwch=`, `DEVICES`), same
overall command set (`INFO`/`CONFIG`/`FILTER`/`CMD`/`SCRIPT`/`CYCLIC`), built
on the same `VectorDriverHandle` process-wide XL-API driver handle so a
process can have CAN and Ethernet channels open at once without either side's
`xlOpenDriver()`/`xlCloseDriver()` interfering with the other. This file only
covers what's different — see `vector_plugin/docs/README.md` for the shared
parts.

## One-time setup

1. Install the Vector driver package for your hardware (the "Vector Driver
   Setup" installer) so `vxlapi64.dll` and the device drivers are present —
   see `vector_plugin/docs/README.md`'s "Getting the real SDK" section
   (both plugins link the same `uVector` CMake target).
2. Open **Vector Hardware Config** and create an application entry (its name
   is what you pass as `a=` / `VECTOR_ETH_APP_NAME`) with your Ethernet
   channel assigned to it — or skip this and use direct selection instead
   (see below).

## Addressing: MAC + EtherType, not a CAN id

Every `CMD`/`SCRIPT`/`CYCLIC` payload is sent as the payload of one or more
standard Ethernet II frames (EtherType + up to 1500 payload bytes each — no
802.3 length-field or VLAN framing). The destination MAC and EtherType come
from `CONFIG`'s `dst=`/`type=` (defaults: broadcast, `0x88B5`) and can be
overridden per call:

```
VECTOR_ETH.CONFIG dst=AA:BB:CC:DD:EE:FF type=0x0800
VECTOR_ETH.CMD > H"48656C6C6F"                     # uses the CONFIG default
VECTOR_ETH.CMD > 11:22:33:44:55:66/0x0806 H"..."   # per-call MAC/EtherType override
```

The source MAC is always left for the hardware to fill in — this driver
never spoofs it.

## FILTER: source MAC and/or EtherType, both enforced together

Unlike `VECTOR.FILTER`'s single-`id:mask`-enforced caveat, `VECTOR_ETH.FILTER`
enforces **both** filters together (logical AND) since there's only ever one
of each:

```
VECTOR_ETH.FILTER src=AA:BB:CC:DD:EE:FF
VECTOR_ETH.FILTER type=0x0800
VECTOR_ETH.FILTER src=AA:BB:CC:DD:EE:FF type=0x0800
VECTOR_ETH.FILTER src= type=                       # clears both
```

## PHY configuration

`speed=`/`duplex=`/`connector=`/`phy=` map onto `xlEthSetConfig()`'s
`XL_ETH_MODE_*` constants and all default to auto-negotiate — most links
never need to touch these. `connector=`/`phy=` only matter on multi-connector
or BroadR-Reach-capable boards (VN5610(A)):

```
VECTOR_ETH.CONFIG speed=auto1000 duplex=full
VECTOR_ETH.CONFIG connector=dsub phy=broadr        # BroadR-Reach over the D-Sub connector
```

## No multi-frame transport protocol

There's no `t=`/ISO-TP/J1939/CANopen/Fast-Packet layer here — Ethernet's own
1500-byte MTU is already far larger than any single CAN frame, and this
codebase's `can_tp` library is CAN-specific. A payload over the MTU is
fragmented one frame per up-to-MTU chunk with no reassembly framing of its
own; pair this with a higher-level protocol (e.g. UDP, via this codebase's
own `udp_plugin` over the channel's own IP stack) if you need one.

## Example

```
VECTOR_ETH.DEVICES
VECTOR_ETH.CONFIG hw=VN5610A dst=AA:BB:CC:DD:EE:FF type=0x0800
VECTOR_ETH.CMD > H"48656C6C6F20457468657273"
VECTOR_ETH.CMD < H"64"
```
