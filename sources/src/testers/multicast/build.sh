#! /bin/bash

# Compile
g++ -o sender sender.cpp
g++ -o receiver receiver.cpp

# Enable multicast on lo (if using loopback)
sudo ip link set lo multicast on

sudo sysctl -w net.ipv4.conf.lo.route_localnet=1


# verify
sudo tcpdump -v -i lo udp port 11111
