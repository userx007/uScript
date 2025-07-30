
cmake_minimum_required(VERSION 3.3)
project(utils_plugin)

add_library(${PROJECT_NAME}
    SHARED
        src/utils_plugin.cpp
)

target_include_directories( ${PROJECT_NAME}
    PUBLIC
        ${PROJECT_SOURCE_DIR}/inc
)

target_link_libraries( ${PROJECT_NAME}
    PUBLIC
        plugin_interface
        args_interface
        string_handling
        time_handling
        validation_handling
        regex_handling
        device_handling
        uart_port_handling
        dlt
)

if(NOT MSVC)
target_link_libraries( ${PROJECT_NAME}
    PUBLIC
        pthread
)
endif()
