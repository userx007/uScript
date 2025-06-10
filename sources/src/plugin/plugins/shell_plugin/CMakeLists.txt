
cmake_minimum_required(VERSION 3.3)
project(shell_plugin)

add_library(${PROJECT_NAME}
    SHARED
        src/shell_plugin.cpp
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${PROJECT_SOURCE_DIR}/inc
)

target_link_libraries( ${PROJECT_NAME}
    PUBLIC
        PluginInterfaces
        PluginUtils
        Utils
)
