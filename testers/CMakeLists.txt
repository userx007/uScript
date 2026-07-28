cmake_minimum_required(VERSION 3.15)
project(LoopbackSuite LANGUAGES C CXX)

add_executable(raweth_raw_loopback_server src/eth_raw_loopback_server.cpp)
add_executable(tcpip_tcpip_loopback_server src/tcpip_loopback_server.cpp)
add_executable(udp_udp_loopback_server src/udp_loopback_server.cpp)
add_executable(kvcan_kvcan_loopback src/kvcan_loopback.c)
add_executable(uart_uart_loopback src/uart_loopback.c)
