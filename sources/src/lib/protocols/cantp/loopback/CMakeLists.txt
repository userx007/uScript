cmake_minimum_required(VERSION 3.25)
project(cantp_loopback)

find_package(Threads REQUIRED)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/LoopbackCommDriver.cpp
    src/SdoLoopbackServer.cpp
    src/RealCommDriver.cpp
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        uTpFactory
        Threads::Threads
        uUtils
        uICommDriver
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
