cmake_minimum_required(VERSION 3.25)
project(uKI2C)

set(SOURCES src/uKI2CCommon.cpp)

if(UNIX AND NOT APPLE)
    list(APPEND SOURCES src/uKI2CLinux.cpp)
else()
    message(FATAL_ERROR "uKI2C is only supported on Linux (requires linux/i2c-dev.h)")
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

target_link_libraries(${PROJECT_NAME} PRIVATE
    uKmpMatch
)
