cmake_minimum_required(VERSION 3.25)
project(uEnc28J60Net)

# Define sources based on platform
set(SOURCES
    src/uEnc28J60NetCommon.cpp
)

# Check if we are building for a POSIX system (Linux, macOS, BSD)
if(UNIX)
    # On POSIX systems, we include the real socket implementation
    list(APPEND SOURCES src/uEnc28J60NetPosix.cpp)
    set(HAS_POSIX_SOCKETS TRUE)
else()
    # On non-POSIX (e.g., Windows or bare-metal), you might have a different backend.
    # For now, we just build the common part.
    message(STATUS "uEnc28J60Net: Building common logic only (No POSIX sockets).")
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

# Optional: Add install rules
install(TARGETS ${PROJECT_NAME}
    EXPORT ${PROJECT_NAME}Targets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(DIRECTORY inc/
    DESTINATION include
    FILES_MATCHING PATTERN "*.hpp"
)
