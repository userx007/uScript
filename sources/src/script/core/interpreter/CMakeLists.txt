cmake_minimum_required(VERSION 3.16)
project(uScriptInterpreter)

add_library(${PROJECT_NAME} STATIC
    src/uScriptInterpreter.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    uSharedConfig
    uIShellScript
    uIPlugin
    uScriptDataTypes
    uScriptCommandValidator
    uPluginOps
    uUtils
)
