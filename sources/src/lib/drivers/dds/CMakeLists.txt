cmake_minimum_required(VERSION 3.25)
project(uDdsDriver)

# Previously POSIX-only (this driver used to own its multicast-capable UDP
# sockets directly). Cyclone DDS ships its own portable network layer
# (including Windows), so that constraint no longer applies; uDdsDriver
# now only needs whatever platforms CycloneDDS::ddsc itself supports.
find_package(CycloneDDS REQUIRED)

add_library(${PROJECT_NAME} STATIC
    src/dds_driver.cpp
)

# Required for std::span/std::string_view (see ICommDriver.hpp) and because
# this static library ends up linked into dds_plugin's SHARED library —
# same convention as e.g. src/lib/drivers/pcan/CMakeLists.txt.
set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD               20
    CXX_STANDARD_REQUIRED      ON
    CXX_EXTENSIONS             OFF
    POSITION_INDEPENDENT_CODE  ON
)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    uICommDriver
    uUtils
    uDdsProtocol
    CycloneDDS::ddsc
)
