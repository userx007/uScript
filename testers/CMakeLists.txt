cmake_minimum_required(VERSION 3.25)

# ─────────────────────────────────────────────────────────────────────────────
# Standalone loopback/test executables installed under
# bin/<os>/testers/bin (see INSTALL_TESTERS_DIR in the top-level
# CMakeLists.txt).
#
# NOTE: cantp_loopback is NOT declared here — it already lives (and is
# already `project()`-ed and `add_executable()`-d) under
# sources/src/lib/protocols/cantp/loopback, which is pulled in earlier via
# add_subdirectory(sources) in the top-level CMakeLists.txt, before this
# directory is ever reached. Redeclaring it here would just be a duplicate,
# conflicting target definition.
#
# Every executable below is a genuinely standalone test tool per its own
# header comment (no ICommDriver/project-header dependency, single
# translation unit, single compiler invocation) - each one's inline "Build:"
# comment and matching test/build.sh are reproduced here as an add_executable()
# instead, so they get installed alongside the other testers rather than only
# buildable by manually invoking gcc/g++ from within that plugin's test/ dir.
# ─────────────────────────────────────────────────────────────────────────────

# uart_loopback — plain C, POSIX termios (see uart_plugin/test/uart_loopback.c)
add_executable(uart_loopback
    ${CMAKE_SOURCE_DIR}/sources/src/plugin/uart_plugin/test/uart_loopback.c
)

# tcpip_loopback_server — C++17, plain BSD sockets (see tcpip_plugin/test/)
add_executable(tcpip_loopback_server
    ${CMAKE_SOURCE_DIR}/sources/src/plugin/tcpip_plugin/test/tcpip_loopback_server.cpp
)
target_compile_features(tcpip_loopback_server PRIVATE cxx_std_17)

# udp_loopback_server — C++17, plain BSD sockets (see udp_plugin/test/)
add_executable(udp_loopback_server
    ${CMAKE_SOURCE_DIR}/sources/src/plugin/udp_plugin/test/udp_loopback_server.cpp
)
target_compile_features(udp_loopback_server PRIVATE cxx_std_17)

# ─────────────────────────────────────────────────────────────────────────────
# Linux-kernel-only testers (SocketCAN / AF_PACKET) — mirrors the
# UNIX-AND-NOT-APPLE guard around TESTER_TARGETS_LINUX_ONLY in the
# top-level CMakeLists.txt; these two sources use headers
# (linux/can.h+linux/can/raw.h, AF_PACKET) that only exist on Linux, so
# only declare the targets there.
# ─────────────────────────────────────────────────────────────────────────────
if(UNIX AND NOT APPLE)
    # kvcan_loopback — plain C, SocketCAN raw sockets (see kvcan_plugin/test/)
    add_executable(kvcan_loopback
        ${CMAKE_SOURCE_DIR}/sources/src/plugin/kvcan_plugin/test/kvcan_loopback.c
    )

    # eth_raw_loopback_server — C++17, AF_PACKET/SOCK_RAW (see raweth_plugin/test/)
    add_executable(eth_raw_loopback_server
        ${CMAKE_SOURCE_DIR}/sources/src/plugin/raweth_plugin/test/eth_raw_loopback_server.cpp
    )
    target_compile_features(eth_raw_loopback_server PRIVATE cxx_std_17)
endif()
