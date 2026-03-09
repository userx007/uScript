cmake_minimum_required(VERSION 3.16)
project(ushell_user_root)

add_library(${PROJECT_NAME} STATIC
    src/ushell_root_interface.cpp
    src/ushell_root_usercode.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    ushell_core
    ushell_core_config
    ushell_core_utils
    ushell_core_terminal
    ushell_user_logger
    uIShellScript
    uScriptDataTypes
    uSharedConfig
    uUtils
)
