cmake_minimum_required(VERSION 3.25)
project(uRawEth)

set(SOURCES src/uRawEthCommon.cpp)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # AF_PACKET raw sockets (socket/bind/recvfrom/sendto on netpacket/packet.h)
    # are a Linux-specific facility, not portable BSD sockets — unlike
    # uTcpip, which builds on Linux and macOS alike. Like uKVCan, this
    # driver is Linux-only.
    list(APPEND SOURCES src/uRawEthLinux.cpp)
else()
    message(FATAL_ERROR "uRawEth requires AF_PACKET raw sockets (linux/if_packet.h) "
                         "and only ships a Linux backend.")
endif()

add_library(${PROJECT_NAME} STATIC ${SOURCES})

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    uICommDriver
    uUtils
)
