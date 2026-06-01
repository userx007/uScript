cmake_minimum_required(VERSION 3.16)
project(uI2C)

set(SOURCES src/uI2CCommon.cpp)

if(UNIX AND NOT APPLE)
    list(APPEND SOURCES src/uI2CLinux.cpp)
else()
    message(FATAL_ERROR "uI2C is only supported on Linux (requires linux/i2c-dev.h)")
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
