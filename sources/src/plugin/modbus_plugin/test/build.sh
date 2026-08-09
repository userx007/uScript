#!/bin/bash
set -e
g++ -std=c++20 -O2 -pthread -o modbus_server modbus_server.cpp
echo "Built ./modbus_server"
