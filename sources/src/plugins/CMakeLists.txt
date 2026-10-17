cmake_minimum_required(VERSION 3.25)

# Shared by every plugin (always built).
add_subdirectory(common/commandexec)
add_subdirectory(common/pluginsettings)

# Each plugin is built only if its USCRIPT_PLUGIN_<NAME> option is ON; the
# drivers / protocols it needs are then configured automatically (see
# cmake/uScriptFeatures.cmake). Platform restrictions are handled there too:
# on a platform that can't build a plugin its flag is forced OFF, so no
# #if(UNIX AND NOT APPLE) / #if(WIN32 OR LINUX) guard is needed here.
uscript_add_subdirectory(USCRIPT_PLUGIN_BUSPIRATE   buspirate_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_CH341       ch341_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_CH347       ch347_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_CP2112      cp2112_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_DSPKI2C     dspki2c_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_DSPKSPI     dspkspi_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_FTDI2232    ftdi2232_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_FTDI232H    ftdi232h_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_FTDI245     ftdi245_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_FTDI4232    ftdi4232_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_HYDRABUS    hydrabus_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_PCAN        pcan_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_SHELL       shell_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_SLCAN       slcan_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_CANDLELIGHT candlelight_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_DDS         dds_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_DDS_TYPED   dds_typed_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_UART        uart_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_UARTMON     uartmon_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_UDP         udp_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_TCPIP       tcpip_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_W5500       w5500_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_ENC28J60    enc28J60_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_LAN8720     lan8720_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_MQTT        mqtt_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_WEBSOCKET   websocket_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_MODBUS      modbus_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_PROFIBUS    profibus_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_GRPC        grpc_plugin)

# Linux-kernel-only (forced OFF elsewhere)
uscript_add_subdirectory(USCRIPT_PLUGIN_KI2C        ki2c_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_KSPI        kspi_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_KCAN        kcan_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_RAWETH      raweth_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_SYSTEC      systec_plugin)

# Windows / Linux only (forced OFF elsewhere)
uscript_add_subdirectory(USCRIPT_PLUGIN_VECTOR      vector_plugin)
uscript_add_subdirectory(USCRIPT_PLUGIN_VECTOR_ETH  vector_eth_plugin)
