cmake_minimum_required(VERSION 3.25)
project(uLan8720Net)

set(SOURCES
    src/uLan8720NetCommon.cpp
)

if(UNIX)
    list(APPEND SOURCES src/uLan8720NetPosix.cpp)
    set(HAS_POSIX_SOCKETS TRUE)
else()
    message(STATUS "uLan8720Net: Building common logic only (No POSIX sockets).")
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

install(TARGETS ${PROJECT_NAME}
    EXPORT ${PROJECT_NAME}Targets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(DIRECTORY inc/
    DESTINATION include
    FILES_MATCHING PATTERN "*.hpp"
)
