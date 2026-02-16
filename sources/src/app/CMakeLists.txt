
cmake_minimum_required(VERSION 3.3)
project(uscript)


add_executable( ${PROJECT_NAME}
        src/ScriptMainApp.cpp
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${PROJECT_SOURCE_DIR}/inc
)

target_link_libraries(${PROJECT_NAME}
    PUBLIC
        ScriptClient
        ScriptInterpreter
        uScriptReader
        ScriptRunner
        ScriptValidator
        uUtils
)
