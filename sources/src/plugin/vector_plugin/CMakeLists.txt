cmake_minimum_required(VERSION 3.25)
project(vector_plugin)

if(NOT (WIN32 OR LINUX))
    message(FATAL_ERROR "vector_plugin: depends on uVector, which ships for Windows and Linux "
                         "only (Vector's XL Driver Library / the unofficial Linux port).")
endif()

add_library(${PROJECT_NAME}
    SHARED
        src/vector_plugin.cpp
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
        uVector
)
