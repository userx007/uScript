
cmake_minimum_required(VERSION 3.25)
project(shell_plugin)

add_library(${PROJECT_NAME}
    SHARED
        src/shell_plugin.cpp
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

target_link_libraries( ${PROJECT_NAME}
    PUBLIC
        uPluginSettings
        uSharedConfig
        uIPlugin
        uPluginOps
        uUtils
        ushell_core
        ushell_core_terminal
        ushell_core_utils
        ushell_user_root
)
