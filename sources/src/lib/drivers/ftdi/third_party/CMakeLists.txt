cmake_minimum_required(VERSION 3.16)
# ─────────────────────────────────────────────────────────────────────────────
# Platform dependency resolution → single IMPORTED target ftdi::sdk
# GLOBAL makes it visible to sibling subdirectories (ftdi2232, ftdi245, ftdi4232)
# ─────────────────────────────────────────────────────────────────────────────

if(WIN32)

    option(FTDI_STATIC "Link FTD2XX statically" OFF)

    set(FTD2XX_INCLUDE_DIR "${FTD2XX_ROOT}/include")

    if(NOT EXISTS "${FTD2XX_INCLUDE_DIR}/ftd2xx.h")
        message(FATAL_ERROR "ftd2xx.h not found in ${FTD2XX_INCLUDE_DIR}")
    endif()

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_arch amd64)
    else()
        set(_arch i386)
    endif()

    if(FTDI_STATIC)

        # Static linking
        set(FTD2XX_LIBRARY "${FTD2XX_ROOT}/static/${_arch}/FTD2XX.lib")
        if(NOT EXISTS "${FTD2XX_LIBRARY}")
            message(FATAL_ERROR "Static FTDI library not found: ${FTD2XX_LIBRARY}")
        endif()

        add_library(ftdi::sdk STATIC IMPORTED GLOBAL)
        set_target_properties(ftdi::sdk PROPERTIES
            IMPORTED_LOCATION "${FTD2XX_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${FTD2XX_INCLUDE_DIR}"
            INTERFACE_COMPILE_DEFINITIONS FTD2XX_STATIC
            INTERFACE_LINK_LIBRARIES "setupapi;user32;advapi32"
        )

        message(STATUS "[ftdi] static library: ${FTD2XX_LIBRARY}")

    else()

        # Dynamic linking → generate MinGW import library if necessary
        set(FTD2XX_DLL "${FTD2XX_ROOT}/${_arch}/FTD2XX64.dll")
        set(FTD2XX_IMPLIB "${FTD2XX_ROOT}/${_arch}/libftd2xx.a")

        if(NOT EXISTS "${FTD2XX_IMPLIB}")
            message(STATUS "[ftdi] Generating MinGW import library from DLL...")
            execute_process(
                COMMAND gendef "${FTD2XX_DLL}"
                WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
                RESULT_VARIABLE _gendef_ret
            )
            if(_gendef_ret)
                message(FATAL_ERROR "gendef failed for ${FTD2XX_DLL}")
            endif()
            execute_process(
                COMMAND x86_64-w64-mingw32-dlltool -d FTD2XX.def -l "${FTD2XX_IMPLIB}" -D FTD2XX64.dll
                WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
                RESULT_VARIABLE _dlltool_ret
            )
            if(_dlltool_ret)
                message(FATAL_ERROR "dlltool failed to create import library")
            endif()
        endif()

        add_library(ftdi::sdk SHARED IMPORTED GLOBAL)
        set_target_properties(ftdi::sdk PROPERTIES
            IMPORTED_IMPLIB "${FTD2XX_IMPLIB}"
            IMPORTED_LOCATION "${FTD2XX_DLL}"
            INTERFACE_INCLUDE_DIRECTORIES "${FTD2XX_INCLUDE_DIR}"
        )

        # Make the DLL available to targets
        set(FTDI_RUNTIME_DLL "${FTD2XX_DLL}" PARENT_SCOPE)

        message(STATUS "[ftdi] dynamic library: ${FTD2XX_IMPLIB}")

    endif()

    set(FTDI_PLATFORM_COMPILE_OPTS
        $<$<CXX_COMPILER_ID:GNU>:-municode>
        PARENT_SCOPE)

else()  # ── Linux / macOS ────────────────────────────────────────────────────

    find_path(LIBFTDI1_INCLUDE_DIR
        NAMES ftdi.h
        PATHS "${LIBFTDI1_ROOT}/include" "${LIBFTDI1_ROOT}"
        NO_DEFAULT_PATH
        DOC "Directory containing ftdi.h"
    )
    if(NOT LIBFTDI1_INCLUDE_DIR)
        message(FATAL_ERROR "ftdi.h not found under LIBFTDI1_ROOT=${LIBFTDI1_ROOT}")
    endif()

    find_library(LIBFTDI1_LIBRARY
        NAMES ftdi1 ftdi
        PATHS "${LIBFTDI1_ROOT}/lib" "${LIBFTDI1_ROOT}"
        NO_DEFAULT_PATH
        DOC "libftdi1 library"
    )
    if(NOT LIBFTDI1_LIBRARY)
        message(FATAL_ERROR "libftdi1 not found under ${LIBFTDI1_ROOT}/lib")
    endif()

    option(LIBUSB_VENDORED "Use vendored libusb-1.0.a from third_party/libftdi1/lib/" OFF)
    if(LIBUSB_VENDORED)
        find_library(LIBUSB_LIBRARY NAMES usb-1.0 PATHS "${LIBFTDI1_ROOT}/lib" NO_DEFAULT_PATH)
        if(NOT LIBUSB_LIBRARY)
            message(FATAL_ERROR "LIBUSB_VENDORED=ON but libusb-1.0.a not found")
        endif()
        message(STATUS "[ftdi] libusb-1.0 (vendored): ${LIBUSB_LIBRARY}")
    else()
        find_library(LIBUSB_LIBRARY NAMES usb-1.0 DOC "System libusb-1.0")
        if(NOT LIBUSB_LIBRARY)
            message(FATAL_ERROR "System libusb-1.0 not found. Install libusb-1.0-0-dev")
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
        INTERFACE_LINK_LIBRARIES      "${LIBUSB_LIBRARY};Threads::Threads"
    )

    set(FTDI_PLATFORM_COMPILE_OPTS "" PARENT_SCOPE)

endif()