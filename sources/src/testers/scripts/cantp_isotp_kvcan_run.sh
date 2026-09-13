#!/bin/bash

sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0

# Run (defaults to vcan0)
../bin/cantp_loopback isotp vcan0 0x100 0x101 500

