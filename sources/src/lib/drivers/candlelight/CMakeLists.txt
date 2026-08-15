cmake_minimum_required(VERSION 3.25)
project(uCandlelight)

set(SOURCES src/uCandlelight.cpp)

add_library(${PROJECT_NAME} STATIC ${SOURCES})

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

# ─────────────────────────────────────────────────────────────────────────────
# libusb-1.0 dependency
#
# Candlelight (gs_usb) is a native-USB protocol (control + bulk transfers),
# not a UART/serial one — see uCandlelight.hpp's header comment. libusb-1.0
# is already a dependency elsewhere in this codebase (the ftdi2xx driver's
# third_party/CMakeLists.txt links it via libftdi1); this target links the
# system library directly, the same "system lib, fail loudly if missing"
# pattern ftdi2xx's third_party/CMakeLists.txt uses.
# ─────────────────────────────────────────────────────────────────────────────
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBUSB1 QUIET libusb-1.0)
endif()

if(LIBUSB1_FOUND)
    target_include_directories(${PROJECT_NAME} PUBLIC ${LIBUSB1_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PUBLIC ${LIBUSB1_LIBRARIES})
else()
    find_library(LIBUSB1_LIBRARY NAMES usb-1.0 DOC "System libusb-1.0")
    if(NOT LIBUSB1_LIBRARY)
        message(FATAL_ERROR "System libusb-1.0 not found. Install libusb-1.0-0-dev (Linux) or vcpkg's libusb (Windows).")
    endif()
    target_link_libraries(${PROJECT_NAME} PUBLIC ${LIBUSB1_LIBRARY})
endif()

target_link_libraries(${PROJECT_NAME} PUBLIC
    uICommDriver
    uUtils
    # Reuses SLCAN's public CAN data types and DLC<->length tables
    # (CanFrame, SLCAN::dlc_to_len/len_to_dlc) — see uCandlelight.hpp's
    # header comment.
    uSlcan
)
