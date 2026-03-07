cmake_minimum_required(VERSION 3.16)
project(ft4232_i2c VERSION 1.0.0 LANGUAGES CXX)

# ─────────────────────────────────────────────────────────────────────────────
# C++ standard
# ─────────────────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)   # -std=c++20, not -std=gnu++20

# ─────────────────────────────────────────────────────────────────────────────
# Library target
#
# Source layout:
#   uFT4232I2CCommon.cpp   — FT4232I2C protocol + ICommDriver interface
#   uFT4232Linux.cpp   OR  — FT4232Base platform impl (libftdi1)
#   uFT4232Windows.cpp     — FT4232Base platform impl (FTD2XX)
# ─────────────────────────────────────────────────────────────────────────────
add_library(ft4232_i2c STATIC
    src/uFT4232I2CCommon.cpp
    $<IF:$<BOOL:${WIN32}>,src/uFT4232Windows.cpp,src/uFT4232Linux.cpp>
)

target_include_directories(ft4232_i2c
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
        $<INSTALL_INTERFACE:include>
)

# ─────────────────────────────────────────────────────────────────────────────
# Platform-specific dependencies
# ─────────────────────────────────────────────────────────────────────────────
if(WIN32)
    # ── FTD2XX (FTDI official D2XX driver) ───────────────────────────────────
    #
    # Download the D2XX SDK from https://ftdichip.com/drivers/d2xx-drivers/
    # and unpack it.  Then point FTD2XX_ROOT to the unpacked folder:
    #
    #   cmake -DFTD2XX_ROOT="C:/FTD2XX" ..
    #
    # The SDK contains:
    #   ftd2xx.h         → include header
    #   <arch>/ftd2xx.lib → import library  (i386 or amd64)
    #
    if(NOT DEFINED FTD2XX_ROOT)
        message(FATAL_ERROR
            "FTD2XX_ROOT is not set.\n"
            "Download the D2XX SDK from https://ftdichip.com/drivers/d2xx-drivers/\n"
            "then pass -DFTD2XX_ROOT=<path> to cmake.")
    endif()

    # Detect target architecture to pick the right lib subfolder
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(FTD2XX_ARCH_DIR "${FTD2XX_ROOT}/amd64")
    else()
        set(FTD2XX_ARCH_DIR "${FTD2XX_ROOT}/i386")
    endif()

    target_include_directories(ft4232_i2c PRIVATE "${FTD2XX_ROOT}")
    target_link_libraries(ft4232_i2c PUBLIC "${FTD2XX_ARCH_DIR}/ftd2xx.lib")

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # MinGW-w64: suppress unicode entry-point warning
        target_compile_options(ft4232_i2c PRIVATE -municode)
    endif()

else()
    # ── libftdi1 (Linux / macOS) ─────────────────────────────────────────────
    #
    # Install:  sudo apt install libftdi1-dev        (Debian/Ubuntu)
    #           sudo dnf install libftdi-devel       (Fedora)
    #           brew install libftdi                 (macOS/Homebrew)
    #
    # The find module looks for libftdi1 via pkg-config first, then falls back
    # to a manual search.  Both paths populate LIBFTDI_INCLUDE_DIRS and
    # LIBFTDI_LIBRARIES.

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBFTDI REQUIRED libftdi1)

    target_include_directories(ft4232_i2c PRIVATE ${LIBFTDI_INCLUDE_DIRS})
    target_link_libraries(ft4232_i2c PUBLIC ${LIBFTDI_LIBRARIES})

    # pthreads — required for std::this_thread::sleep_for in the common layer
    find_package(Threads REQUIRED)
    target_link_libraries(ft4232_i2c PUBLIC Threads::Threads)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Compiler warnings
# ─────────────────────────────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(ft4232_i2c PRIVATE /W4 /permissive-)
else()
    target_compile_options(ft4232_i2c PRIVATE -Wall -Wextra -Wpedantic)
endif()
