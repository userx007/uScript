cmake_minimum_required(VERSION 3.25)
project(uMqttDriver)

find_package(OpenSSL REQUIRED)

add_library(${PROJECT_NAME} STATIC
    src/mqtt_driver.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    uICommDriver
    uUtils
    uTcpip
    uMqttProtocol
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
)
