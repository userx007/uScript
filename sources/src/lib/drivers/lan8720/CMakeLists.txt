cmake_minimum_required(VERSION 3.25)
project(uLan8720Net)

set(SOURCES
    src/uLan8720NetCommon.cpp
)

if(WIN32)
    # Winsock2 backend — see uLan8720NetWindows.cpp's header comment.
    list(APPEND SOURCES src/uLan8720NetWindows.cpp)
elseif(UNIX)
    list(APPEND SOURCES src/uLan8720NetPosix.cpp)
    set(HAS_POSIX_SOCKETS TRUE)
else()
    message(FATAL_ERROR "uLan8720Net only ships POSIX and Winsock socket backends "
                         "(Linux/macOS/Windows).")
endif()

add_library(${PROJECT_NAME} STATIC ${SOURCES})

set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

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
