cmake_minimum_required(VERSION 3.16)
project(core_plugin)

add_library(${PROJECT_NAME} SHARED
    src/core_plugin.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    uSharedConfig
    uIPlugin
    uPluginOps
    uUtils
)

if(NOT MSVC)
    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} PUBLIC Threads::Threads)
endif()
