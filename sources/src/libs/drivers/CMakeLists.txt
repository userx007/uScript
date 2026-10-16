cmake_minimum_required(VERSION 3.25)

# Every directory below is configured only when something that is enabled needs
# it: USCRIPT_LIB_<KEY> is derived from the USCRIPT_PLUGIN_<NAME> options (and
# from the lib -> lib dependencies) in cmake/uScriptFeatures.cmake. That is also
# why a driver whose SDK is not installed (CycloneDDS, gRPC, ...) no longer
# breaks the configure of a build that doesn't use it.
#
# Platform restrictions (Linux-kernel-only drivers, Windows/Linux-only Vector)
# live in the same file, next to the plugins they belong to.

uscript_add_subdirectory(USCRIPT_LIB_KMPMATCH       common/kmpmatch)
uscript_add_subdirectory(USCRIPT_LIB_CANFRAME       common/canframe)
uscript_add_subdirectory(USCRIPT_LIB_CH341          ch341)
uscript_add_subdirectory(USCRIPT_LIB_CH347          ch347)
uscript_add_subdirectory(USCRIPT_LIB_CP2112         cp2112)
uscript_add_subdirectory(USCRIPT_LIB_FTDI_SDK       ftdi2xx)   # chip sub-dirs gated inside
uscript_add_subdirectory(USCRIPT_LIB_HYDRABUS       hydrabus)
uscript_add_subdirectory(USCRIPT_LIB_UART           uart)
uscript_add_subdirectory(USCRIPT_LIB_DIGISPARK      digispark) # i2c/spi gated inside
uscript_add_subdirectory(USCRIPT_LIB_SLCAN          slcan)

# Linux-kernel-only drivers
uscript_add_subdirectory(USCRIPT_LIB_KSPI           kspi)
uscript_add_subdirectory(USCRIPT_LIB_KI2C           ki2c)
uscript_add_subdirectory(USCRIPT_LIB_KCAN           kcan)
uscript_add_subdirectory(USCRIPT_LIB_KVCAN          kvcan)
uscript_add_subdirectory(USCRIPT_LIB_RAWETH         raweth)
uscript_add_subdirectory(USCRIPT_LIB_SYSTEC         systec)

uscript_add_subdirectory(USCRIPT_LIB_CANDLELIGHT    candlelight)
uscript_add_subdirectory(USCRIPT_LIB_PCAN           pcan)
uscript_add_subdirectory(USCRIPT_LIB_TCPIP          tcpip)
uscript_add_subdirectory(USCRIPT_LIB_UDP            udp)
uscript_add_subdirectory(USCRIPT_LIB_W5500          w5500)
uscript_add_subdirectory(USCRIPT_LIB_ENC28J60       enc28j60)
uscript_add_subdirectory(USCRIPT_LIB_LAN8720        lan8720)
uscript_add_subdirectory(USCRIPT_LIB_WEBSOCKET      websocket)

# Windows- and Linux-only (Vector XL Driver Library); needs the vendored SDK /
# Linux port, see vector/third_party/CMakeLists.txt's header comment.
uscript_add_subdirectory(USCRIPT_LIB_VECTOR         vector)

uscript_add_subdirectory(USCRIPT_LIB_DDS            dds)
uscript_add_subdirectory(USCRIPT_LIB_DDS_TYPED      dds_typed)
uscript_add_subdirectory(USCRIPT_LIB_GRPC           grpc)
uscript_add_subdirectory(USCRIPT_LIB_MODBUS         modbus)
uscript_add_subdirectory(USCRIPT_LIB_MQTT           mqtt)
uscript_add_subdirectory(USCRIPT_LIB_PROFIBUS       profibus)

# Relay both vendor-DLL variables up one more level (to src/lib/'s scope) —
# ftdi2xx/'s and vector/'s own PARENT_SCOPE exports only reach here. See the
# top-level CMakeLists.txt's "Windows runtime DLL packaging" section for
# the full rationale and where this relay chain terminates. Both are only
# defined when the corresponding driver was configured, hence the DEFINED
# guards (also required by anything further up that consumes them).
if(DEFINED FTDI_RUNTIME_DLL)
    set(FTDI_RUNTIME_DLL "${FTDI_RUNTIME_DLL}" PARENT_SCOPE)
endif()
if(DEFINED VECTOR_RUNTIME_DLL)
    set(VECTOR_RUNTIME_DLL "${VECTOR_RUNTIME_DLL}" PARENT_SCOPE)
endif()