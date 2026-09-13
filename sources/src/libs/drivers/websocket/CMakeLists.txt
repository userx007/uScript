cmake_minimum_required(VERSION 3.25)
project(uWebSocket)

set(SOURCES src/uWebSocketCommon.cpp)

# Unlike uTcpip/uUdp, this driver has no platform-specific backend at all: it
# rides entirely on top of uTcpip (HTTP handshake + WS framing are both plain
# C++), so it builds and runs anywhere uTcpip does - no UNIX guard needed here.

add_library(${PROJECT_NAME} STATIC ${SOURCES})

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    uICommDriver
    uUtils
    uTcpip
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    uKmpMatch
)
