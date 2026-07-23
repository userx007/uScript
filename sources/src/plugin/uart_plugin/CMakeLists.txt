
cmake_minimum_required(VERSION 3.25)
project(uart_plugin)

add_library(${PROJECT_NAME}
    SHARED
        src/uart_plugin.cpp
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
        uICoreScript
        uCommScriptClient
        uCommScriptCommandInterpreter
        uScriptReader
        uPluginOps
        uUart
)
