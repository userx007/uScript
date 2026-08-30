cmake_minimum_required(VERSION 3.25)
project(uICommDriver)

add_library(${PROJECT_NAME} INTERFACE)

target_include_directories(${PROJECT_NAME} INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

# Separate target (used only by DdsTypedDriver / customer type plugin .so's —
# see DdsTypePluginAbi.h's doc comment) rather than folded into uICommDriver
# above: it needs CycloneDDS::ddsc (for dds_topic_descriptor_t) and nothing
# else in this repo should have to pull that in just to get ICommDriver.hpp.
find_package(CycloneDDS REQUIRED)

add_library(uIDdsTypePluginAbi INTERFACE)

target_include_directories(uIDdsTypePluginAbi INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(uIDdsTypePluginAbi INTERFACE CycloneDDS::ddsc)
