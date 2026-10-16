cmake_minimum_required(VERSION 3.25)
project(cantp_loopback)

find_package(Threads REQUIRED)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Real CAN-TP loopback over a SocketCAN interface (RealCommDriver uses linux/can.h).
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

    target_compile_features(${PROJECT_NAME} 
        PRIVATE 
            cxx_std_20
    )
else()
    # SocketCAN does not exist outside Linux, so the "real" variant above is not built. The
    # in-memory two-thread simulation (main_sim.cpp + LoopbackCommDriver) is fully portable
    # and exercises the same CAN-TP stack.
    add_executable(${PROJECT_NAME}_sim
        src/main_sim.cpp
        src/LoopbackCommDriver.cpp
        src/SdoLoopbackServer.cpp
    )

    target_include_directories(${PROJECT_NAME}_sim
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/inc
    )

    target_link_libraries(${PROJECT_NAME}_sim
        PRIVATE
            uTpFactory
            Threads::Threads
            uUtils
            uICommDriver
    )

    target_compile_features(${PROJECT_NAME}_sim
        PRIVATE
            cxx_std_20
    )
endif()
