cmake_minimum_required(VERSION 3.25)
project(uUdp)

set(SOURCES src/uUdpCommon.cpp)

if(UNIX)
    # Plain BSD sockets (socket/connect/poll/getaddrinfo) — builds on Linux
    # and macOS alike, same as uEth.
    list(APPEND SOURCES src/uUdpPosix.cpp)
else()
    message(FATAL_ERROR "uUdp currently only ships a POSIX socket backend (Linux/macOS). "
                         "A Winsock backend would be needed for Windows.")
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
