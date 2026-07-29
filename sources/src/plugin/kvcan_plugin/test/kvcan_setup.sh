#!/bin/bash

sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
# Raise the MTU to CANFD_MTU (72) so KVCAN can transmit CAN-FD frames
# (payloads of 9-64 bytes) on this interface. Without this, KVCAN::open()'s
# CAN_RAW_FD_FRAMES setsockopt still succeeds, but every write() of a frame
# longer than 8 bytes fails at the link layer (frame > interface MTU) —
# this only matters for CAN_TP_PROTOCOL=none; ISO-TP/J1939 payloads always
# fit in classic 8-byte frames and work on the default (16) MTU too.
sudo ip link set vcan0 mtu 72
sudo ip link set up vcan0

# Specify another interface
# ./kvcan_loopback vcan1

# Run (defaults to vcan0)
./kvcan_loopback 

