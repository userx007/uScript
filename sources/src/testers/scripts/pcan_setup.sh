#! /bin/bash

sudo modprobe peak_usb
sudo modprobe can_raw
sudo modprobe can
sudo ip link set can0 up type can bitrate 125000
ip -details link show can0
