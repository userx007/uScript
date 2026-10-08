#!/bin/bash
# Prepare a REAL CAN adapter (e.g. PEAK PCAN-USB in SocketCAN mode) for the KCAN plugin.
#
# NOTE: the KCAN plugin can do the bitrate/link part itself (CONFIG b=500000, or KCAN.UP) when it
# runs as root / with CAP_NET_ADMIN. Use this script when you prefer to configure the interface
# from the outside (e.g. the plugin runs unprivileged: it then just uses the interface as-is).
#
#   ./kcan_setup.sh [iface] [bitrate] [sample_point]     e.g.  ./kcan_setup.sh can0 500000 0.875

IFACE=${1:-can0}
BITRATE=${2:-125000}
SP=${3:-0.875}

sudo modprobe can
sudo modprobe can_raw
sudo modprobe gs_usb          # PEAK PCAN-USB / USB Pro / FD; use gs_usb for candleLight, kvaser_usb for Kvaser ...

sudo ip link set "$IFACE" down 2>/dev/null
if [ -n "$SP" ]; then
    sudo ip link set "$IFACE" type can bitrate "$BITRATE" sample-point "$SP"
else
    sudo ip link set "$IFACE" type can bitrate "$BITRATE"
fi
sudo ip link set "$IFACE" up
ip -details link show "$IFACE"
