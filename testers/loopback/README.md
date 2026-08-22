# loopback — unified UART / CAN / TCP / UDP / raw-Ethernet bridge

Replaces the five standalone testers (`uart_loopback`, `kvcan_loopback`
a.k.a. `vcan_mirror`, `eth_loopback_server` (TCP), `udp_loopback_server`,
`eth_raw_loopback_server`) with **one** tool that can:

* mirror a channel back to itself, exactly as each original tool did, or
* bridge **any** of the five transports to **any** other, with an optional
  delay between receiving a message and sending it back out.

## Build

```
make
```

Produces `./loopback`. `raweth:` (raw Ethernet) needs `CAP_NET_RAW` to
run — either run as root, or `sudo setcap cap_net_raw+ep ./loopback` once.
`kvcan:` needs a SocketCAN interface (`vcan0`, `can0`, ...).

## Usage

```
loopback -i <input-spec> [-o <output-spec>] [-t <delay-ms>]
```

* `-i` — input channel (required). Whatever arrives here gets forwarded.
* `-o` — output channel (optional). **If omitted, the tool mirrors the
  input channel back to itself** — byte-for-byte the same behaviour the
  original per-protocol tools had. If `-o` names the *same* endpoint as
  `-i` (same CAN interface, same UART device, same TCP/UDP listen port,
  same raw-Eth interface), it is automatically collapsed into the same
  mirror behaviour rather than opening the resource twice.
* `-t` — delay in milliseconds between RX and the mirrored/forwarded TX
  (default: `0`).

Every message received and every message sent is dumped to stdout —
CAN frames in `candump`-style `ID  [DLC] bytes` form, everything else as
`[len] hex bytes` — exactly like the original tools already did.

## Spec syntax

```
uart:<device>[/<baud>]                        e.g. uart:/dev/tnt0/115200
kvcan:<iface>[/<can_id>]                       e.g. kvcan:vcan0/0x100
tcpip:[server/]<port>[/<bindaddr>]             e.g. tcpip:5000
tcpip:client/<host>/<port>                     e.g. tcpip:client/10.0.0.5/5000
udp:[server/]<port>[/<bindaddr>]               e.g. udp:5000
udp:client/<host>/<port>                       e.g. udp:client/10.0.0.5/5000
raweth:<ifname>[/<ethertype>][/promisc][/<mac>] e.g. raweth:eth0/0x88b5
```

`can` / `tcp` / `eth` are accepted as aliases for `kvcan` / `tcpip` /
`raweth`. The type separator after the keyword may be `:` or `/` — both
`uart:/dev/tnt0/115200` and `kvcan/vcan0/0x100` parse correctly.

**Server vs. client (TCP/UDP):** a bare port (`tcpip:5000`) listens —
the natural mode for an *input* (the tool waits for a peer to connect and
send it something), same as the original servers. `client/<host>/<port>`
dials out — the natural mode for a pure *output* channel, e.g. forwarding
UART traffic on to a remote TCP/UDP listener.

## Examples

```sh
# Original behaviours, unchanged:
./loopback -i uart:/dev/tnt0/115200          # UART -> UART mirror
./loopback -i kvcan:vcan0                    # CAN  -> CAN  mirror
./loopback -i tcpip:5000                     # TCP  -> TCP  mirror (echo server)
./loopback -i udp:5000                       # UDP  -> UDP  mirror (echo server)
./loopback -i raweth:eth0                    # raw-Eth -> raw-Eth mirror

# From your example:
./loopback -i uart:/dev/tnt0/115200 -o kvcan:vcan0/0x100 -t 500

# Any other combination works the same way:
./loopback -i kvcan:can0 -o tcpip:client/10.0.0.5/6000 -t 100
./loopback -i udp:6001   -o udp:client/127.0.0.1/6002
./loopback -i raweth:eth0/0x88b5 -o uart:/dev/ttyUSB0/115200
```

## CAN 8-byte limit

Classic CAN frames carry at most 8 bytes of payload. When the **output**
channel is `kvcan:`/`can:` and the message being forwarded is longer than
8 bytes, bytes 9 onward are dropped and a warning is logged, e.g.:

```
[kvcan:vcan0] WARNING: message is 14 bytes, CAN payload is limited to 8 - dropping bytes 9..14
```

The TX dump line reflects only the 8 bytes that were actually put on the
bus. This only applies when CAN is the *output*; an input CAN frame is,
naturally, never more than 8 bytes to begin with.

## Design notes

* **Mirror mode reuses the exact original logic per transport:**
  * `kvcan:` — one socket, `CAN_RAW_RECV_OWN_MSGS=0`, `CAN_RAW_LOOPBACK`
    left on — identical trick to `vcan_mirror.c`, and the reason two
    channels naming the *same* CAN interface are always collapsed into a
    single shared channel (two separate sockets on the same interface
    would otherwise cause an infinite echo storm, since the kernel
    delivers one socket's writes to the other).
  * `uart:` — single full-duplex fd, raw termios mode, read-then-write-back.
  * `tcpip:` — replies on the same accepted client socket.
  * `udp:` — replies to the address the last datagram came from.
  * `raweth:` — swaps source/destination MAC and replies on the same
    interface, skipping frames the socket sees because it sent them
    itself (`PACKET_OUTGOING`).
* **Bridging** (different `-i`/`-o` specs) uses each output channel's own
  fixed target: a CAN output uses its own configured arbitration ID (or,
  if none was given, whatever ID a CAN *input* last received); a TCP/UDP
  output in `client/` mode sends to its configured host:port; a raw-Eth
  output uses a configured/broadcast destination MAC when it isn't also
  replying to something it just captured itself.
* Ctrl-C / SIGTERM stop the tool cleanly after the current blocking call
  returns (signal handlers are installed without `SA_RESTART`, same as
  the original TCP/UDP/raw-Eth servers already did).

## Source layout

```
loopback.cpp           main(): arg parsing, channel wiring, RX/delay/TX loop
common.hpp              Message struct, logging, hex-dump helpers, g_stop
ichannel.hpp             abstract channel interface
channel_factory.hpp      spec-string parser -> channel objects
uart_channel.hpp          serial port driver
can_channel.hpp            SocketCAN driver (+ 8-byte truncation)
tcp_channel.hpp             TCP server/client driver
udp_channel.hpp               UDP server/client driver
raweth_channel.hpp             AF_PACKET raw-Ethernet driver
```
