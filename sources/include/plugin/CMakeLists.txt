cmake_minimum_required(VERSION 3.16)

add_library(uIPlugin
    INTERFACE
)

target_include_directories(uIPlugin
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
        $<INSTALL_INTERFACE:include>
)