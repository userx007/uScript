cmake_minimum_required(VERSION 3.16)
add_library(ft4232_base STATIC
    $<IF:$<BOOL:${WIN32}>,src/uFT4232Windows.cpp,src/uFT4232Linux.cpp>
)

target_include_directories(ft4232_base PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(ft4232_base PUBLIC ftdi::sdk)
target_compile_options(ft4232_base PRIVATE ${FTDI_WARN_FLAGS} ${FTDI_PLATFORM_COMPILE_OPTS})

add_library(ft4232 STATIC
    src/uFT4232I2CCommon.cpp
    src/uFT4232SPICommon.cpp
    src/uFT4232GPIOCommon.cpp
)

target_include_directories(ft4232 PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(ft4232 PUBLIC ft4232_base)
target_compile_options(ft4232 PRIVATE ${FTDI_WARN_FLAGS})

target_link_libraries(ft4232_base
    PUBLIC
        uICommDriver
        uUtils
)

target_link_libraries(ft4232
    PUBLIC
        uICommDriver
        uUtils
)
