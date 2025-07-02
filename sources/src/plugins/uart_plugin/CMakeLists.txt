
cmake_minimum_required(VERSION 3.3)
project(uart_plugin)

add_library(${PROJECT_NAME}
    SHARED
        src/uart_plugin.cpp
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${PROJECT_SOURCE_DIR}/inc
)

target_link_libraries( ${PROJECT_NAME}
    PUBLIC
        plugin_interface
        plugin_extension
        args_interface
        string_handling
        time_handling
        file_handling
        uart_driver
        uart_handling
        upload_handling
        script_parser
        crc32
        dlt
)
