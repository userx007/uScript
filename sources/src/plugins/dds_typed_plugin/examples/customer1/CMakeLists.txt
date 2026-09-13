cmake_minimum_required(VERSION 3.25)
project(customer1_types C)

# Reference example only — not part of any real customer's build, and not
# linked or depended on by dds_typed_plugin or uDdsTypedDriver (which
# never mention it by name; see DdsTypePluginAbi.h's doc comment for why
# that's the whole point). Demonstrates the complete idl -> .so pipeline
# a real customer would follow; see this directory's src/customer1_adapter.c
# for the one hand-written file involved.
find_package(CycloneDDS REQUIRED)

idlc_generate(TARGET customer1_types_idl FILES idl/customer1.idl)

add_library(${PROJECT_NAME} SHARED
    src/customer1_adapter.c
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include/driver/inc # DdsTypePluginAbi.h
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    uICommDriver
    customer1_types_idl
    CycloneDDS::ddsc
)
