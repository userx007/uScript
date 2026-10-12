cmake_minimum_required(VERSION 3.25)

# Gated by the flags derived in cmake/uScriptFeatures.cmake (see
# src/libs/drivers/CMakeLists.txt). Notably protocols/dds and protocols/grpc
# need CycloneDDS / Protobuf and are therefore skipped entirely unless a plugin
# that uses them is enabled.
uscript_add_subdirectory(USCRIPT_LIB_CANTP          cantp)

uscript_add_subdirectory(USCRIPT_LIB_PROTO_DDS      dds)
uscript_add_subdirectory(USCRIPT_LIB_PROTO_GRPC     grpc)
uscript_add_subdirectory(USCRIPT_LIB_PROTO_MODBUS   modbus)
uscript_add_subdirectory(USCRIPT_LIB_PROTO_MQTT     mqtt)
uscript_add_subdirectory(USCRIPT_LIB_PROTO_PROFIBUS profibus)
