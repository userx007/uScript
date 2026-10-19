#!/bin/bash

sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0

konsole -e "bash -c '../../testers/bin/cross_loopback -i uart:/dev/tnt1/115200 -o kcan:vcan0/0x100 -t 500'" &
