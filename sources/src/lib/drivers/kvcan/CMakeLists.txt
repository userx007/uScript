cmake_minimum_required(VERSION 3.25)
project(uKVCan)

set(SOURCES src/uKVCanCommon.cpp)

if(UNIX AND NOT APPLE)
    list(APPEND SOURCES src/uKVCanLinux.cpp)
else()
    message(FATAL_ERROR "uKVCan is only supported on Linux (requires linux/can.h and linux/can/raw.h)")
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
