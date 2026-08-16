cmake_minimum_required(VERSION 3.25)
project(uProfibusProtocol)

add_library(${PROJECT_NAME} STATIC
    src/profibus_protocol.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)
