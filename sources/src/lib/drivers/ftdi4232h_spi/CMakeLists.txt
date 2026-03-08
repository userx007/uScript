cmake_minimum_required(VERSION 3.16)
project(ft4232 VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

set(FT4232_PLATFORM_SRC
    $<IF:$<BOOL:${WIN32}>,src/uFT4232Windows.cpp,src/uFT4232Linux.cpp>
)

set(FT4232_INC_DIR
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

if(MSVC)
    set(FT4232_WARN_FLAGS /W4 /permissive-)
else()
    set(FT4232_WARN_FLAGS -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    if(NOT DEFINED FTD2XX_ROOT)
        message(FATAL_ERROR
            "FTD2XX_ROOT is not set.\n"
            "Download the D2XX SDK from https://ftdichip.com/drivers/d2xx-drivers/\n"
            "then pass -DFTD2XX_ROOT=<path> to cmake.")
    endif()
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(FTD2XX_LIB "${FTD2XX_ROOT}/amd64/ftd2xx.lib")
    else()
        set(FTD2XX_LIB "${FTD2XX_ROOT}/i386/ftd2xx.lib")
    endif()
    set(FT4232_PLATFORM_LIBS  "${FTD2XX_LIB}")
    set(FT4232_PLATFORM_INCS  "${FTD2XX_ROOT}")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(FT4232_PLATFORM_COMPILE_OPTS -municode)
    endif()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBFTDI REQUIRED libftdi1)
    find_package(Threads REQUIRED)
    set(FT4232_PLATFORM_LIBS  ${LIBFTDI_LIBRARIES} Threads::Threads)
    set(FT4232_PLATFORM_INCS  ${LIBFTDI_INCLUDE_DIRS})
    set(FT4232_PLATFORM_COMPILE_OPTS "")
endif()

# ── Helper macro ──────────────────────────────────────────────────────────────
# ft4232_add_library(<name> <protocol_common_src>)
# Creates a self-contained STATIC library from the protocol common file
# and the platform file.  Each library can be linked independently.
macro(ft4232_add_library TARGET_NAME COMMON_SRC)
    add_library(${TARGET_NAME} STATIC
        ${COMMON_SRC}
        ${FT4232_PLATFORM_SRC}
    )
    target_include_directories(${TARGET_NAME} PUBLIC  ${FT4232_INC_DIR})
    target_include_directories(${TARGET_NAME} PRIVATE ${FT4232_PLATFORM_INCS})
    target_link_libraries     (${TARGET_NAME} PUBLIC  ${FT4232_PLATFORM_LIBS})
    target_compile_options    (${TARGET_NAME} PRIVATE
        ${FT4232_WARN_FLAGS}
        ${FT4232_PLATFORM_COMPILE_OPTS}
    )
endmacro()

# ── Protocol targets ──────────────────────────────────────────────────────────
# Each is a self-contained static library — link only what you need:
#
#   target_link_libraries(my_app PRIVATE ft4232_i2c)
#   target_link_libraries(my_app PRIVATE ft4232_spi)
#   target_link_libraries(my_app PRIVATE ft4232_i2c ft4232_spi)
#
# Note: linking multiple ft4232_* libraries to the same executable will
# compile the platform file multiple times.  Once GPIO is added, consider
# factoring it into a shared ft4232_base static library.

ft4232_add_library(ft4232_i2c src/uFT4232I2CCommon.cpp)
ft4232_add_library(ft4232_spi src/uFT4232SPICommon.cpp)

# ft4232_add_library(ft4232_gpio src/uFT4232GPIOCommon.cpp)
