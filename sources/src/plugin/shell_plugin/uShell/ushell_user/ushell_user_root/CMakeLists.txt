
cmake_minimum_required(VERSION 3.5)

project(ushell_user_root)


add_library(${PROJECT_NAME}
    OBJECT
        src/ushell_root_interface.cpp
        src/ushell_root_usercode.cpp
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${PROJECT_SOURCE_DIR}/inc
)

target_link_libraries(${PROJECT_NAME}
    ushell_core
    ushell_core_config
    ushell_core_utils
    ushell_core_terminal
    ushell_user_logger
    ScriptInterfaces
    ScriptDataTypes
    SharedSettings
    uUtils
)

