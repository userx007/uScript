#!/bin/bash

# Create a veth pair: veth0 and veth1
sudo ip link add veth0 type veth peer name veth1

# Bring up both interfaces
sudo ip link set veth0 up
sudo ip link set veth1 up

# Now run your server
sudo setcap cap_net_raw+ep ../bin/eth_raw_loopback_server
RAWETH_ABS_PATH=$(realpath "../bin/eth_raw_loopback_server")

echo "Launching server on veth0..."
konsole --noclose -e bash -c "${RAWETH_ABS_PATH} veth0 --promisc; read -p 'Press Enter to close...'" &
