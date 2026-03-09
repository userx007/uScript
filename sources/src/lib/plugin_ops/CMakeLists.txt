cmake_minimum_required(VERSION 3.16)

set(TARGET_NAME uPluginOps)

add_library(${TARGET_NAME}
    INTERFACE
)

target_include_directories(${TARGET_NAME}
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(${TARGET_NAME}
    INTERFACE
        uUtils
)
