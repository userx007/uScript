cmake_minimum_required(VERSION 3.25)

# Each tester is built only when the plugin it exercises is enabled.

# --- UART ---
# uart_loopback — plain C, POSIX termios on Linux / Win32 comm API on Windows (see uart_plugin/test/uart_loopback.c)
if(USCRIPT_PLUGIN_UART)
    add_executable(uart_loopback
        src/uart_loopback.c
    )
    if(MSVC)
        target_compile_definitions(uart_loopback PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
endif()


# --- TCPIP ---
# tcpip_loopback_server — C++17, plain BSD sockets (see tcpip_plugin/test/)
if(USCRIPT_PLUGIN_TCPIP)
    add_executable(tcpip_loopback_server
        src/tcpip_loopback_server.cpp
    )
    target_compile_features(tcpip_loopback_server PRIVATE cxx_std_17)
    target_include_directories(tcpip_loopback_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common) # tester_platform.h
    if(WIN32)
        target_link_libraries(tcpip_loopback_server PRIVATE ws2_32)
    endif()
endif()


# --- UDP ---
# udp_loopback_server — C++17, plain BSD sockets (see udp_plugin/test/)
if(USCRIPT_PLUGIN_UDP)
    add_executable(udp_loopback_server
        src/udp_loopback_server.cpp
    )
    target_compile_features(udp_loopback_server PRIVATE cxx_std_17)
    target_include_directories(udp_loopback_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common) # tester_platform.h
    if(WIN32)
        target_link_libraries(udp_loopback_server PRIVATE ws2_32)
    endif()
endif()


# --- Websocket ---
# websocket_loopback_server — C++17, plain BSD sockets (see udp_plugin/test/)
if(USCRIPT_PLUGIN_WEBSOCKET)
    add_executable(websocket_loopback_server
        src/websocket_loopback_server.cpp
    )
    target_compile_features(websocket_loopback_server PRIVATE cxx_std_17)
    target_include_directories(websocket_loopback_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common) # tester_platform.h
    if(WIN32)
        target_link_libraries(websocket_loopback_server PRIVATE ws2_32)
    endif()
endif()


# --- KVCAN ---
# kvcan_loopback — plain C, SocketCAN raw sockets (see kvcan_plugin/test/)
# (USCRIPT_PLUGIN_KVCAN is forced OFF on non-Linux platforms)
if(USCRIPT_PLUGIN_KVCAN)
    add_executable(kvcan_loopback
        src/kvcan_loopback.c
    )
endif()

# --- RAW-ETH ---
# eth_raw_loopback_server — C++17, AF_PACKET/SOCK_RAW (see raweth_plugin/test/)
# (USCRIPT_PLUGIN_RAWETH is forced OFF on non-Linux platforms)
if(USCRIPT_PLUGIN_RAWETH)
    add_executable(eth_raw_loopback_server
        src/eth_raw_loopback_server.cpp
    )
    target_compile_features(eth_raw_loopback_server PRIVATE cxx_std_17)
endif()
