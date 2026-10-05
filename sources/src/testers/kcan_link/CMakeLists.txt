cmake_minimum_required(VERSION 3.25)
project(kcan_link)

# kcan_link — command line front-end for the KCAN link layer (see kcan_link.cpp). Linux only.
add_executable(kcan_link kcan_link.cpp)
target_compile_features(kcan_link PRIVATE cxx_std_20)
target_link_libraries(kcan_link PRIVATE uKCan)
target_include_directories(kcan_link PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../plugins/kcan_plugin/inc/private)
