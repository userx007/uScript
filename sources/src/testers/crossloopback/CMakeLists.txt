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
)

target_compile_options(${PROJECT_NAME} 
    PRIVATE 
    -Wall -Wextra
)



# raweth: (AF_PACKET raw sockets) needs CAP_NET_RAW to run:
#   sudo ./cross_loopback -i raweth:eth0 ...
# or, once after building:
#   sudo setcap cap_net_raw+ep $<TARGET_FILE:cross_loopback>
