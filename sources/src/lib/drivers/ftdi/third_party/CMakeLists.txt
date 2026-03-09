cmake_minimum_required(VERSION 3.16)
# ─────────────────────────────────────────────────────────────────────────────
# Platform dependency resolution → single IMPORTED target  ftdi::sdk
# GLOBAL makes it visible to sibling subdirectories (ftdi2232, ftdi245, ftdi4232)
# ─────────────────────────────────────────────────────────────────────────────

if(WIN32)

    find_path(FTD2XX_INCLUDE_DIR
        NAMES         FTD2XX.H ftd2xx.h          # handles both casings
        PATHS         "${FTD2XX_ROOT}/amd64"
                      "${FTD2XX_ROOT}/i386"
                      "${FTD2XX_ROOT}"
        NO_DEFAULT_PATH
        DOC           "Directory containing FTD2XX.H")
    if(NOT FTD2XX_INCLUDE_DIR)
        message(FATAL_ERROR
            "FTD2XX.H not found under FTD2XX_ROOT=${FTD2XX_ROOT}\n"
            "Download the D2XX SDK from https://ftdichip.com/drivers/d2xx-drivers/\n"
            "and unpack it into third_party/ftd2xx/")
    endif()

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_ftd2xx_arch "${FTD2XX_ROOT}/amd64")
    else()
        set(_ftd2xx_arch "${FTD2XX_ROOT}/i386")
    endif()

    # NOTE: Your tree only shows FTD2XX.dll — you'll also need ftd2xx.lib (import lib).
    # The .lib stub is required to link against the DLL on Windows.
    find_library(FTD2XX_LIBRARY
        NAMES         ftd2xx FTD2XX
        PATHS         "${_ftd2xx_arch}"
        NO_DEFAULT_PATH
        DOC           "FTD2XX import library (.lib)")
    if(NOT FTD2XX_LIBRARY)
        message(FATAL_ERROR
            "ftd2xx.lib not found under ${_ftd2xx_arch}\n"
            "Expected: ${FTD2XX_ROOT}/amd64/ftd2xx.lib  (64-bit)\n"
            "          ${FTD2XX_ROOT}/i386/ftd2xx.lib   (32-bit)")
    endif()

    message(STATUS "[ftdi] FTD2XX header : ${FTD2XX_INCLUDE_DIR}")
    message(STATUS "[ftdi] FTD2XX library: ${FTD2XX_LIBRARY}")

    add_library(ftdi::sdk STATIC IMPORTED GLOBAL)
    set_target_properties(ftdi::sdk PROPERTIES
        IMPORTED_LOCATION             "${FTD2XX_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${FTD2XX_INCLUDE_DIR}")

    set(FTDI_PLATFORM_COMPILE_OPTS
        $<$<CXX_COMPILER_ID:GNU>:-municode>
        PARENT_SCOPE)

else()  # ── Linux / macOS ────────────────────────────────────────────────────

    find_path(LIBFTDI1_INCLUDE_DIR
        NAMES         ftdi.h
        PATHS         "${LIBFTDI1_ROOT}/include" "${LIBFTDI1_ROOT}"
        NO_DEFAULT_PATH
        DOC           "Directory containing ftdi.h")
    if(NOT LIBFTDI1_INCLUDE_DIR)
        message(FATAL_ERROR
            "ftdi.h not found under LIBFTDI1_ROOT=${LIBFTDI1_ROOT}\n"
            "Place the SDK in third_party/libftdi1/ (or pass -DLIBFTDI1_ROOT=<path>).")
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

    option(LIBUSB_VENDORED "Use a vendored libusb-1.0.a from third_party/libftdi1/lib/" OFF)

    if(LIBUSB_VENDORED)
        find_library(LIBUSB_LIBRARY
            NAMES usb-1.0
            PATHS "${LIBFTDI1_ROOT}/lib"
            NO_DEFAULT_PATH)
        if(NOT LIBUSB_LIBRARY)
            message(FATAL_ERROR
                "LIBUSB_VENDORED=ON but libusb-1.0.a not found in ${LIBFTDI1_ROOT}/lib")
        endif()
        message(STATUS "[ftdi] libusb-1.0 (vendored): ${LIBUSB_LIBRARY}")
    else()
        find_library(LIBUSB_LIBRARY NAMES usb-1.0 DOC "System libusb-1.0")
        if(NOT LIBUSB_LIBRARY)
            message(FATAL_ERROR
                "System libusb-1.0 not found. Install libusb-1.0-0-dev "
                "or vendor it with -DLIBUSB_VENDORED=ON.")
        endif()
        message(STATUS "[ftdi] libusb-1.0 (system): ${LIBUSB_LIBRARY}")
    endif()

    message(STATUS "[ftdi] libftdi1 header : ${LIBFTDI1_INCLUDE_DIR}")
    message(STATUS "[ftdi] libftdi1 library: ${LIBFTDI1_LIBRARY}")

    find_package(Threads REQUIRED)

    add_library(ftdi::sdk UNKNOWN IMPORTED GLOBAL)
    set_target_properties(ftdi::sdk PROPERTIES
        IMPORTED_LOCATION             "${LIBFTDI1_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBFTDI1_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES      "${LIBUSB_LIBRARY};Threads::Threads")

    set(FTDI_PLATFORM_COMPILE_OPTS "" PARENT_SCOPE)

endif()