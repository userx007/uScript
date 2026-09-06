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

Vector's XL Driver Library is proprietary, so — unlike the PCAN-Basic SDK
vendored under `uPcan/third_party/PCAN_Basic` — it is **not** bundled here.
Drop your own copy into `uVector/third_party/vxlapi/amd64/` (`vxlapi.h`,
`vxlapi64.lib`, `vxlapi64.dll`) — same vendoring layout `ftdi2xx` uses for
FTD2XX under `third_party/ftd2xx/amd64/`. No `-D` flags needed; it's found
automatically. Without those files, the build falls back to a minimal,
independently-written stub header (`uVector/third_party/vxlapi/include/vxlapi.h`)
that lets the source compile but will not link against a real driver — see
that file's header comment. Once vendored, the DLL's path is exported as
`VECTOR_RUNTIME_DLL` for whatever assembles the final `extlibs/` folder next
to `uscript` (same convention as `FTDI_RUNTIME_DLL`).

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
combined. `Vector::hwTypeToString()`/`hwTypeFromString()` only recognise a
subset of Vector's device types out of the box (the VN16xx/VN56xx/VN7xxx/
VN8xxx/VX1xxx family plus the legacy CANcardX/XL and CANcaseXL/CANboardXL
lines) — extend the lookup table in `uVector.cpp` for anything else, or
just pass the raw numeric `XL_HWTYPE_*` value to `hw=`.

## Differences from `pcan_plugin` / `kvcan_plugin`

| Key | PCAN                         | Vector                                          |
|-----|-------------------------------|--------------------------------------------------|
| `a` | n/a                            | Vector Hardware Config application name           |
| `i` | global PCAN channel handle     | zero-based channel index *within* that application |
| `hw`/`serial`/`name`/`hwidx`/`hwch` | n/a          | direct channel selection, bypassing `a`/`i` — see above |
| `f` | CAN FD toggle                  | accepted for grammar symmetry, but `1` is rejected — CAN FD isn't implemented |

Everything else — `b`/`x`/`y`/`r`/`w`/`s`/`e`/`t` and all the ISO-TP/J1939/
CANopen/Fast-Packet tuning keys, `FILTER`'s single-active-filter caveat,
`CMD`/`SCRIPT`/`CYCLIC` semantics — is identical to `pcan_plugin`.

## Example

```
VECTOR.DEVICES
VECTOR.CONFIG hw=VN1610 b=500000 x=0x123
VECTOR.CMD > H"AABBCCDD"
VECTOR.CMD < H"8"
```
