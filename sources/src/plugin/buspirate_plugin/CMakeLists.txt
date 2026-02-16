
cmake_minimum_required(VERSION 3.3)
project(buspirate_plugin)

add_library(${PROJECT_NAME}
    SHARED
        src/buspirate_plugin.cpp
        src/buspirate_generic.cpp
        src/buspirate_spi.cpp
        src/buspirate_i2c.cpp
        src/buspirate_onewire.cpp
        src/buspirate_rawwire.cpp
        src/buspirate_uart.cpp
        src/buspirate_mode.cpp
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${PROJECT_SOURCE_DIR}/inc
        ${PROJECT_SOURCE_DIR}/inc/private
)

target_link_libraries( ${PROJECT_NAME}
    PUBLIC
        SharedSettings
        PluginInterfaces
        uPluginOps
        ScriptInterfaces
        CommScriptClient
        CommScriptCommandInterpreter
        ScriptReader
        uUart
        uUtils
)
