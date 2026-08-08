# PROFIBUS Plugin Tutorial

## What this is

A PROFIBUS **FDL** (Fieldbus Data Link, layer 2, IEC 61158-2 / EN 50170) master, plus a
passive bus monitor, running over the existing UART/RS-485 driver. It lets you send the four
FDL request services — `SDN`, `SDA`, `SRD`, and Request-FDL-Status — to a station on the bus and
decode whatever comes back, or just listen to whatever traffic is already on the wire.

It does **not** implement the PROFIBUS-DP application layer (no GSD files, no `Slave_Diag` /
`Set_Prm` / `Chk_Cfg` / `Data_Exchange` SAP semantics, no cyclic DP state machine) and it does
**not** participate in the token ring (it never holds or passes a token — it only issues
point-to-point requests and reads whatever response, or other traffic, shows up). See
`profibus_driver.hpp`'s class doc comment for the full scope note, and the hardware/timing
limitations below — read those before pointing this at real field devices.

## Architecture

Same three-way split as the MQTT plugin:

```
profibus_protocol.{hpp,cpp}   Pure FDL telegram encode/decode. No I/O. FCB/FCV
                               security-sequence bookkeeping only.

profibus_driver.{hpp,cpp}     ICommDriver implementation. Wraps the existing UART
                               driver unmodified. Owns the serial port, the SYN
                               inter-telegram pause, the PROFIBUS.CMD sub-command
                               parsing, and the GUI comm-dump reporting.

profibus_plugin.{hpp,cpp}     Thin shell: CONFIG storage, INFO text, and wiring
                               ProfibusDriver::send()/receive() into
                               ucmdexec::generic_cmd()/generic_script().
```

## Known hardware/timing limitations — read this first

PROFIBUS FDL is specified for 8E1 (even-parity) serial framing, with the inter-telegram SYN
pause and per-telegram response deadline both guaranteed by dedicated silicon (e.g. Siemens
SPC3/ASPC2) on a real station. None of that is true of a generic UART:

- **Parity.** The underlying UART driver opens the port 8N1 (no parity) unconditionally. A
  real PROFIBUS-DP slave's UART will flag every octet this plugin sends as a parity error and
  discard it. This plugin talks successfully to a peer that itself tolerates or ignores parity
  (a bench/simulator slave built the same way) — not, out of the box, to unmodified production
  PROFIBUS-DP hardware.
- **Baud rate.** Of PROFIBUS's eight standard rates, only 9600, 19200, 500000, and
  (platform-dependent) 1500000/3000000 bit/s are reachable through the UART driver's
  `termios`-based rate table. 45450, 93750, 187500, 6000000, and 12000000 are not — `CONFIG
  b=...` rejects them outright rather than silently running at 9600.
- **SYN pause.** Approximated with a plain OS-level sleep before each send, sized from the
  configured baud rate. Best-effort, not a hardware-guaranteed deadline.
- **No token ring.** This is a single point-to-point master; it never holds or passes a token.

In short: a correct, checksummed FDL implementation aimed at bench/diagnostic use against
compatible peers — not a certified, interoperable-with-anything PROFIBUS-DP master stack.

## Commands

### CONFIG

| Key  | Meaning                              | Default |
|------|---------------------------------------|---------|
| `d`  | Serial device, e.g. `/dev/ttyUSB0`    | (none)  |
| `b`  | Baud rate — see limitation above       | 19200   |
| `a`  | This master's own station address (0-125) | 2   |
| `rt` | Response timeout, ms                   | 200     |
| `hp` | Default priority: `1`/`true`=high, `0`/`false`=low | low |
| `rb` | Read buffer size, bytes                | 256     |

```
PROFIBUS.CONFIG d=/dev/ttyUSB0 b=19200 a=2 rt=200
```

### CMD

One FDL exchange per line, on the plugin's single persistent session (the serial port is opened
once, on first use, and stays open — see `profibus_plugin.hpp`'s "Session lifetime").

```
PROFIBUS.CMD > SDN 127 0400          // broadcast, fire-and-forget, no response wait
PROFIBUS.CMD > SDA 5                 // send-with-ack, no data | expect ACK
PROFIBUS.CMD > SDA 5 | ACK
PROFIBUS.CMD > SRD 5 0102 | 0304     // send+request data — assert the reply hex
PROFIBUS.CMD > STATUS 5              // Request FDL Status | expect e.g. SLAVE:OK
PROFIBUS.CMD <                       // one blocking passive-bus-monitor read
line ?= PROFIBUS.CMD < &             // background monitor thread
```

- `hexdata` is plain hex (`0A1B`), no separators, even digit count.
- Station addresses are 0-127. 127 is the FDL broadcast address (`SDN` only). 126 is reserved
  for commissioning tools.
- `SRD`'s reply text is the response payload in hex, or a status word (`NO_RESPONSE_DATA`,
  `USER_ERROR`, ...) when the reply carries no data.
- `STATUS`'s reply text is `<station-type>:<status>`, e.g. `SLAVE:OK`,
  `MASTER_READY_IN_RING:OK`.
- **Always pair a `>` line that expects a response (`SDA`/`SRD`/`STATUS`) with its
  `| expected`.** If you omit it, the response is left unread on the wire and the *next*
  `PROFIBUS.CMD <` will read (and mismatch against) it instead of fresh bus traffic — the same
  gotcha the MQTT plugin has for unpaired `PUBLISH`/`SUBSCRIBE` lines.
- The GUI comm-dump panel shows the real telegram bytes exchanged, one row per complete
  telegram — including malformed ones, which is deliberate for a bench/diagnostic tool.

### SCRIPT

Runs a file of `PROFIBUS.CMD`-style lines over the same persistent session:

```
PROFIBUS.SCRIPT script.txt
```

## Worked example

Talking to a slave at station address 5, master at address 2:

```
PROFIBUS.CONFIG d=/dev/ttyUSB0 b=19200 a=2 rt=200
PROFIBUS.CMD > SDA 5 | ACK
PROFIBUS.CMD > SRD 5 0102 | 0304
PROFIBUS.CMD > STATUS 5 | SLAVE:OK
```

## Testing without real hardware

Every command above (`SDN`, `SDA`, `SRD`, `STATUS`, and the standalone bus-monitor `<`) was
exercised end-to-end against a small Python FDL slave simulator, connected over a PTY pair, as
part of building this plugin — the driver correctly builds/sends each request telegram,
matches the response to the right station/service, verifies its checksum, and decodes it. The
same approach (a PTY pair plus a minimal Python or C responder speaking the FDL wire format
from `profibus_protocol.hpp`) is a convenient way to exercise this plugin without physical
PROFIBUS hardware — keep in mind PTYs have terminal-emulation semantics (echo, canonical mode)
that a real serial port does not, so disable those (`termios` raw mode) on whichever side you
control before writing to it.
