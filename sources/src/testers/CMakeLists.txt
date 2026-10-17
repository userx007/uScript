cmake_minimum_required(VERSION 3.25)

# Helper executables for the plugins' loopback tests. Switch the whole group
# off with -DUSCRIPT_BUILD_TESTERS=OFF; individual testers are additionally
# skipped when the plugin / library they exercise is not built.
if(USCRIPT_BUILD_TESTERS)
    # links uTpFactory (the CAN transport stack), so it needs a CAN plugin
    uscript_add_subdirectory(USCRIPT_LIB_CANTP        cantploopback)
    add_subdirectory(crossloopback)
    add_subdirectory(simpleloopback)
    add_subdirectory(multicast)
    # kcan_link: command line front-end for the KCAN link layer (bitrate / up / down / status)
    uscript_add_subdirectory(USCRIPT_PLUGIN_KCAN      kcan_link)
    uscript_add_subdirectory(USCRIPT_PLUGIN_MODBUS    modbus_server)
endif()
