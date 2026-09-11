# vector_plugin

Communicate with Vector Informatik CAN interfaces (VN1610, VN16xx, VN89xx,
VX1xxx, ...) through Vector's XL Driver Library (XL-API). **Windows only** —
see `uVector.hpp` / `docs` for why: Vector does not ship a Linux driver for
these devices.

Modelled directly on `pcan_plugin` (same command set, same CONFIG/FILTER
grammar, same multi-frame transport-protocol support) — see that plugin's
`docs/README.md` for the parts that are identical. This file only covers
what's different.

## One-time setup

1. Install the Vector driver package for your hardware (the "Vector Driver
   Setup" installer) so `vxlapi64.dll` and the device drivers are present.
2. Open **Vector Hardware Config** and create an application entry (its name
   is what you pass as `a=` / `VECTOR_APP_NAME`) with your CAN channel(s)
   assigned to it. XL-API resolves a physical channel through this
   application mapping, not through a raw device handle the way PCAN's
   `PCAN_USBBUS1` constant works — see `uVector.hpp`'s "Device selection"
   note for the full rationale.

## Getting the real SDK

Vector's XL Driver Library headers ship with this repo (`uVector/third_party/vxlapi/include/vxlapi.h`
is Vector's real, unmodified header), but the **import library and runtime DLL are
proprietary** and — unlike the PCAN-Basic SDK vendored under
`uPcan/third_party/PCAN_Basic` — are **not** bundled here. Drop your own copy
into `uVector/third_party/vxlapi/amd64/` (`vxlapi64.lib`, `vxlapi64.dll`, and
optionally your own `vxlapi.h` if your installed SDK's differs from the
vendored one) — same vendoring layout `ftdi2xx` uses for FTD2XX under
`third_party/ftd2xx/amd64/`. No `-D` flags needed; it's found automatically.
Without the `.lib`/`.dll`, the build still compiles against the vendored
header but will not link against a real driver — see
`uVector/third_party/CMakeLists.txt`'s header comment. Once vendored, the
DLL's path is exported as `VECTOR_RUNTIME_DLL` for whatever assembles the
final `extlibs/` folder next to `uscript` (same convention as
`FTDI_RUNTIME_DLL`).

## Two ways to pick a channel

**1. Vector Hardware Config** — pre-configure an application there with
your channel(s) assigned (see "One-time setup" above), then:
```
VECTOR.CONFIG a=Vector_Plugin i=0 b=500000
```

**2. Direct device selection (new)** — skip Vector Hardware Config
entirely. Run `VECTOR.DEVICES` to list every channel XL-API currently sees
(name, hardware type, hardware/channel index, serial number, CAN-capable
flag), then select one by type, serial number, or exact name:
```
VECTOR.DEVICES
VECTOR.CONFIG hw=VN1610 b=500000
VECTOR.CONFIG serial=12345 b=500000
VECTOR.CONFIG name="VN1610 Channel 1" b=500000
```
Add `hwidx=`/`hwch=` if `hw=` alone matches more than one board/connector.
Setting any of `hw=`/`serial=`/`name=` makes `CONFIG` use direct selection
instead of the `a=`/`i=` path for that session — the two modes aren't
combined. `Vector::hwTypeToString()`/`hwTypeFromString()` recognise every
`XL_HWTYPE_*` device type declared in Vector's real `vxlapi.h` (the full
VN0xxx/VN16xx/VN56xx/VN7xxx/VN8xxx/VX1xxx/VT6xxx family plus the legacy
CANcardX/XL and CANcaseXL/CANboardXL lines) — pass the raw numeric
`XL_HWTYPE_*` value to `hw=` for anything newer than this repo's copy of the
lookup table in `uVector.cpp`.

## Differences from `pcan_plugin` / `kvcan_plugin`

| Key | PCAN                         | Vector                                          |
|-----|-------------------------------|--------------------------------------------------|
| `a` | n/a                            | Vector Hardware Config application name           |
| `i` | global PCAN channel handle     | zero-based channel index *within* that application |
| `hw`/`serial`/`name`/`hwidx`/`hwch` | n/a          | direct channel selection, bypassing `a`/`i` — see above |
| `f` | CAN FD toggle                  | CAN FD toggle, fully implemented (up to 64 bytes/frame) — see `d=`/`iso=`/`brs=`/`padb=` |

Everything else — `b`/`x`/`y`/`r`/`w`/`s`/`e`/`t` and all the ISO-TP/J1939/
CANopen/Fast-Packet tuning keys, `FILTER`'s single-active-filter caveat,
`CMD`/`SCRIPT`/`CYCLIC` semantics — is identical to `pcan_plugin`.

## CAN FD

`VECTOR.CONFIG f=1` switches the channel to CAN FD (up to 64 data bytes per
frame). `b=` becomes the arbitration-phase bitrate; `d=` sets the data-phase
bitrate (default 2 Mbit/s). `iso=1` (default) selects ISO 11898-1:2015
framing, `iso=0` the pre-standard Bosch/non-ISO variant. `brs=1` (default)
switches to the data bitrate on outgoing frames; `brs=0` sends FD-framed
(EDL) messages at the arbitration bitrate only. `padb=` sets the fill byte
used to pad a fragment shorter than 64 bytes up to the next legal CAN-FD DLC
length (0-8, 12, 16, 20, 24, 32, 48, 64) — see `uVector.hpp`'s
`canFdLenToDlc()`. Remember to raise `s=` (read buffer size) past 8 if you
want `CMD`'s single-frame reads to actually see more than 8 bytes of an FD
frame.
```
VECTOR.CONFIG hw=VN1630 b=500000 f=1 d=2000000 s=64
VECTOR.CMD > H"0011223344556677889900112233445566778899001122334455667788990011"
```

## Ethernet

This plugin only talks CAN/CAN-FD. For Vector's port-based Ethernet
interfaces (VN5610(A)/VN7610/VN7570/VX1135/...) see the separate
`vector_eth_plugin`, which shares this plugin's device-enumeration/direct-
selection grammar (`hw=`/`serial=`/`name=`/`hwidx=`/`hwch=`) but addresses
by destination MAC/EtherType instead of a CAN id/bitrate.

## Example

```
VECTOR.DEVICES
VECTOR.CONFIG hw=VN1610 b=500000 x=0x123
VECTOR.CMD > H"AABBCCDD"
VECTOR.CMD < H"8"
```
