cmake_minimum_required(VERSION 3.16)
# ── Platform layer (compiled once) ───────────────────────────────────────────
add_library(ft2232_base STATIC
    $<IF:$<BOOL:${WIN32}>,src/uFT2232Windows.cpp,src/uFT2232Linux.cpp>
)

target_include_directories(ft2232_base PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(ft2232_base PUBLIC ftdi::sdk)

target_compile_options(ft2232_base PRIVATE
    ${FTDI_WARN_FLAGS}
    ${FTDI_PLATFORM_COMPILE_OPTS}
)

# ── Unified protocol library (I²C + SPI + GPIO + UART) ───────────────────────────────
add_library(ft2232 STATIC
    src/uFT2232I2CCommon.cpp
    src/uFT2232SPICommon.cpp
    src/uFT2232GPIOCommon.cpp
    src/uFT2232UARTCommon.cpp
    $<IF:$<BOOL:${WIN32}>,src/uFT2232UARTWindows.cpp,src/uFT2232UARTLinux.cpp>
)

target_include_directories(ft2232 PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

# ft2232_base transitively pulls in: SDK headers, SDK libs, platform object code
target_link_libraries(ft2232 PUBLIC ft2232_base)

target_compile_options(ft2232 PRIVATE ${FTDI_WARN_FLAGS})

target_link_libraries(ft2232_base
    PUBLIC
        uICommDriver
        uUtils
)

target_link_libraries(ft2232
    PUBLIC
        uICommDriver
        uUtils
)
