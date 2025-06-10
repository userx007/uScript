
cmake_minimum_required(VERSION 3.5)

project(math_iplugin)

add_library(${PROJECT_NAME} SHARED
    src/ushell_plugin_interface.cpp
    src/ushell_plugin_usercode.cpp
)

set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "lib")

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${PROJECT_SOURCE_DIR}/inc
)

target_link_libraries(${PROJECT_NAME}
    ushell_core_utils
    ushell_core_terminal
    ushell_core_config
    ushell_user_logger
)
