cmake_minimum_required(VERSION 3.25)
project(uJ1939Tp)

set(SOURCES 
    src/J1939TpProtocol.cpp
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
