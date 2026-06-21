cmake_minimum_required(VERSION 3.25)
project(uCH341)

set(SOURCES src/uCh341Common.cpp)

if(WIN32)
    list(APPEND SOURCES src/uCh341Windows.cpp)
elseif(UNIX)
    list(APPEND SOURCES src/uCh341Linux.cpp)
    list(APPEND SOURCES src/ch341.c)
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
