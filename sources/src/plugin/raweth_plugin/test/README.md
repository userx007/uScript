# eth_raw_loopback_server — usage guide (with `veth`)

`eth_raw_loopback_server` listens on a network interface at the raw
Ethernet layer (`AF_PACKET`/`SOCK_RAW`) and echoes every frame it sees
back onto the wire, with source/destination MAC addresses swapped. Since
it works at layer 2, you don't need a real NIC or physical network to test
it — a **`veth` pair** (a virtual, in-kernel "patch cable" between two
interfaces) is the standard way to exercise it entirely on one Linux box.

This guide covers obtaining/installing what you need, setting up the veth
pair, building and running the server, and sending test frames against it.

Linux only — `AF_PACKET` and `veth` are Linux kernel features.

---

## 1. What a veth pair is, and why use one here

A `veth` pair is two virtual network interfaces that are connected to each
other directly in the kernel: any frame sent out one end appears on the
other end, like a crossover cable. That makes it a convenient, disposable
stand-in for a real link when testing raw-Ethernet code:

- No physical NIC, switch, or cabling required.
- Traffic never leaves the machine — safe to send arbitrary/malformed test
  frames without disturbing a real network.
- Both ends can be put in the same network namespace (simplest) or
  separate namespaces (closer to testing two independent hosts).

You'll run `eth_raw_loopback_server` on one end (say `veth0`) and send
test frames from the other end (`veth1`); the server should echo each one
back, which you'll see arrive on `veth1`.

---

## 2. Prerequisites and installation

You need:

| Tool | Purpose | Typical package |
|---|---|---|
| A C++17 compiler | build the server | `build-essential` (Debian/Ubuntu) or `gcc-c++` (Fedora/RHEL) |
| `ip` (iproute2) | create/manage the veth pair | `iproute2` |
| `tcpdump` (optional) | independently sniff traffic while debugging | `tcpdump` |
| `python3` + `scapy` (optional) | easiest way to hand-craft and send test frames | `python3`, `pip install scapy` |

Debian/Ubuntu:
```bash
sudo apt-get update
sudo apt-get install -y build-essential iproute2 tcpdump python3-pip
pip3 install scapy
```

Fedora/RHEL:
```bash
sudo dnf install -y gcc-c++ iproute tcpdump python3-pip
pip3 install scapy
```

`iproute2` and a C++ compiler are all that's strictly required; `tcpdump`
and `scapy` are convenience tools for observing/sending traffic and are
optional if you'd rather write your own sender.

You'll need root (or `sudo`) for creating the veth pair and for running
the server, since raw sockets require the `CAP_NET_RAW` capability — see
§6 for how to avoid needing `sudo` for the server binary itself.

---

## 3. Create the veth pair

```bash
# Create the pair: veth0 <-> veth1
sudo ip link add veth0 type veth peer name veth1

# Bring both ends up
sudo ip link set veth0 up
sudo ip link set veth1 up

# Sanity check
ip link show veth0
ip link show veth1
```

No IP addresses are needed — the server and this guide's send methods
operate below IP, directly on Ethernet frames.

If you want a more realistic two-host setup, move `veth1` into its own
network namespace instead:
```bash
sudo ip netns add testns
sudo ip link set veth1 netns testns
sudo ip netns exec testns ip link set veth1 up
sudo ip netns exec testns ip link set lo up
```
Everything below still applies — just prefix commands touching `veth1`
with `sudo ip netns exec testns ...`. The simple (no-namespace) setup is
enough for most driver/protocol testing, so the rest of this guide assumes
that.

---

## 4. Build the server

```bash
g++ -std=c++17 -O2 -Wall -Wextra -o eth_raw_loopback_server eth_raw_loopback_server.cpp
```

---

## 5. Run the server on `veth0`

```bash
sudo ./eth_raw_loopback_server veth0
```

You should see:
```
eth_raw_loopback_server listening on veth0 (mac aa:bb:cc:dd:ee:ff), ethertype=0x0003 (Ctrl+C to stop)
```

Leave this running in one terminal.

Optional arguments (see the tool's own `--help`-style usage banner if run
with no arguments):
```bash
sudo ./eth_raw_loopback_server veth0 0x88b5      # only echo EtherType 0x88B5
sudo ./eth_raw_loopback_server veth0 --promisc   # also capture frames not addressed to veth0's MAC
```

---

## 6. Avoid needing `sudo` for the server (optional)

Instead of running as root every time, grant the binary `CAP_NET_RAW`
once:
```bash
sudo setcap cap_net_raw+ep ./eth_raw_loopback_server
./eth_raw_loopback_server veth0   # no sudo needed from here on
```
(You'll still need `sudo` for the one-time veth setup in §3, since that's
a separate privileged operation.)

---

## 7. Send test frames from `veth1`

The server doesn't care what EtherType or payload it sees — it echoes
whatever arrives. The easiest way to send an arbitrary raw frame is a
short `scapy` one-liner in a second terminal:

```bash
sudo python3 - <<'EOF'
from scapy.all import Ether, sendp

# Find veth1's own MAC so the "reply" (which the server addresses back to
# whatever source MAC it saw) has somewhere real to go.
import subprocess
mac = subprocess.check_output(["cat", "/sys/class/net/veth1/address"]).decode().strip()

frame = Ether(src=mac, dst="ff:ff:ff:ff:ff:ff", type=0x88B5) / b"hello raw ethernet"
sendp(frame, iface="veth1")
EOF
```

To also *see* the echoed reply come back on `veth1`, sniff it in a third
terminal before (or while) sending:
```bash
sudo tcpdump -i veth1 -e -xx
```
or capture with scapy instead of tcpdump:
```bash
sudo python3 - <<'EOF'
from scapy.all import sniff
sniff(iface="veth1", count=1, prn=lambda p: p.show())
EOF
```

In the server's terminal you should see a line like:
```
[be:2f:.. -> ff:ff:ff:ff:ff:ff] ethertype=0x88b5, echoing 32 bytes
```
and the sniffer on `veth1` should show a frame coming back addressed to
`veth1`'s own MAC, with source set to `veth0`'s MAC — same EtherType and
payload, unchanged.

---

## 8. Cleanup

Stop the server with `Ctrl+C`, then remove the veth pair (deleting either
end deletes both):
```bash
sudo ip link delete veth0
```
If you created the `testns` namespace in §3:
```bash
sudo ip netns delete testns
```

---

## 9. Troubleshooting

- **`socket(AF_PACKET, SOCK_RAW) failed` / permission denied** — you're
  missing `CAP_NET_RAW`. Run with `sudo`, or grant the capability as in
  §6.
- **Server starts but never logs anything** — double-check you're sending
  on `veth1` (the *peer* of the interface the server is bound to), and
  that both ends are `up` (`ip link show`). If you passed an EtherType
  filter, confirm your test frame's `type` field matches it.
- **No echo seen on the sniffer** — the server explicitly ignores frames
  it recognizes as its own outgoing traffic (`PACKET_OUTGOING`, or source
  MAC matching its own); this is expected and prevents infinite echo
  loops. Make sure the frame you *sent* had `veth1`'s MAC as source, not
  `veth0`'s.
- **`RTNETLINK answers: Operation not permitted`** while creating the veth
  pair — you need `sudo`/root for `ip link add`.
