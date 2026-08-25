cmake_minimum_required(VERSION 3.10)
project(generic_loopback LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(generic_loopback loopback.cpp)

target_compile_options(generic_loopback PRIVATE -Wall -Wextra)

# Headers (common.hpp, ichannel.hpp, channel_factory.hpp, *_channel.hpp)
# live alongside loopback.cpp and are picked up via #include "..."; no
# extra include_directories() needed as long as they stay in this folder.

# raweth: (AF_PACKET raw sockets) needs CAP_NET_RAW to run:
#   sudo ./generic_loopback -i raweth:eth0 ...
# or, once after building:
#   sudo setcap cap_net_raw+ep $<TARGET_FILE:generic_loopback>
