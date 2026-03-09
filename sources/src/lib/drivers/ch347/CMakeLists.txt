cmake_minimum_required(VERSION 3.16)
project(uCH347)

add_library(${PROJECT_NAME} STATIC
    src/uCH347Drivers.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

add_library(ch347 STATIC IMPORTED)
set_target_properties(ch347 PROPERTIES
    IMPORTED_LOCATION
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ch347/x64/static/libch347.a
    INTERFACE_INCLUDE_DIRECTORIES
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ch347/x64/static
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    ch347
    uICommDriver
    uUtils
)
