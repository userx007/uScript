cmake_minimum_required(VERSION 3.25)
project(uGrpcDriver)

find_package(gRPC CONFIG QUIET)
if(NOT gRPC_FOUND)
    # Distro packages (e.g. Debian/Ubuntu's libgrpc++-dev) don't always ship
    # a gRPCConfig.cmake; fall back to pkg-config, which they do provide.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GRPCPP REQUIRED grpc++)
endif()

add_library(${PROJECT_NAME} STATIC
    src/grpc_driver.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    uICommDriver
    uUtils
    uGrpcProtocol
    $<IF:$<TARGET_EXISTS:gRPC::grpc++>,gRPC::grpc++,${GRPCPP_LIBRARIES}>
)

target_include_directories(${PROJECT_NAME} PRIVATE ${GRPCPP_INCLUDE_DIRS})
target_link_directories(${PROJECT_NAME} PRIVATE ${GRPCPP_LIBRARY_DIRS})
target_compile_options(${PROJECT_NAME} PRIVATE ${GRPCPP_CFLAGS_OTHER})
