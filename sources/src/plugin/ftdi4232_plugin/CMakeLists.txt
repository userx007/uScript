cmake_minimum_required(VERSION 3.16)

set(TARGET_NAME ftdi4232_plugin)

add_library(${TARGET_NAME} SHARED
    src/ft4232_plugin.cpp
    src/ft4232_spi.cpp
    src/ft4232_i2c.cpp
    src/ft4232_gpio.cpp
)

target_include_directories(${TARGET_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/inc
        ${CMAKE_CURRENT_SOURCE_DIR}/inc/private
)

target_link_libraries(${TARGET_NAME}
    PRIVATE
        ft4232
        uPluginOps
        uIPlugin
        uSharedConfig
)

target_compile_options(${TARGET_NAME}
    PRIVATE
        ${FTDI_WARN_FLAGS}
)