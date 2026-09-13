cmake_minimum_required(VERSION 3.25)
project(ddsty_plugin)

add_library(${PROJECT_NAME}
    SHARED
        src/dds_typed_plugin.cpp
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/inc
        ${CMAKE_CURRENT_SOURCE_DIR}/inc/private
)

target_link_libraries( ${PROJECT_NAME}
    PUBLIC
        uPluginSettings
        uCommandExec
        uIPlugin
        uICommDriver
        uPluginOps
        uCommScriptClient
        uCommScriptCommandInterpreter
        uScriptReader
        uDdsTypedDriver
)

add_subdirectory(examples)