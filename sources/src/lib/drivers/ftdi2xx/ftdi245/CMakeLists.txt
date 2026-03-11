cmake_minimum_required(VERSION 3.16)
# ── Platform layer (compiled once) ───────────────────────────────────────────
add_library(ft245_base STATIC
    $<IF:$<BOOL:${WIN32}>,src/uFT245Windows.cpp,src/uFT245Linux.cpp>
)

target_include_directories(ft245_base PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(ft245_base PUBLIC ftdi::sdk)

target_compile_options(ft245_base PRIVATE
    ${FTDI_WARN_FLAGS}
    ${FTDI_PLATFORM_COMPILE_OPTS}
)

# ── Unified protocol library (Sync FIFO + GPIO) ───────────────────────────────
add_library(ft245 STATIC
    src/uFT245SyncCommon.cpp
    src/uFT245GPIOCommon.cpp
)

target_include_directories(ft245 PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

# ft245_base transitively pulls in: SDK headers, SDK libs, platform object code
target_link_libraries(ft245 PUBLIC ft245_base)

target_compile_options(ft245 PRIVATE ${FTDI_WARN_FLAGS})

target_link_libraries(ft245_base
    PUBLIC
        uICommDriver
        uUtils
)

target_link_libraries(ft245
    PUBLIC
        uICommDriver
        uUtils
)
