#!/bin/bash

RAWETH_ABS_PATH=$(realpath "../bin/eth_raw_loopback_server")

sudo setcap cap_net_raw+ep $RAWETH_ABS_PATH
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0

#konsole -e "bash -c './can_single_frame_kvcan_run.sh; exec bash'" &
konsole -e "bash -c './cantp_canopen_kvcan_run.sh; exec bash'" &
konsole -e "bash -c './cantp_isotp_kvcan_run.sh; exec bash'" &
konsole -e "bash -c './cantp_j1939_bam_kvcan_run.sh; exec bash'" &
konsole -e "bash -c './cantp_j1939_rtscts_kvcan_run.sh; exec bash'" &
konsole -e "bash -c './cantp_nmea2000_kvcan_run.sh; exec bash'" &
konsole -e "bash -c './konsole_all.sh; exec bash'" &
konsole -e "bash -c './raweth_run.sh; exec bash'" &
konsole -e "bash -c './tcpip_run.sh; exec bash'" &
konsole -e "bash -c './uart_run.sh; exec bash'" &
konsole -e "bash -c './udp_run.sh; exec bash'" &
