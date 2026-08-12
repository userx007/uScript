cmake_minimum_required(VERSION 3.25)
project(grpc_plugin)

find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG QUIET)
if(NOT gRPC_FOUND)
    # Distro packages (e.g. Debian/Ubuntu's libgrpc++-dev) don't always ship
    # a gRPCConfig.cmake; fall back to pkg-config, which they do provide.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GRPCPP REQUIRED grpc++)
endif()

add_library(${PROJECT_NAME}
    SHARED
        src/grpc_plugin.cpp
        src/grpc_driver.cpp
        src/grpc_protocol.cpp
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/inc
        ${CMAKE_CURRENT_SOURCE_DIR}/inc/private
    PRIVATE
        ${GRPCPP_INCLUDE_DIRS}
)

target_link_libraries( ${PROJECT_NAME}
    PUBLIC
        uPluginSettings
        uCommandExec
        uIPlugin
        uICommDriver
        uPluginOps
    PRIVATE
        protobuf::libprotobuf
        $<IF:$<TARGET_EXISTS:gRPC::grpc++>,gRPC::grpc++,${GRPCPP_LIBRARIES}>
)

target_link_directories(${PROJECT_NAME} PRIVATE ${GRPCPP_LIBRARY_DIRS})
target_compile_options(${PROJECT_NAME} PRIVATE ${GRPCPP_CFLAGS_OTHER})
