# Using KCAN with a PEAK PCAN-USB (SocketCAN mode)

No PEAK driver package is needed: the in-kernel `peak_usb` module drives PCAN-USB, PCAN-USB Pro and
PCAN-USB FD. (This is different from the `pcan` plugin, which uses PEAK's PCAN-Basic library.)

```
sudo modprobe can can_raw peak_usb
ip link show                         # a "canN" appears when the adapter is plugged in
```

Bitrate/up either via KCAN (`KCAN.CONFIG i=can0 b=500000`, privileged) or externally
(`testers/scripts/kcan_setup.sh can0 500000`).

Unprivileged use: let the system do the setup, e.g. `/etc/systemd/network/80-can0.network`

```
[Match]
Name=can0
[CAN]
BitRate=500K
```
(`sudo systemctl enable --now systemd-networkd`), or grant the host application `cap_net_admin`:
`sudo setcap cap_net_admin+ep <application>`.

Other adapters work the same way once their kernel driver created a `canN`: `gs_usb` (candleLight),
`kvaser_usb`, `mcp251x`/`mcp251xfd` (SPI), `slcan` via `slcand` (serial), …
If you do not want the adapter to claim `can0` after a reboot, add a udev rule that names it.

Self test without wiring: `KCAN.CONFIG i=can0 b=500000 lb=on` puts the controller in internal loopback, so frames sent by KCAN are
received by *other* processes on the host (e.g. `candump can0` from can-utils) — the KCAN socket itself does not see its own frames.
