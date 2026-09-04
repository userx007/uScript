# systec_plugin test harness

A `vcan`-based smoke-test setup for `systec_plugin`, mirroring `kvcan_plugin`'s
test harness. `systec_loopback` is a generic SocketCAN echo utility (identical
in design to `kvcan_plugin`'s `vcan_mirror`/`kvcan_loopback`) — it isn't
SYSTEC-specific, since at the SocketCAN layer a `vcan0` interface is
indistinguishable from a `can0` interface created by `systec_can.ko`.

## Setup

```bash
./systec_setup.sh    # modprobe vcan, create + bring up vcan0, run the mirror
```

In another terminal:

```bash
./test.sh             # cansend a 4-byte frame on vcan0
./dump.sh              # candump both the original and the mirrored echo
```

## What this does and doesn't cover

Covered — exercises the same code path the plugin uses for `CMD`/`SCRIPT`/`CYCLIC`:
- `uSystecCan::open()` / `close()` (raw `PF_CAN` socket, bind, `CAN_RAW_RECV_OWN_MSGS`)
- `set_filters()` / `SYSTEC.FILTER`
- `tout_read()` / `tout_write()` / `SYSTEC.CMD`, `SYSTEC.SCRIPT`, `SYSTEC.CYCLIC`

**Not** covered — `vcan` has none of `systec_can.ko`'s device/interface sysfs
attribute groups, so:
- `SYSTEC.HWCTRL <any key>` will fail here (sysfs node doesn't exist).
- Exercising `HWCTRL` requires a real SYS TEC USB-CANmodul with
  `systec_can.ko` loaded and bound (`can0`/`can1` created by the driver,
  `/sys/class/net/can0/device/devicenr` etc. present).

## Teardown

```bash
sudo ip link set down vcan0
sudo ip link delete vcan0
```
