cmake_minimum_required(VERSION 3.25)
project(modbus_plugin)

add_library(${PROJECT_NAME}
    SHARED
        src/modbus_plugin.cpp
        src/modbus_driver.cpp
        src/modbus_protocol.cpp
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
        uTcpip
)
