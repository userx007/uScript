cmake_minimum_required(VERSION 3.25)
project(mqtt_plugin)

find_package(OpenSSL REQUIRED)

add_library(${PROJECT_NAME}
    SHARED
        src/mqtt_plugin.cpp
        src/mqtt_driver.cpp
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/inc
        ${CMAKE_CURRENT_SOURCE_DIR}/inc/private
)

target_link_libraries( ${PROJECT_NAME}
    PUBLIC
        uCommandExec
        uIPlugin
        uICommDriver
        uPluginOps
        uCommScriptClient
        uCommScriptCommandInterpreter
        uScriptReader
        uTcpip
    PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
)
