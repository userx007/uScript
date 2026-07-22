#!/bin/bash

# Usage:
#   sudo ./eth_raw_loopback_server <ifname> [ethertype_hex] [--promisc]
#
#   ifname         Interface to listen/echo on (e.g. "eth0", "veth0", or
#                   "lo" for local testing — "lo" does carry a (fake)
#                   Ethernet header under AF_PACKET, so it works fine for
#                   exercising a driver's frame-parsing code).
#   ethertype_hex  Only capture/echo this EtherType, e.g. "0x88b5". If
#                   omitted, captures ETH_P_ALL (every EtherType).
#   --promisc      Put the interface into promiscuous mode so frames not
#                   addressed to this host's own MAC are captured too.
#                   Needed if the peer under test sends to some MAC other
#                   than this box's real NIC address.
#
# Requires CAP_NET_RAW (typically: run as root, or
#   sudo setcap cap_net_raw+ep ./eth_raw_loopback_server
# once, then run unprivileged).

../bin/eth_raw_loopback_server veth0 --promisc