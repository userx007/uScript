cmake_minimum_required(VERSION 3.25)
project(w5500net_plugin)

add_library(${PROJECT_NAME}
    SHARED
        src/w5500net_plugin.cpp
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
        uW5500Net
)

target_compile_definitions(${PROJECT_NAME} PUBLIC
    HAS_W5500NET
)
