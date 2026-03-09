cmake_minimum_required(VERSION 3.16)
# ── Platform layer (compiled once) ───────────────────────────────────────────
add_library(ft232h_base STATIC
    $<IF:$<BOOL:${WIN32}>,src/uFT232HWindows.cpp,src/uFT232HLinux.cpp>
)

target_include_directories(ft232h_base PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(ft232h_base PUBLIC ftdi::sdk)

target_compile_options(ft232h_base PRIVATE
    ${FTDI_WARN_FLAGS}
    ${FTDI_PLATFORM_COMPILE_OPTS}
)

# ── Unified protocol library (I²C + SPI + GPIO) ───────────────────────────────
add_library(ft232h STATIC
    src/uFT232HI2CCommon.cpp
    src/uFT232HSPICommon.cpp
    src/uFT232HGPIOCommon.cpp
)

target_include_directories(ft232h PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

# ft232h_base transitively pulls in: SDK headers, SDK libs, platform object code
target_link_libraries(ft232h PUBLIC ft232h_base)

target_compile_options(ft232h PRIVATE ${FTDI_WARN_FLAGS})

target_link_libraries(ft232h_base
    PUBLIC
        uICommDriver
        uUtils
)

target_link_libraries(ft232h
    PUBLIC
        uICommDriver
        uUtils
)
