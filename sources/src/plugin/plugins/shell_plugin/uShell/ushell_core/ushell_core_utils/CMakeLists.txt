cmake_minimum_required(VERSION 3.5)

project(ushell_core_utils)

add_library(${PROJECT_NAME}
    OBJECT
        src/ushell_core_utils.cpp
)

set_target_properties(${PROJECT_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${PROJECT_SOURCE_DIR}/inc
)

target_link_libraries(${PROJECT_NAME}
    ushell_core_terminal
    ushell_core_config
    ushell_settings
)
