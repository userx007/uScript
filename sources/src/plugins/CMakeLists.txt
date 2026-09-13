cmake_minimum_required(VERSION 3.25)

add_subdirectory(common/commandexec)
add_subdirectory(common/pluginsettings)
add_subdirectory(buspirate_plugin)
add_subdirectory(ch341_plugin)
add_subdirectory(ch347_plugin)
add_subdirectory(cp2112_plugin)
add_subdirectory(dspki2c_plugin)
add_subdirectory(dspkspi_plugin)
add_subdirectory(ftdi2232_plugin)
add_subdirectory(ftdi232h_plugin)
add_subdirectory(ftdi245_plugin)
add_subdirectory(ftdi4232_plugin)
add_subdirectory(hydrabus_plugin)
add_subdirectory(pcan_plugin)
add_subdirectory(shell_plugin)
add_subdirectory(slcan_plugin)
add_subdirectory(candlelight_plugin)
add_subdirectory(dds_plugin)
add_subdirectory(dds_typed_plugin)
add_subdirectory(uart_plugin)
add_subdirectory(uartmon_plugin)
add_subdirectory(udp_plugin)
add_subdirectory(tcpip_plugin)
add_subdirectory(w5500_plugin)
add_subdirectory(enc28J60_plugin)
add_subdirectory(lan8720_plugin)
add_subdirectory(mqtt_plugin)
add_subdirectory(websocket_plugin)
add_subdirectory(modbus_plugin)
add_subdirectory(profibus_plugin)
add_subdirectory(grpc_plugin)

# ─────────────────────────────────────────────────────────────────────────────
# Linux-kernel-only plugins.
#
# Each of these wraps a driver that intentionally FATAL_ERRORs its own
# CMakeLists.txt on non-Linux platforms, because it talks directly to a
# Linux-specific kernel ABI with no Windows equivalent to port to:
#   uKI2C    -> linux/i2c-dev.h            (ki2c_plugin)
#   uKSpi    -> linux/spi/spidev.h         (kspi_plugin)
#   uKVCan   -> linux/can.h, linux/can/raw.h (kvcan_plugin)
#   uRawEth  -> linux/if_packet.h (AF_PACKET) (raweth_plugin)
#   uSystecCan -> linux/can.h + systec_can.ko sysfs layout (systec_plugin)
#
# Left unconditional, add_subdirectory()'ing these on Windows aborts CMake
# configuration for the *entire* project the moment it reaches one of them
# (message(FATAL_ERROR ...) is fatal, not skippable) — so every other plugin
# below this point would never even get configured. Gating them here lets a
# Windows build configure and produce every plugin that has a real Windows
# backend; these five simply aren't part of that build.
# ─────────────────────────────────────────────────────────────────────────────
if(UNIX AND NOT APPLE)
    add_subdirectory(ki2c_plugin)
    add_subdirectory(kspi_plugin)
    add_subdirectory(kvcan_plugin)
    add_subdirectory(raweth_plugin)
    add_subdirectory(systec_plugin)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# vector_plugin (CAN/CAN-FD) and vector_eth_plugin (Ethernet) — both Windows
# and Linux. Both depend on uVector/uVectorEth, which depend on Vector
# Informatik's XL Driver Library (vxlapi): Vector's own SDK on Windows, the
# unofficial libXlApi.so.26.20.14/.26.30.16 port on Linux (see
# src/lib/drivers/vector/third_party/vxlapi/include/vxlapi_linux.h's own
# header comment) — no macOS build either way. Same rationale as the
# Linux-kernel-only guard above, mirrored: both plugins' own CMakeLists.txt
# FATAL_ERROR on any other platform, so they're gated here to avoid aborting
# the whole configure on an unsupported-platform build. See
# src/lib/drivers/CMakeLists.txt's matching guard around add_subdirectory(vector);
# both must stay in sync.
#
# vector_eth_plugin's Linux path uses an architecturally different XL-API
# subsystem than Windows (the Network/virtual-switch API, not the direct
# port-based one) — confirmed against real symbol-table dumps of two separate
# driver releases, not just header presence — but it's a real, working path
# either way. See uVectorEth.hpp's class comment for the full platform split.
# ─────────────────────────────────────────────────────────────────────────────
if(WIN32 OR LINUX)
    add_subdirectory(vector_plugin)
    add_subdirectory(vector_eth_plugin)
endif()


