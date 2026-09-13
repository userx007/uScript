cmake_minimum_required(VERSION 3.25)


# --- UART ---
# uart_loopback — plain C, POSIX termios (see uart_plugin/test/uart_loopback.c)
add_executable(uart_loopback
    src/uart_loopback.c
)


# --- TCPIP ---
# tcpip_loopback_server — C++17, plain BSD sockets (see tcpip_plugin/test/)
add_executable(tcpip_loopback_server
    src/tcpip_loopback_server.cpp
)
target_compile_features(tcpip_loopback_server PRIVATE cxx_std_17)


# --- UDP ---
# udp_loopback_server — C++17, plain BSD sockets (see udp_plugin/test/)
add_executable(udp_loopback_server
    src/udp_loopback_server.cpp
)
target_compile_features(udp_loopback_server PRIVATE cxx_std_17)


# --- Websocket ---
# websocket_loopback_server — C++17, plain BSD sockets (see udp_plugin/test/)
add_executable(websocket_loopback_server
    src/websocket_loopback_server.cpp
)
target_compile_features(udp_loopback_server PRIVATE cxx_std_17)


if(UNIX AND NOT APPLE)

# --- KVCAN ---
    # kvcan_loopback — plain C, SocketCAN raw sockets (see kvcan_plugin/test/)
    add_executable(kvcan_loopback
        src/kvcan_loopback.c
    )

# --- RAW-ETH ---
    # eth_raw_loopback_server — C++17, AF_PACKET/SOCK_RAW (see raweth_plugin/test/)
    add_executable(eth_raw_loopback_server
        src/eth_raw_loopback_server.cpp
    )
    target_compile_features(eth_raw_loopback_server PRIVATE cxx_std_17)

endif()
