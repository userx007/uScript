cmake_minimum_required(VERSION 3.25)
project(uKvParser C)

set(SOURCES
    src/kvparser.c
)

add_library(${PROJECT_NAME}
    STATIC
        ${SOURCES}
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
)