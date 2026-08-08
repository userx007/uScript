cmake_minimum_required(VERSION 3.25)
project(profibus_plugin)

add_library(${PROJECT_NAME}
    SHARED
        src/profibus_plugin.cpp
        src/profibus_driver.cpp
        src/profibus_protocol.cpp
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
        uUart
)
