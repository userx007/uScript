cmake_minimum_required(VERSION 3.25)
project(uProfibusDriver)

add_library(${PROJECT_NAME} STATIC
    src/profibus_driver.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    uICommDriver
    uUtils
    uUart
    uProfibusProtocol
)
