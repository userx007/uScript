cmake_minimum_required(VERSION 3.17)
project(HydraHAL VERSION 1.0.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# C++ standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------
add_library(HydraHAL
    src/Hydrabus.cpp
    src/AUXPin.cpp
    src/Protocol.cpp
    src/SPI.cpp
    src/I2C.cpp
    src/UART.cpp
    src/OneWire.cpp
    src/RawWire.cpp
    src/SWD.cpp
    src/Smartcard.cpp
    src/NFC.cpp
    src/MMC.cpp
    src/SDIO.cpp
    src/Utils.cpp
)

target_include_directories(HydraHAL
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
        $<INSTALL_INTERFACE:include/HydraHAL>
)

# ---------------------------------------------------------------------------
# ICommDriver header – expected to be provided by the consuming project.
# If it lives somewhere else, override with:
#   cmake -DICOMMDRIVER_INCLUDE_DIR=/path/to/dir ..
# ---------------------------------------------------------------------------
if(DEFINED ICOMMDRIVER_INCLUDE_DIR)
    target_include_directories(HydraHAL PUBLIC ${ICOMMDRIVER_INCLUDE_DIR})
endif()

# ---------------------------------------------------------------------------
# Compiler warnings
# ---------------------------------------------------------------------------
target_compile_options(HydraHAL PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion>
    $<$<CXX_COMPILER_ID:MSVC>:
        /W4>
)

# ---------------------------------------------------------------------------
# Optional: install rules
# ---------------------------------------------------------------------------
#include(GNUInstallDirs)
#
#install(TARGETS HydraHAL
#    EXPORT  HydraHALTargets
#    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
#    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
#    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
#)
#
#install(FILES
#    HydraHAL.hpp
#    common.hpp
#    Hydrabus.hpp
#    AUXPin.hpp
#    Protocol.hpp
#    SPI.hpp
#    I2C.hpp
#    UART.hpp
#    OneWire.hpp
#    RawWire.hpp
#    SWD.hpp
#    Smartcard.hpp
#    NFC.hpp
#    MMC.hpp
#    SDIO.hpp
#    Utils.hpp
#    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/HydraHAL
#)

target_link_libraries(HydraHAL
    PUBLIC
        uICommDriver
        uUtils
)
