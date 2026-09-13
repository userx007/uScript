cmake_minimum_required(VERSION 3.25)
project(uUdp)

set(SOURCES src/uUdpCommon.cpp)

if(WIN32)
    # Winsock2 backend — see uUdpWindows.cpp's header comment.
    list(APPEND SOURCES src/uUdpWindows.cpp)
elseif(UNIX)
    # Plain BSD sockets (socket/connect/poll/getaddrinfo) — builds on Linux
    # and macOS alike, same as uEth.
    list(APPEND SOURCES src/uUdpPosix.cpp)
else()
    message(FATAL_ERROR "uUdp only ships POSIX and Winsock socket backends "
                         "(Linux/macOS/Windows).")
endif()

add_library(${PROJECT_NAME} STATIC ${SOURCES})

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    uICommDriver
    uUtils
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PUBLIC ws2_32)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    uKmpMatch
)
