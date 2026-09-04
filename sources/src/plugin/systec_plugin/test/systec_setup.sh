#!/bin/bash

sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
# No MTU bump needed here (unlike kvcan_plugin's setup): systec_can.ko is
# classic-CAN only (see docs/README.md "Why Classic CAN Only"), so the
# default vcan0 MTU (16, CAN_MTU) already matches what real SYS TEC
# USB-CANmodul hardware supports — every frame this plugin sends is <= 8
# bytes. Note: this vcan interface has none of the systec_can.ko-specific
# sysfs nodes (devicenr, reset, dual_channel, status_timeout,
# high_performance, channel, tx_timeout_ms), so SYSTEC.HWCTRL calls will
# fail here with a "cannot open" sysfs error — HWCTRL can only be
# exercised against a real USB-CANmodul with systec_can.ko loaded.
sudo ip link set up vcan0

# Specify another interface
# ./systec_loopback vcan1

# Run (defaults to vcan0)
./systec_loopback
