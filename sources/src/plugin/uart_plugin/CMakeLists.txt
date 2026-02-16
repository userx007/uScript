
cmake_minimum_required(VERSION 3.3)
project(uart_plugin)

add_library(${PROJECT_NAME}
    SHARED
        src/uart_plugin.cpp
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${PROJECT_SOURCE_DIR}/inc
)

target_link_libraries( ${PROJECT_NAME}
    PUBLIC
        PluginInterfaces
        ScriptInterfaces
        uCommScriptClient
        CommScriptCommandInterpreter
        ScriptReader
        uPluginOps
        uUart
)
