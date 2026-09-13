cmake_minimum_required(VERSION 3.25)
project(uSystecCan)

set(SOURCES src/uSystecCanCommon.cpp)

if(UNIX AND NOT APPLE)
    list(APPEND SOURCES src/uSystecCanLinux.cpp)
else()
    message(FATAL_ERROR "uSystecCan is only supported on Linux (requires linux/can.h, linux/can/raw.h and the systec_can.ko sysfs layout)")
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
