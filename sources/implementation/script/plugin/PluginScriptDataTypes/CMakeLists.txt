cmake_minimum_required(VERSION 3.3)

project(PluginScriptDataTypes)

add_library( ${PROJECT_NAME}
    INTERFACE
)

target_include_directories(${PROJECT_NAME}
    INTERFACE
        ${PROJECT_SOURCE_DIR}/inc
)

