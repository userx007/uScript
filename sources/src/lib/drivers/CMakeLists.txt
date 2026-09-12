cmake_minimum_required(VERSION 3.25)
add_subdirectory(common/kmpmatch)
add_subdirectory(ch341)
add_subdirectory(ch347)
add_subdirectory(cp2112)
add_subdirectory(ftdi2xx)
add_subdirectory(hydrabus)
add_subdirectory(uart)
add_subdirectory(digispark)
add_subdirectory(slcan)

# Linux-kernel-only drivers (linux/i2c-dev.h, linux/spi/spidev.h,
# linux/can.h+can/raw.h, linux/if_packet.h, systec_can.ko's sysfs layout) —
# each of these FATAL_ERRORs its own CMakeLists.txt on non-Linux platforms,
# so they're only added when building for Linux. See src/plugin/CMakeLists.txt's
# matching guard around ki2c_plugin/kspi_plugin/kvcan_plugin/raweth_plugin/
# systec_plugin for the full rationale — both guards must stay in sync.
if(UNIX AND NOT APPLE)
    add_subdirectory(kspi)
    add_subdirectory(ki2c)
    add_subdirectory(kvcan)
    add_subdirectory(raweth)
    add_subdirectory(systec)
endif()

add_subdirectory(candlelight)
add_subdirectory(pcan)
add_subdirectory(tcpip)
add_subdirectory(udp)
add_subdirectory(w5500)
add_subdirectory(enc28j60)
add_subdirectory(lan8720)
add_subdirectory(websocket)

# Windows- and Linux-only driver — Vector Informatik's XL Driver Library
# (vxlapi) ships for Windows (Vector's own SDK) and Linux (the unofficial
# libXlApi.so.26.20.14 port — see
# src/lib/drivers/vector/third_party/vxlapi/include/vxlapi_linux.h's own
# header comment), no macOS build. uVector's own CMakeLists.txt already
# FATAL_ERRORs on any other platform as a safety net, but (same rationale as
# the Linux-kernel-only guard above) that FATAL_ERROR would still abort the
# entire configure on an unsupported-platform build if reached — so it's
# gated here too. See src/plugin/CMakeLists.txt's matching guard around
# vector_plugin/vector_eth_plugin; both must stay in sync. Requires vendoring
# Vector's proprietary SDK / the Linux port yourself — see
# src/lib/drivers/vector/third_party/CMakeLists.txt's header comment for the
# expected layout; without it, the driver still builds against a vendored
# header but there is nothing to link against.
if(WIN32 OR LINUX)
    add_subdirectory(vector)
endif()

add_subdirectory(dds)
add_subdirectory(dds_typed)
add_subdirectory(grpc)
add_subdirectory(modbus)
add_subdirectory(mqtt)
add_subdirectory(profibus)

# Relay both vendor-DLL variables up one more level (to src/lib/'s scope) —
# ftdi2xx/'s and vector/'s own PARENT_SCOPE exports only reach here. See the
# top-level CMakeLists.txt's "Windows runtime DLL packaging" section for
# the full rationale and where this relay chain terminates.
if(DEFINED FTDI_RUNTIME_DLL)
    set(FTDI_RUNTIME_DLL "${FTDI_RUNTIME_DLL}" PARENT_SCOPE)
endif()
if(DEFINED VECTOR_RUNTIME_DLL)
    set(VECTOR_RUNTIME_DLL "${VECTOR_RUNTIME_DLL}" PARENT_SCOPE)
endif()