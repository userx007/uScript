cmake_minimum_required(VERSION 3.10)

# Multicast loopback demo pair (formerly built by build.sh only).
#   multicast_receiver  - joins 239.1.1.1:11111 and prints what it gets
#   multicast_sender    - sends one datagram to the group
# See sender.cpp for the interface choice (loopback on Linux, default interface on Windows).

foreach(tool sender receiver)
    add_executable(multicast_${tool} ${tool}.cpp)
    target_compile_features(multicast_${tool} PRIVATE cxx_std_17)
    target_include_directories(multicast_${tool} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common) # tester_platform.h
    if(MSVC)
        target_compile_options(multicast_${tool} PRIVATE /W4)
        target_compile_definitions(multicast_${tool} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(multicast_${tool} PRIVATE -Wall -Wextra)
    endif()
    if(WIN32)
        target_link_libraries(multicast_${tool} PRIVATE ws2_32)
    endif()
endforeach()
