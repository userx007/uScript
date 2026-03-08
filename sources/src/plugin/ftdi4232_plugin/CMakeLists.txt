cmake_minimum_required(VERSION 3.16)
project(ft4232_plugin VERSION 1.0.0 LANGUAGES CXX)

# ─────────────────────────────────────────────────────────────────────────────
# C++ standard
# ─────────────────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# ─────────────────────────────────────────────────────────────────────────────
# External dependency paths (override on the cmake command line)
#
#   cmake -B build \
#       -DFT4232_INCLUDE_DIR=/path/to/ft4232h/inc \
#       -DFT4232_LIB_DIR=/path/to/ft4232h/build  \
#       -DPLUGIN_FRAMEWORK_INCLUDE_DIR=/path/to/plugin/inc
#
# The ft4232 CMake project (from the ftdi4232h library) must have been
# built first:  cmake --build /path/to/ft4232h/build
#
# Required targets / libraries from that project:
#   libft4232.a   (protocol layer: I2C, SPI, GPIO)
#   libft4232_base.a (platform layer: Linux or Windows transport)
#
# The SDK libraries (libftdi1 / ftd2xx) are transitively linked by the
# ft4232 CMake targets' INTERFACE_LINK_LIBRARIES — no explicit mention
# needed here.
# ─────────────────────────────────────────────────────────────────────────────

set(FT4232_INCLUDE_DIR ""
    CACHE PATH "Directory containing FT4232Base.hpp, uFT4232SPI.hpp, ...")

set(FT4232_LIB_DIR ""
    CACHE PATH "Directory containing libft4232.a and libft4232_base.a")

set(PLUGIN_FRAMEWORK_INCLUDE_DIR ""
    CACHE PATH "Directory containing IPlugin.hpp, PluginOperations.hpp, ...")

if(NOT FT4232_INCLUDE_DIR)
    message(FATAL_ERROR
        "FT4232_INCLUDE_DIR is not set.\n"
        "Pass -DFT4232_INCLUDE_DIR=/path/to/ftdi4232h/inc")
endif()

if(NOT FT4232_LIB_DIR)
    message(FATAL_ERROR
        "FT4232_LIB_DIR is not set.\n"
        "Build the ft4232 library first, then pass -DFT4232_LIB_DIR=/path/to/build")
endif()

if(NOT PLUGIN_FRAMEWORK_INCLUDE_DIR)
    message(FATAL_ERROR
        "PLUGIN_FRAMEWORK_INCLUDE_DIR is not set.\n"
        "Pass -DPLUGIN_FRAMEWORK_INCLUDE_DIR=/path/to/plugin-framework/inc")
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Locate ft4232 libraries
# ─────────────────────────────────────────────────────────────────────────────
find_library(FT4232_LIB
    NAMES ft4232
    PATHS "${FT4232_LIB_DIR}"
    NO_DEFAULT_PATH
    DOC "ft4232 protocol library (libft4232.a)")
if(NOT FT4232_LIB)
    message(FATAL_ERROR "libft4232 not found in FT4232_LIB_DIR=${FT4232_LIB_DIR}")
endif()

find_library(FT4232_BASE_LIB
    NAMES ft4232_base
    PATHS "${FT4232_LIB_DIR}"
    NO_DEFAULT_PATH
    DOC "ft4232 platform library (libft4232_base.a)")
if(NOT FT4232_BASE_LIB)
    message(FATAL_ERROR "libft4232_base not found in FT4232_LIB_DIR=${FT4232_LIB_DIR}")
endif()

message(STATUS "[ft4232_plugin] ft4232     : ${FT4232_LIB}")
message(STATUS "[ft4232_plugin] ft4232_base: ${FT4232_BASE_LIB}")

# ─────────────────────────────────────────────────────────────────────────────
# Warning flags
# ─────────────────────────────────────────────────────────────────────────────
if(MSVC)
    set(FT4232_PLUGIN_WARN_FLAGS /W4 /permissive-)
else()
    set(FT4232_PLUGIN_WARN_FLAGS -Wall -Wextra -Wpedantic)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Plugin shared library target
# ─────────────────────────────────────────────────────────────────────────────
add_library(ft4232_plugin SHARED
    src/ft4232_plugin.cpp
    src/ft4232_spi.cpp
    src/ft4232_i2c.cpp
    src/ft4232_gpio.cpp
)

target_include_directories(ft4232_plugin
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/inc
        ${CMAKE_CURRENT_SOURCE_DIR}/inc/private
        ${FT4232_INCLUDE_DIR}
        ${PLUGIN_FRAMEWORK_INCLUDE_DIR}
)

target_link_libraries(ft4232_plugin
    PRIVATE
        ${FT4232_LIB}
        ${FT4232_BASE_LIB}
        # The ft4232 library's CMake targets carry INTERFACE_LINK_LIBRARIES
        # with libftdi1/libusb (Linux) or ftd2xx.lib (Windows) — those are
        # pulled in transitively when linking against FT4232_LIB.
        # If you link against the raw .a files as above, add the SDK libs
        # explicitly if needed (they are in the ft4232 build system):
        #   $<IF:$<BOOL:${WIN32}>, ftd2xx, ftdi1;usb-1.0>
)

target_compile_options(ft4232_plugin PRIVATE
    ${FT4232_PLUGIN_WARN_FLAGS}
)

# ─────────────────────────────────────────────────────────────────────────────
# Install
# ─────────────────────────────────────────────────────────────────────────────
# install(TARGETS ft4232_plugin
#     LIBRARY DESTINATION lib/plugins
#     RUNTIME DESTINATION lib/plugins   # Windows DLL
# )
# 
# install(DIRECTORY inc/
#     DESTINATION include/ft4232_plugin
#     FILES_MATCHING PATTERN "*.hpp"
#     PATTERN "private" EXCLUDE
# )
