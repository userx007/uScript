cmake_minimum_required(VERSION 3.25)
project(uDdsTypedDriver)

find_package(CycloneDDS REQUIRED)

add_library(${PROJECT_NAME} STATIC
    src/dds_typed_driver.cpp
)

# See src/lib/drivers/dds/CMakeLists.txt's identical rationale: std::span/
# std::string_view need C++20, and this static library ends up linked into
# dds_typed_plugin's SHARED library.
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
    uIDdsTypePluginAbi
    CycloneDDS::ddsc
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${CMAKE_DL_LIBS} # dlopen()/dlsym()/dlclose() — customer type plugin loading
)
