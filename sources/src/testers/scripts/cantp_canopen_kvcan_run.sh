#!/bin/bash

sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0

# Run (defaults to vcan0)
../bin/cantp_loopback canopen vcan0 0x500 0x501 500

