cmake_minimum_required(VERSION 3.25)
project(uGrpcProtocol)

find_package(Protobuf REQUIRED)

add_library(${PROJECT_NAME} STATIC
    src/grpc_protocol.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    protobuf::libprotobuf
)
