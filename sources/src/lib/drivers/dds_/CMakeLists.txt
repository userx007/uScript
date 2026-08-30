cmake_minimum_required(VERSION 3.25)
project(uDdsDriver)

if(NOT UNIX)
    message(FATAL_ERROR "uDdsDriver currently only ships a POSIX socket backend (Linux/macOS), "
                         "same constraint as uUdp — multicast group management needs a Winsock "
                         "backend to support Windows.")
endif()

add_library(${PROJECT_NAME} STATIC
    src/dds_driver.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    uICommDriver
    uUtils
    uDdsProtocol
)
