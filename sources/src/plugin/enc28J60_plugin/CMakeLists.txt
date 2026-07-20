cmake_minimum_required(VERSION 3.25)
project(enc28j60net_plugin)

add_library(${PROJECT_NAME}
    SHARED
        src/enc28j60net_plugin.cpp
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/inc
        ${CMAKE_CURRENT_SOURCE_DIR}/inc/private
)

target_link_libraries( ${PROJECT_NAME}
    PUBLIC
        uCommandExec
        uIPlugin
        uICoreScript
        uCommScriptClient
        uCommScriptCommandInterpreter
        uScriptReader
        uPluginOps
        uEnc28J60Net
)

target_compile_definitions(${PROJECT_NAME} PUBLIC
    HAS_ENC28J60NET
)
