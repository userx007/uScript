#!/bin/bash

RAWETH_ABS_PATH=$(realpath "../bin/eth_raw_loopback_server")

sudo setcap cap_net_raw+ep $RAWETH_ABS_PATH
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0

konsole -e "bash -c './uart_run.sh; exec bash'" &

konsole  -e "bash -c './kvcan_run.sh; exec bash'" &

konsole  -e "bash -c './tcpip_run.sh; exec bash'" &

konsole  -e "bash -c './udp_run.sh; exec bash'" &

#konsole  -e "bash -c './raweth_run.sh; exec bash'" &
