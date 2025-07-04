
cmake_minimum_required(VERSION 3.3)
project(ScriptValidator)


add_library(${PROJECT_NAME}
    OBJECT
        src/ScriptValidator.cpp
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${PROJECT_SOURCE_DIR}/inc
)

target_link_libraries(${PROJECT_NAME}
    PUBLIC
        ScriptInterfaces
        PluginInterfaces
        ItemValidator
        uUtils
)
