
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
        CommonSettings
        PluginInterfaces
        uPluginOps
        uUtils
)

if(NOT MSVC)
target_link_libraries( ${PROJECT_NAME}
    PUBLIC
        pthread
)
endif()
