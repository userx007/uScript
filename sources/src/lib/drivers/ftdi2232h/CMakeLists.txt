cmake_minimum_required(VERSION 3.16)
project(ft2232 VERSION 1.0.0 LANGUAGES CXX)

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
# Override on the command line if your SDK lives elsewhere:
#
#   Linux   : cmake -DLIBFTDI1_ROOT=/opt/myftdi ..
#   Windows : cmake -DFTD2XX_ROOT=C:/SDKs/ftd2xx ..
#
# Expected layout
# ───────────────
#   third_party/
#     libftdi1/               ← Linux
#       include/ftdi.h
#       lib/libftdi1.a        (or .so)
#       lib/libusb-1.0.a      (only needed if LIBUSB_VENDORED=ON)
#
#     ftd2xx/                 ← Windows
#       ftd2xx.h
#       amd64/ftd2xx.lib      (64-bit)
#       i386/ftd2xx.lib       (32-bit)
#
# The same SDK is used for both FT2232H and FT2232D — variant selection is
# done at runtime by passing the correct Variant enum value to open().
# ─────────────────────────────────────────────────────────────────────────────
set(LIBFTDI1_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libftdi1"
    CACHE PATH "Root of the vendored libftdi1 SDK (Linux / macOS)")

set(FTD2XX_ROOT   "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ftd2xx"
    CACHE PATH "Root of the vendored FTD2XX SDK (Windows)")


# ─────────────────────────────────────────────────────────────────────────────
# Warning flags
# ─────────────────────────────────────────────────────────────────────────────
if(MSVC)
    set(FT2232_WARN_FLAGS /W4 /permissive-)
else()
    set(FT2232_WARN_FLAGS -Wall -Wextra -Wpedantic)
endif()


# ─────────────────────────────────────────────────────────────────────────────
# Platform dependency resolution → single IMPORTED target  ft2232::sdk
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

    message(STATUS "[ft2232] FTD2XX header : ${FTD2XX_INCLUDE_DIR}")
    message(STATUS "[ft2232] FTD2XX library: ${FTD2XX_LIBRARY}")

    add_library(ft2232::sdk STATIC IMPORTED)
    set_target_properties(ft2232::sdk PROPERTIES
        IMPORTED_LOCATION             "${FTD2XX_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${FTD2XX_INCLUDE_DIR}")

    set(FT2232_PLATFORM_COMPILE_OPTS
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
            "  mv usr/include/*/ftdi.h include/\n"
            "  mv usr/lib/*/libftdi1.* lib/")
    endif()

    find_library(LIBFTDI1_LIBRARY
        NAMES         ftdi1 ftdi
        PATHS         "${LIBFTDI1_ROOT}/lib" "${LIBFTDI1_ROOT}"
        NO_DEFAULT_PATH
        DOC           "libftdi1 library")
    if(NOT LIBFTDI1_LIBRARY)
        message(FATAL_ERROR
            "libftdi1 not found under ${LIBFTDI1_ROOT}/lib")
    endif()

    message(STATUS "[ft2232] libftdi1 header : ${LIBFTDI1_INCLUDE_DIR}")
    message(STATUS "[ft2232] libftdi1 library: ${LIBFTDI1_LIBRARY}")

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
        message(STATUS "[ft2232] libusb-1.0 (vendored): ${LIBUSB_LIBRARY}")
    else()
        find_library(LIBUSB_LIBRARY NAMES usb-1.0 DOC "System libusb-1.0")
        if(NOT LIBUSB_LIBRARY)
            message(FATAL_ERROR
                "System libusb-1.0 not found. Install libusb-1.0-0-dev "
                "or vendor it with -DLIBUSB_VENDORED=ON.")
        endif()
        message(STATUS "[ft2232] libusb-1.0 (system): ${LIBUSB_LIBRARY}")
    endif()

    find_package(Threads REQUIRED)

    add_library(ft2232::sdk UNKNOWN IMPORTED)
    set_target_properties(ft2232::sdk PROPERTIES
        IMPORTED_LOCATION             "${LIBFTDI1_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBFTDI1_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES      "${LIBUSB_LIBRARY};Threads::Threads")

    set(FT2232_PLATFORM_COMPILE_OPTS "")

endif()


# ─────────────────────────────────────────────────────────────────────────────
# ft2232_base  —  platform layer  (FT2232Base: open_device, close, mpsse_*)
#
# Compiled exactly once regardless of how many protocol targets are linked.
# ─────────────────────────────────────────────────────────────────────────────
add_library(ft2232_base STATIC
    $<IF:$<BOOL:${WIN32}>,src/uFT2232Windows.cpp,src/uFT2232Linux.cpp>
)

target_include_directories(ft2232_base
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(ft2232_base
    PUBLIC ft2232::sdk
)

target_compile_options(ft2232_base PRIVATE
    ${FT2232_WARN_FLAGS}
    ${FT2232_PLATFORM_COMPILE_OPTS}
)


# ─────────────────────────────────────────────────────────────────────────────
# ft2232  —  unified protocol library  (I²C + SPI + GPIO)
#
# The only target application code should link:
#
#   target_link_libraries(my_app PRIVATE ft2232)
#
# To add a future protocol:
#   1. Add inc/uFT2232<Proto>.hpp
#   2. Add src/uFT2232<Proto>Common.cpp to the list below
# ─────────────────────────────────────────────────────────────────────────────
add_library(ft2232 STATIC
    src/uFT2232I2CCommon.cpp
    src/uFT2232SPICommon.cpp
    src/uFT2232GPIOCommon.cpp
)

target_include_directories(ft2232
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
        $<INSTALL_INTERFACE:include>
)

# Linking ft2232_base transitively pulls in:
#   • FT2232Base object code  (open_device, mpsse_write, …)
#   • SDK includes            (ftdi.h / ftd2xx.h)
#   • SDK link libraries      (libftdi1 / ftd2xx.lib)
target_link_libraries(ft2232
    PUBLIC ft2232_base
)

target_compile_options(ft2232 PRIVATE
    ${FT2232_WARN_FLAGS}
)
