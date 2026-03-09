cmake_minimum_required(VERSION 3.16)
project(ushell_core)

if(NOT MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC" CACHE STRING "Set C Compiler Flags" FORCE)
endif()

add_library(${PROJECT_NAME} STATIC
    src/ushell_core.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    ushell_core_utils
    ushell_core_config
)
