cmake_minimum_required(VERSION 3.25)
project(uTcpip)

set(SOURCES src/uTcpipCommon.cpp)

if(WIN32)
    # Winsock2 (socket/connect/WSAPoll/getaddrinfo) — see uTcpipWindows.cpp's
    # header comment for how this mirrors the POSIX backend below.
    list(APPEND SOURCES src/uTcpipWindows.cpp)
elseif(UNIX)
    # Plain BSD sockets (socket/connect/poll/getaddrinfo)
    # platform-specific headers required, so this builds on Linux and macOS
    # alike, unlike uKVCan which is Linux-only (linux/can.h).
    list(APPEND SOURCES src/uTcpipPosix.cpp)
else()
    message(FATAL_ERROR "uTcpip only ships POSIX and Winsock socket backends "
                         "(Linux/macOS/Windows).")
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

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PUBLIC ws2_32)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    uKmpMatch
)
