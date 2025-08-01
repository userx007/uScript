
cmake_minimum_required(VERSION 3.3)
project(uart_port_handling)

add_library( ${PROJECT_NAME}
    OBJECT
        src/uart_port_handling.cpp
)

target_include_directories( ${PROJECT_NAME}
    PUBLIC
        ${PROJECT_SOURCE_DIR}/inc
)

target_link_libraries( ${PROJECT_NAME}
    PUBLIC
        string_handling
        device_handling
        time_handling
        dlt
)
