cmake_minimum_required(VERSION 3.16)
project(ft4232 VERSION 1.1.0 LANGUAGES CXX)

# ─────────────────────────────────────────────────────────────────────────────
# C++ standard
# ─────────────────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)


# ─────────────────────────────────────────────────────────────────────────────
# Vendored SDK roots
#
# Default to third_party/ inside the project — no system installation needed.
# Override on the command line only if your SDK lives elsewhere:
#
#   Linux   : cmake -DLIBFTDI1_ROOT=/opt/myftdi ..
#   Windows : cmake -DFTD2XX_ROOT=C:/SDKs/ftd2xx ..
#
# Expected layout
# ───────────────
#   third_party/
#     libftdi1/                 ← Linux
#       include/ftdi.h
#       lib/libftdi1.a          (or .so)
#       lib/libusb-1.0.a        (only if LIBUSB_VENDORED=ON)
#
#     ftd2xx/                   ← Windows
#       ftd2xx.h
#       amd64/ftd2xx.lib        (64-bit)
#       i386/ftd2xx.lib         (32-bit)
# ─────────────────────────────────────────────────────────────────────────────
set(LIBFTDI1_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libftdi1"
    CACHE PATH "Root of the vendored libftdi1 SDK (Linux / macOS)")

set(FTD2XX_ROOT   "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ftd2xx"
    CACHE PATH "Root of the vendored FTD2XX SDK (Windows)")


# ─────────────────────────────────────────────────────────────────────────────
# Warning flags
# ─────────────────────────────────────────────────────────────────────────────
if(MSVC)
    set(FT4232_WARN_FLAGS /W4 /permissive-)
else()
    set(FT4232_WARN_FLAGS -Wall -Wextra -Wpedantic)
endif()


# ─────────────────────────────────────────────────────────────────────────────
# Platform dependency resolution → produces IMPORTED target  ft4232::sdk
#
# Everything SDK-specific (include paths, link libraries) is encapsulated in
# this single imported target.  The two library targets below simply link it;
# no SDK logic is repeated.
# ─────────────────────────────────────────────────────────────────────────────
if(WIN32)

    find_path(FTD2XX_INCLUDE_DIR
        NAMES         ftd2xx.h
        PATHS         "${FTD2XX_ROOT}"
        NO_DEFAULT_PATH
        DOC           "Directory containing ftd2xx.h")
    if(NOT FTD2XX_INCLUDE_DIR)
        message(FATAL_ERROR
            "ftd2xx.h not found under FTD2XX_ROOT=${FTD2XX_ROOT}\n"
            "Download the D2XX SDK from https://ftdichip.com/drivers/d2xx-drivers/\n"
            "and unpack it into third_party/ftd2xx/  (or pass -DFTD2XX_ROOT=<path>).")
    endif()

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_ftd2xx_arch "${FTD2XX_ROOT}/amd64")
    else()
        set(_ftd2xx_arch "${FTD2XX_ROOT}/i386")
    endif()

    find_library(FTD2XX_LIBRARY
        NAMES         ftd2xx
        PATHS         "${_ftd2xx_arch}"
        NO_DEFAULT_PATH
        DOC           "FTD2XX import library")
    if(NOT FTD2XX_LIBRARY)
        message(FATAL_ERROR
            "ftd2xx.lib not found under ${_ftd2xx_arch}\n"
            "Expected: ${FTD2XX_ROOT}/amd64/ftd2xx.lib  (64-bit)\n"
            "          ${FTD2XX_ROOT}/i386/ftd2xx.lib   (32-bit)")
    endif()

    message(STATUS "[ft4232] FTD2XX header : ${FTD2XX_INCLUDE_DIR}")
    message(STATUS "[ft4232] FTD2XX library: ${FTD2XX_LIBRARY}")

    add_library(ft4232::sdk STATIC IMPORTED)
    set_target_properties(ft4232::sdk PROPERTIES
        IMPORTED_LOCATION             "${FTD2XX_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${FTD2XX_INCLUDE_DIR}")

    set(FT4232_PLATFORM_COMPILE_OPTS
        $<$<CXX_COMPILER_ID:GNU>:-municode>)

else()  # ── Linux / macOS ────────────────────────────────────────────────────

    find_path(LIBFTDI1_INCLUDE_DIR
        NAMES         ftdi.h
        PATHS         "${LIBFTDI1_ROOT}/include" "${LIBFTDI1_ROOT}"
        NO_DEFAULT_PATH
        DOC           "Directory containing ftdi.h")
    if(NOT LIBFTDI1_INCLUDE_DIR)
        message(FATAL_ERROR
            "ftdi.h not found under LIBFTDI1_ROOT=${LIBFTDI1_ROOT}\n"
            "Place the SDK in third_party/libftdi1/ (or pass -DLIBFTDI1_ROOT=<path>).\n"
            "Quick extraction without installing:\n"
            "  cd third_party/libftdi1\n"
            "  apt-get download libftdi1-dev libftdi1-2\n"
            "  dpkg -x libftdi1-dev*.deb . && dpkg -x libftdi1-2*.deb .\n"
            "  mkdir -p include lib\n"
            "  mv usr/include/*/ftdi.h     include/\n"
            "  mv usr/lib/*/libftdi1.*     lib/")
    endif()

    find_library(LIBFTDI1_LIBRARY
        NAMES         ftdi1 ftdi
        PATHS         "${LIBFTDI1_ROOT}/lib" "${LIBFTDI1_ROOT}"
        NO_DEFAULT_PATH
        DOC           "libftdi1 library (static or shared)")
    if(NOT LIBFTDI1_LIBRARY)
        message(FATAL_ERROR
            "libftdi1 not found under ${LIBFTDI1_ROOT}/lib")
    endif()

    message(STATUS "[ft4232] libftdi1 header : ${LIBFTDI1_INCLUDE_DIR}")
    message(STATUS "[ft4232] libftdi1 library: ${LIBFTDI1_LIBRARY}")

    # libftdi1 depends on libusb-1.0.
    # Default: use the system libusb (header not needed, only the link).
    # Set LIBUSB_VENDORED=ON to use a libusb-1.0.a from third_party/libftdi1/lib/.
    option(LIBUSB_VENDORED
        "Use a vendored libusb-1.0.a from third_party/libftdi1/lib/" OFF)

    if(LIBUSB_VENDORED)
        find_library(LIBUSB_LIBRARY
            NAMES usb-1.0
            PATHS "${LIBFTDI1_ROOT}/lib"
            NO_DEFAULT_PATH)
        if(NOT LIBUSB_LIBRARY)
            message(FATAL_ERROR
                "LIBUSB_VENDORED=ON but libusb-1.0.a not found in ${LIBFTDI1_ROOT}/lib")
        endif()
        message(STATUS "[ft4232] libusb-1.0 (vendored): ${LIBUSB_LIBRARY}")
    else()
        find_library(LIBUSB_LIBRARY NAMES usb-1.0 DOC "System libusb-1.0")
        if(NOT LIBUSB_LIBRARY)
            message(FATAL_ERROR
                "System libusb-1.0 not found. Install libusb-1.0-0-dev "
                "or vendor it with -DLIBUSB_VENDORED=ON.")
        endif()
        message(STATUS "[ft4232] libusb-1.0 (system): ${LIBUSB_LIBRARY}")
    endif()

    find_package(Threads REQUIRED)

    add_library(ft4232::sdk UNKNOWN IMPORTED)
    set_target_properties(ft4232::sdk PROPERTIES
        IMPORTED_LOCATION             "${LIBFTDI1_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBFTDI1_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES      "${LIBUSB_LIBRARY};Threads::Threads")

    set(FT4232_PLATFORM_COMPILE_OPTS "")

endif()


# ─────────────────────────────────────────────────────────────────────────────
# ft4232_base  —  platform layer  (FT4232Base: open, close, mpsse_*)
#
# Contains only the OS-specific code.  Compiled exactly ONCE, regardless of
# how many protocol libraries are built.  Not intended to be linked directly
# by application code — use ft4232 (below) instead.
# ─────────────────────────────────────────────────────────────────────────────
add_library(ft4232_base STATIC
    $<IF:$<BOOL:${WIN32}>,src/uFT4232Windows.cpp,src/uFT4232Linux.cpp>
)

target_include_directories(ft4232_base
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(ft4232_base
    PUBLIC ft4232::sdk          # propagates SDK includes + libs to dependents
)

target_compile_options(ft4232_base PRIVATE
    ${FT4232_WARN_FLAGS}
    ${FT4232_PLATFORM_COMPILE_OPTS}
)


# ─────────────────────────────────────────────────────────────────────────────
# ft4232  —  unified protocol library  (I²C + SPI + GPIO)
#
# This is the only target application code should link against:
#
#   target_link_libraries(my_app PRIVATE ft4232)
#
# To add a future protocol:
#   1. Add inc/uFT4232<Proto>.hpp
#   2. Add src/uFT4232<Proto>Common.cpp to the source list below
#   3. Nothing else — ft4232_base and the SDK chain are already wired up.
# ─────────────────────────────────────────────────────────────────────────────
add_library(ft4232 STATIC
    src/uFT4232I2CCommon.cpp
    src/uFT4232SPICommon.cpp
    src/uFT4232GPIOCommon.cpp
)

target_include_directories(ft4232
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
        $<INSTALL_INTERFACE:include>
)

# Linking ft4232_base pulls in:
#   • the compiled FT4232Base object code (open_device, mpsse_write, …)
#   • the SDK includes  (ftdi.h / ftd2xx.h) via ft4232::sdk's INTERFACE
#   • the SDK libraries (libftdi1 / ftd2xx.lib) via ft4232::sdk's INTERFACE
target_link_libraries(ft4232
    PUBLIC ft4232_base
)

target_compile_options(ft4232 PRIVATE
    ${FT4232_WARN_FLAGS}
)
