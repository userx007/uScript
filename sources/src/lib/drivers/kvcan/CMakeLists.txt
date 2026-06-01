cmake_minimum_required(VERSION 3.16)
project(uCan)

set(SOURCES src/uCanCommon.cpp)

if(UNIX AND NOT APPLE)
    list(APPEND SOURCES src/uCanLinux.cpp)
else()
    message(FATAL_ERROR "uCan is only supported on Linux (requires linux/can.h and linux/can/raw.h)")
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
