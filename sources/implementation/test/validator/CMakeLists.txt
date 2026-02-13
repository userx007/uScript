cmake_minimum_required(VERSION 3.10)
project(validator)

add_executable(${PROJECT_NAME}
    src/test_validator_main.cpp
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        inc
)

target_link_libraries( ${PROJECT_NAME}
    PRIVATE
        ScriptInterfaces
        CommScriptDataTypes
        CommScriptCommandValidator
        SharedSettings
        uUtils
)
