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

# Windows-only driver — Vector Informatik's XL Driver Library (vxlapi) has no
# Linux/macOS build, unlike PCAN-Basic. uVector's own CMakeLists.txt already
# FATAL_ERRORs on non-Windows as a safety net, but (same rationale as the
# Linux-kernel-only guard above) that FATAL_ERROR would still abort the
# entire configure on a non-Windows build if reached — so it's gated here
# too. See src/plugin/CMakeLists.txt's matching guard around vector_plugin;
# both must stay in sync. Requires vendoring Vector's proprietary SDK
# yourself — see src/lib/drivers/vector/third_party/CMakeLists.txt's header
# comment for the expected layout; without it, the driver still builds
# against a bundled stub header but there is nothing to link against.
if(WIN32)
    add_subdirectory(vector)
endif()

add_subdirectory(dds)
add_subdirectory(dds_typed)
add_subdirectory(grpc)
add_subdirectory(modbus)
add_subdirectory(mqtt)
add_subdirectory(profibus)