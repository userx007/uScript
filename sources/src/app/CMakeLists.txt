cmake_minimum_required(VERSION 3.16)
project(uscript)

add_executable(${PROJECT_NAME}
    src/uScriptMainApp.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    uScriptClient
    uScriptInterpreter
    uScriptReader
    uScriptRunner
    uScriptValidator
    uUtils
)
