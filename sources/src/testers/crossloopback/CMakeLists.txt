cmake_minimum_required(VERSION 3.10)
project(cross_loopback LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(${PROJECT_NAME} 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loopback.cpp
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/inc
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../common   # tester_platform.h (POSIX / Winsock portability)
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
    target_compile_definitions(${PROJECT_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
endif()

# Winsock (TCP / UDP channels). The CAN (SocketCAN) and raw-Ethernet (AF_PACKET) channels are
# Linux-only and are compiled out elsewhere (LOOPBACK_HAVE_CAN / LOOPBACK_HAVE_RAWETH in common.hpp).
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
endif()



# raweth: (AF_PACKET raw sockets) needs CAP_NET_RAW to run:
#   sudo ./cross_loopback -i raweth:eth0 ...
# or, once after building:
#   sudo setcap cap_net_raw+ep $<TARGET_FILE:cross_loopback>
