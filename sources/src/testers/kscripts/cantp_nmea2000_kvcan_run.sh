#!/bin/bash

sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0

# Run (defaults to vcan0)
konsole -e "bash -c '../bin/cantp_loopback nmea2000 vcan0 0x400 0x401 500'" &
