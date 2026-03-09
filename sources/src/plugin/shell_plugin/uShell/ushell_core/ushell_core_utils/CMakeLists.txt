cmake_minimum_required(VERSION 3.16)
project(ushell_core_utils)

add_library(${PROJECT_NAME} STATIC
    src/ushell_core_utils.cpp
)

set_target_properties(${PROJECT_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    ushell_core_terminal
    ushell_core_config
    ushell_settings
)
