cmake_minimum_required(VERSION 3.16)
project(uPcanDriver LANGUAGES CXX)

# ---------------------------------------------------------------------------
# This wrapper uses std::span, std::string_view, and std::from_chars
# (<charconv>) -> requires C++20.
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Locate the PCAN-Basic SDK
#
# Linux:   PEAK's source install (peak-linux-driver + libpcanbasic) normally
#          drops the header in /usr/include/PCAN-Basic/PCANBasic.h and the
#          shared lib in the standard library search path (/usr/lib or
#          /usr/lib/x86_64-linux-gnu). Override with -DPCAN_ROOT=<path> if
#          you installed it somewhere else.
#
# Windows: The PCAN-Basic SDK zip from peak-system.com unpacks to a folder
#          containing Include/PCANBasic.h and Win32/x64 subfolders with the
#          .lib/.dll. Point -DPCAN_ROOT=<path> at that unpacked folder, or
#          set the PCAN_ROOT environment variable.
# ---------------------------------------------------------------------------

set(PCAN_ROOT "" CACHE PATH "Root folder of the PCAN-Basic SDK installation")

if(WIN32)
    find_path(PCAN_INCLUDE_DIR
        NAMES PCANBasic.h
        PATHS
            "${PCAN_ROOT}/Include"
            "$ENV{PCAN_ROOT}/Include"
            "C:/PCAN-Basic/Include"
            "C:/Program Files/PEAK-System/PCAN-Basic/Include"
            "C:/Program Files (x86)/PEAK-System/PCAN-Basic/Include"
    )

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_PCAN_ARCH_DIR "x64")
    else()
        set(_PCAN_ARCH_DIR "Win32")
    endif()

    find_library(PCAN_LIBRARY
        NAMES PCANBasic
        PATHS
            "${PCAN_ROOT}/${_PCAN_ARCH_DIR}/VC_LIB"
            "${PCAN_ROOT}/Lib/${_PCAN_ARCH_DIR}"
            "$ENV{PCAN_ROOT}/${_PCAN_ARCH_DIR}/VC_LIB"
            "C:/PCAN-Basic/Lib/${_PCAN_ARCH_DIR}"
    )

    # PCANBasic.dll must be next to the executable (or on PATH) at runtime.
    # We try to locate it so it can be copied post-build.
    find_file(PCAN_DLL
        NAMES PCANBasic.dll
        PATHS
            "${PCAN_ROOT}/${_PCAN_ARCH_DIR}"
            "$ENV{PCAN_ROOT}/${_PCAN_ARCH_DIR}"
            "C:/Windows/System32"
    )
else()
    find_path(PCAN_INCLUDE_DIR
        NAMES PCANBasic.h
        PATHS
            "${PCAN_ROOT}/include"
            "$ENV{PCAN_ROOT}/include"
            /usr/include/PCAN-Basic
            /usr/local/include/PCAN-Basic
            /usr/include
    )

    find_library(PCAN_LIBRARY
        NAMES pcanbasic libpcanbasic
        PATHS
            "${PCAN_ROOT}/lib"
            "$ENV{PCAN_ROOT}/lib"
            /usr/lib
            /usr/local/lib
            /usr/lib/x86_64-linux-gnu
    )
endif()

if(NOT PCAN_INCLUDE_DIR)
    message(FATAL_ERROR
        "PCANBasic.h not found. Install the PCAN-Basic SDK from peak-system.com, "
        "or pass -DPCAN_ROOT=<path-to-sdk> to CMake.")
endif()

if(NOT PCAN_LIBRARY)
    message(FATAL_ERROR
        "PCAN-Basic library not found. Install the PCAN-Basic SDK from peak-system.com, "
        "or pass -DPCAN_ROOT=<path-to-sdk> to CMake.")
endif()

message(STATUS "PCAN-Basic include dir: ${PCAN_INCLUDE_DIR}")
message(STATUS "PCAN-Basic library:     ${PCAN_LIBRARY}")

# ---------------------------------------------------------------------------
# Imported target wrapping the SDK so consumers just link against it
# ---------------------------------------------------------------------------
add_library(PCANBasic::PCANBasic UNKNOWN IMPORTED)
set_target_properties(PCANBasic::PCANBasic PROPERTIES
    IMPORTED_LOCATION "${PCAN_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${PCAN_INCLUDE_DIR}"
)

# ---------------------------------------------------------------------------
# The driver wrapper itself
#
# Adjust UCOMM_INCLUDE_DIR to wherever ICommDriver.hpp / uLogger.hpp live in
# your tree (they are not part of this repo/snippet).
# ---------------------------------------------------------------------------
set(UCOMM_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include" CACHE PATH
    "Directory containing ICommDriver.hpp and uLogger.hpp")

add_library(uPcanDriver STATIC
    uPcan.cpp
)

target_include_directories(uPcanDriver
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${UCOMM_INCLUDE_DIR}
)

target_link_libraries(uPcanDriver
    PUBLIC
        PCANBasic::PCANBasic
)

if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(uPcanDriver PUBLIC Threads::Threads)
endif()

# ---------------------------------------------------------------------------
# Example: copy PCANBasic.dll next to a consuming executable on Windows.
# (Add this snippet in the target that links uPcanDriver, e.g.:)
#
# if(WIN32 AND PCAN_DLL)
#     add_custom_command(TARGET your_app POST_BUILD
#         COMMAND ${CMAKE_COMMAND} -E copy_if_different
#                 "${PCAN_DLL}" "$<TARGET_FILE_DIR:your_app>"
#     )
# endif()
# ---------------------------------------------------------------------------
