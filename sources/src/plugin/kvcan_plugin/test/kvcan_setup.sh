#!/bin/bash

sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0

# Specify another interface
# ./kvcan_loopback vcan1

# Run (defaults to vcan0)
./kvcan_loopback 

