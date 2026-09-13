cmake_minimum_required(VERSION 3.25)
project(uNmea2000Fp)

set(SOURCES 
    src/Nmea2000FastPacketProtocol.cpp
)

add_library(${PROJECT_NAME} 
    STATIC 
        ${SOURCES}
)

target_include_directories(${PROJECT_NAME} 
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME} 
    PUBLIC
        uICommDriver
        uITpInterface
        uTpConfig
)
