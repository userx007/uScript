cmake_minimum_required(VERSION 3.16)
project(cp2112 VERSION 1.0.0 LANGUAGES CXX)

# ─────────────────────────────────────────────────────────────────────────────
# C++ standard
# Explicit global setting as a safety net for toolchains (e.g. older MinGW-w64
# CMake integrations) that do not propagate target_compile_features reliably.
# ─────────────────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)   # -std=c++20, not -std=gnu++20

# ─────────────────────────────────────────────────────────────────────────────
# Library target
# ─────────────────────────────────────────────────────────────────────────────
add_library(${PROJECT_NAME} 
    STATIC
        src/uCP2112Common.cpp
        $<IF:$<BOOL:${WIN32}>,src/uCP2112Windows.cpp,src/uCP2112Linux.cpp>
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
        $<INSTALL_INTERFACE:include>
)

# ─────────────────────────────────────────────────────────────────────────────
# Platform-specific link dependencies
# ─────────────────────────────────────────────────────────────────────────────
if(WIN32)

    # Both MSVC and MinGW-w64 ship import libraries for these.
    #
    #   hid       → hid.lib   / libhid.a    — HidD_* API (Windows DDK/WDK)
    #   setupapi  → setupapi.lib / libsetupapi.a — SetupDi* enumeration API
    #
    # CMake's linker resolves the correct file name per toolchain automatically.
    target_link_libraries(${PROJECT_NAME} 
        PUBLIC 
            hid 
            setupapi
    )

    # MinGW-w64 specific: suppress -municode if not already set project-wide.
    # Only applied when the compiler is GCC to leave MSVC builds untouched.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${PROJECT_NAME} 
            PRIVATE 
                -municode
        )
    endif()

else()

    # Linux: hidraw is a kernel module, accessed via /dev/hidraw* ioctls.
    # No userspace HID library is required.
    #
    # Threads is needed for std::this_thread::sleep_for in uCP2112Common.cpp.
    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} 
        PUBLIC 
            Threads::Threads
    )

endif()

# ─────────────────────────────────────────────────────────────────────────────
# Compiler warnings
# ─────────────────────────────────────────────────────────────────────────────
if(MSVC)

    target_compile_options(${PROJECT_NAME} 
        PRIVATE 
            /W4 /permissive
        )

else()

    # Applies to GCC (MinGW-w64 included) and Clang
    target_compile_options(${PROJECT_NAME} 
        PRIVATE 
        -Wall -Wextra -Wpedantic
    )

endif()

target_link_libraries(${PROJECT_NAME}
    PUBLIC
        uICommDriver
        uUtils
)
