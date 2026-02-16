
cmake_minimum_required(VERSION 3.3)
project(uScriptInterpreter)


add_library(${PROJECT_NAME}
    OBJECT
        src/uScriptInterpreter.cpp
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${PROJECT_SOURCE_DIR}/inc
)

target_link_libraries(${PROJECT_NAME}
    PUBLIC
        uSharedConfig
        uIShellScript
        uIPlugin
        uScriptDataTypes
        uScriptCommandValidator
        uPluginOps
        uUtils
)