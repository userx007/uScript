cmake_minimum_required(VERSION 3.20)

project(uPcan
    VERSION     1.0.0
    DESCRIPTION "PCAN-Basic driver wrapper implementing ICommDriver"
    LANGUAGES   CXX
)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(UPCAN_BUILD_SHARED  "Build uPcan as a shared library instead of static" OFF)


# Only x64 PCAN-Basic binaries are vendored under third_party/, so fail fast
# on anything else instead of producing a confusing link error later.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "uPcan: only 64-bit (x64) builds are supported - "
                         "the vendored PCAN-Basic binaries are x64 only.")
endif()

# ---------------------------------------------------------------------------
# External interface headers (ICommDriver.hpp, uLogger.hpp)
# ---------------------------------------------------------------------------
# uPcan.cpp/.hpp depend on two headers that are NOT part of this repository:
#   - ICommDriver.hpp  (the abstract comm-driver interface uPcan implements)
#   - uLogger.hpp      (the logging macros: LOG_PRINT, LOG_STRING, ...)
#
# These normally live in a shared "core" component of the parent project.
# Two ways to satisfy them, handled automatically below:
#
#   1) If this is add_subdirectory()'d from a parent project that already
#      defines CMake targets named `ICommDriver` and/or `uLogger`, uPcan
#      will simply link against those targets.
#
#   2) Otherwise, point CMake at the directories containing the headers:
#      cmake -DUPCAN_ICOMMDRIVER_INCLUDE_DIR=/path/to/dir \
#            -DUPCAN_ULOGGER_INCLUDE_DIR=/path/to/dir ...
#

# ---------------------------------------------------------------------------
# Third-party: PCAN-Basic SDK (vendored, prebuilt)
# ---------------------------------------------------------------------------
set(PCANBASIC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/PCAN_Basic")
set(PCANBASIC_INCLUDE_DIR "${PCANBASIC_ROOT}/include")

if(NOT EXISTS "${PCANBASIC_INCLUDE_DIR}/PCANBasic.h")
    message(FATAL_ERROR "uPcan: PCANBasic.h not found at ${PCANBASIC_INCLUDE_DIR}. "
                         "Check that third_party/PCAN_Basic was extracted correctly.")
endif()

add_library(PCANBasic::PCANBasic SHARED IMPORTED)
target_include_directories(PCANBasic::PCANBasic INTERFACE "${PCANBASIC_INCLUDE_DIR}")

if(WIN32)
    set(PCANBASIC_LIB_DIR "${PCANBASIC_ROOT}/lib/windows/x64")
    set(PCANBASIC_IMPLIB  "${PCANBASIC_LIB_DIR}/PCANBasic.lib")
    set(PCANBASIC_DLL     "${PCANBASIC_LIB_DIR}/PCANBasic.dll")

    if(NOT EXISTS "${PCANBASIC_IMPLIB}" OR NOT EXISTS "${PCANBASIC_DLL}")
        message(FATAL_ERROR "uPcan: PCANBasic.lib/.dll not found under ${PCANBASIC_LIB_DIR}")
    endif()

    set_target_properties(PCANBasic::PCANBasic PROPERTIES
        IMPORTED_IMPLIB   "${PCANBASIC_IMPLIB}"
        IMPORTED_LOCATION "${PCANBASIC_DLL}"
    )
elseif(UNIX)
    set(PCANBASIC_LIB_DIR "${PCANBASIC_ROOT}/lib/linux/x64")
    # Link against the unversioned dev symlink; the loader resolves the
    # real SONAME (libpcanbasic.so.5) at runtime.
    set(PCANBASIC_SO "${PCANBASIC_LIB_DIR}/libpcanbasic.so")

    if(NOT EXISTS "${PCANBASIC_SO}")
        message(FATAL_ERROR "uPcan: libpcanbasic.so not found under ${PCANBASIC_LIB_DIR}")
    endif()

    set_target_properties(PCANBasic::PCANBasic PROPERTIES
        IMPORTED_LOCATION "${PCANBASIC_SO}"
    )
else()
    message(FATAL_ERROR "uPcan: unsupported platform (only Windows and Linux are provided).")
endif()

# ---------------------------------------------------------------------------
# uPcan library target
# ---------------------------------------------------------------------------
if(UPCAN_BUILD_SHARED)
    add_library(uPcan SHARED ${CMAKE_CURRENT_SOURCE_DIR}/src/uPcan.cpp)
else()
    add_library(uPcan STATIC ${CMAKE_CURRENT_SOURCE_DIR}/src/uPcan.cpp)
endif()
add_library(uPcan::uPcan ALIAS uPcan)

set_target_properties(uPcan PROPERTIES
    CXX_STANDARD          20      # required for std::span / std::string_view
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS        OFF
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(uPcan
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(uPcan 
    PUBLIC 
        PCANBasic::PCANBasic
        uICommDriver
        uUtils        
)

# --- Threads (std::mutex on Linux needs pthread) ----------------------------
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(uPcan PUBLIC Threads::Threads)

if(WIN32)
    target_compile_definitions(uPcan PRIVATE _WIN32_WINNT=0x0601)
endif()

# On Windows, a shared uPcan (or any executable linking it) needs
# PCANBasic.dll alongside the binary. Copy it next to the uPcan target
# output directory for convenience during local development/testing.
if(WIN32)
    add_custom_command(TARGET uPcan POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PCANBASIC_DLL}" "$<TARGET_FILE_DIR:uPcan>"
        COMMENT "uPcan: copying PCANBasic.dll next to build output"
    )
endif()

