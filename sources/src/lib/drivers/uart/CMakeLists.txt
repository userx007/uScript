cmake_minimum_required(VERSION 3.16)
project(uUart)

set(SOURCES src/uUartCommon.cpp)

if(WIN32)
    list(APPEND SOURCES src/uUartWindows.cpp)
elseif(UNIX)
    list(APPEND SOURCES src/uUartLinux.cpp)
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
