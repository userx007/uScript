#!/bin/bash
set -e
cd "$(dirname "$0")"

command -v protoc >/dev/null || { echo "protoc not found (install protobuf-compiler)"; exit 1; }
command -v grpc_cpp_plugin >/dev/null || { echo "grpc_cpp_plugin not found (install protobuf-compiler-grpc)"; exit 1; }

# Generated C++ stubs for the server below.
protoc --cpp_out=. --grpc_out=. --plugin=protoc-gen-grpc="$(which grpc_cpp_plugin)" loopback.proto

# The descriptor set the GRPC plugin itself needs (GRPC.CONFIG d=loopback.protoset)
# to resolve LoopbackService's methods/messages at runtime - see
# docs/grpc_plugin_tutorial.md section 2 for what this file is and why.
protoc --descriptor_set_out=loopback.protoset --include_imports loopback.proto

g++ -std=c++17 -O2 -Wall -Wextra -o grpc_loopback_server \
    grpc_loopback_server.cpp loopback.pb.cc loopback.grpc.pb.cc \
    $(pkg-config --cflags --libs grpc++ protobuf)

echo "Built grpc_loopback_server and loopback.protoset"
