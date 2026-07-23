cmake_minimum_required(VERSION 3.25)
project(uPluginSettings)

# Header-only: no sources to compile, just an include path plus the
# dependencies its templates rely on (uNumeric, uBoolEvaluator) to publish
# transitively, so any plugin linking this target gets them for free.
add_library(${PROJECT_NAME} INTERFACE)

target_include_directories(${PROJECT_NAME} INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME} INTERFACE
    uUtils
)
