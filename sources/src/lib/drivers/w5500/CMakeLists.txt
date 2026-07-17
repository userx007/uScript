cmake_minimum_required(VERSION 3.25)
project(uW5500Net)

# Define sources based on platform
set(SOURCES
    src/uW5500NetCommon.cpp
)

# Check if we are building for a POSIX system (Linux, macOS, BSD)
if(UNIX)
    list(APPEND SOURCES src/uW5500NetPosix.cpp)
    set(HAS_POSIX_SOCKETS TRUE)
else()
    message(STATUS "uW5500Net: Building common logic only (No POSIX sockets).")
    # You can add src/uW5500NetStub.cpp or src/uW5500NetWindows.cpp here
endif()

# Create the static library
add_library(${PROJECT_NAME} STATIC ${SOURCES})

# Set C++ Standard
set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Include directories
target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

# Link dependencies
target_link_libraries(${PROJECT_NAME} PUBLIC
    uICommDriver
    uUtils
)

# Install rules
install(TARGETS ${PROJECT_NAME}
    EXPORT ${PROJECT_NAME}Targets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(DIRECTORY inc/
    DESTINATION include
    FILES_MATCHING PATTERN "*.hpp"
)
