cmake_minimum_required(VERSION 3.16)
project(hydrabus_plugin CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(${PROJECT_NAME} SHARED
    src/hydrabus_plugin.cpp
    src/hydrabus_spi.cpp
    src/hydrabus_i2c.cpp
    src/hydrabus_uart.cpp
    src/hydrabus_onewire.cpp
    src/hydrabus_rawwire.cpp
    src/hydrabus_swd.cpp
    src/hydrabus_smartcard.cpp
    src/hydrabus_nfc.cpp
    src/hydrabus_mmc.cpp
    src/hydrabus_sdio.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/inc/private
)

# HydraHAL is built in the same tree — its PUBLIC includes propagate automatically
target_link_libraries(${PROJECT_NAME}
    PUBLIC
        uSharedConfig
        uIPlugin
        uPluginOps
        uICoreScript
        uCommScriptClient
        uCommScriptCommandInterpreter
        uScriptReader
        uUart
        uUtils
    PRIVATE
        HydraHAL
)
