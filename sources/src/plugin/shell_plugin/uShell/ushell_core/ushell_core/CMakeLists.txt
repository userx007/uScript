cmake_minimum_required(VERSION 3.5)

project(ushell_core)

if(NOT MSVC)
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC" CACHE STRING "Set C Compiler Flags" FORCE)
endif()


add_library(${PROJECT_NAME}
    OBJECT
        src/ushell_core.cpp
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${PROJECT_SOURCE_DIR}/inc

)

target_link_libraries(${PROJECT_NAME}
    ushell_core_utils
    ushell_core_config
)
