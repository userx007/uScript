cmake_minimum_required(VERSION 3.10)
project(UartTools)

add_executable(uart_responder  src/test_uart_responder.cpp  src/test_uart_common.cpp)
add_executable(uart_sender     src/test_uart_sender.cpp     src/test_uart_common.cpp)


target_link_libraries(uart_responder
    PRIVATE
        uCommDriver
        uUart
        uUtils
)

target_include_directories(uart_responder
    PUBLIC
        ${PROJECT_SOURCE_DIR}/inc
)

target_link_libraries(uart_sender
    PRIVATE
        uCommDriver
        uUart
        uUtils
)

target_include_directories(uart_sender
    PUBLIC
        ${PROJECT_SOURCE_DIR}/inc
)