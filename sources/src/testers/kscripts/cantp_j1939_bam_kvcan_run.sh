#!/bin/bash

sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0

# Run (defaults to vcan0)
konsole -e "bash -c '../bin/cantp_loopback j1939-bam vcan0 0x300 0x301 500'" &

