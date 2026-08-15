cmake_minimum_required(VERSION 3.25)

project(uScript)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set Microsoft Visual Studio specific macros
if(MSVC)
  set(BUILD_LOCAL "ON")
  set(FOLDER_SUFFIX "_vs")
  set(CMAKE_SHARED_LIBRARY_PREFIX "lib")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
else()
  set(FOLDER_SUFFIX "")
endif()

# --- Installation Directories ---
# We define base paths and append the OS/Type specific suffixes later if needed
set(INSTALL_BASE_TESTERS "testers")
set(INSTALL_BASE_PLUGINS "splugins")
set(INSTALL_BASE_IPLUGINS "iplugins")

# Helper to build the full path based on OS
if(MSVC OR MSYS OR MINGW)
    set(INSTALL_OS_PREFIX "windows${FOLDER_SUFFIX}_${CMAKE_BUILD_TYPE}")
    set(LIB_INSTALL_TYPE RUNTIME)
else()
    set(INSTALL_OS_PREFIX "linux_${CMAKE_BUILD_TYPE}")
    set(LIB_INSTALL_TYPE LIBRARY)
endif()

set(INSTALL_APP_DIR               "bin/${INSTALL_OS_PREFIX}")
set(INSTALL_PLUGIN_DIR            "bin/${INSTALL_OS_PREFIX}/${INSTALL_BASE_PLUGINS}")
set(INSTALL_IPLUGIN_DIR           "bin/${INSTALL_OS_PREFIX}/${INSTALL_BASE_IPLUGINS}")
set(INSTALL_TESTERS_DIR           "bin/${INSTALL_OS_PREFIX}/${INSTALL_BASE_TESTERS}/bin")
set(INSTALL_TESTERS_SCRIPTS_DIR   "bin/${INSTALL_OS_PREFIX}/${INSTALL_BASE_TESTERS}/scripts")

# Set default install prefix if not provided
if( CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT )
  message(STATUS "Setting default CMAKE_INSTALL_PREFIX path to ../")
  set(CMAKE_INSTALL_PREFIX "../" CACHE STRING "The path to use for make install" FORCE)
endif()

# --- Compiler Flags ---
if(MINGW)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -static ")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static ")

  if( (CMAKE_BUILD_TYPE STREQUAL "Release") OR (CMAKE_BUILD_TYPE STREQUAL "MinSizeRel") )
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s -Os")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s -Os")
  endif()
endif()

# --- RPATH ---
if(UNIX AND NOT APPLE)
  set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/extlibs")
endif()

# Add subdirectories
add_subdirectory(sources)
add_subdirectory(testers)

# --- 1. Applications ---
install(TARGETS uscript ScriptFrontend DESTINATION ${INSTALL_APP_DIR})

# --- 2. SPlugins ---
# List all plugins here. This is much cleaner than individual install commands.
set(PLUGIN_TARGETS
    buspirate_plugin
    ch341_plugin
    ch347_plugin
    cp2112_plugin
    dspki2c_plugin
    dspkspi_plugin
    enc28j60net_plugin
    ft2232_plugin
    ft232h_plugin
    ft245_plugin
    ft4232_plugin
    hydrabus_plugin
    ki2c_plugin
    kspi_plugin
    kvcan_plugin
    lan8720net_plugin
    mqtt_plugin
    pcan_plugin
    raweth_plugin
    shell_plugin
    tcpip_plugin
    uart_plugin
    uartmon_plugin
    udp_plugin
    w5500net_plugin
    websocket_plugin
    modbus_plugin
    profibus_plugin
    grpc_plugin
    candlelight_plugin
)

install(TARGETS ${PLUGIN_TARGETS} ${LIB_INSTALL_TYPE} DESTINATION ${INSTALL_PLUGIN_DIR})

# --- 3. IPlugins ---
install(TARGETS test_iplugin ${LIB_INSTALL_TYPE} DESTINATION ${INSTALL_IPLUGIN_DIR})

# --- 4. Testers (Executables) ---
set(TESTER_TARGETS
    cantp_loopback
    eth_raw_loopback_server
    tcpip_loopback_server
    udp_loopback_server
    kvcan_loopback
    uart_loopback
)

install(TARGETS ${TESTER_TARGETS} DESTINATION ${INSTALL_TESTERS_DIR})

# --- 5. Testers' Scripts ---
# Define the permission macro once
macro(install_scripts)
    foreach(_script ${ARGN})
        install(FILES ${_script}
                DESTINATION ${INSTALL_TESTERS_SCRIPTS_DIR}
                PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_READ WORLD_READ WORLD_EXECUTE)
    endforeach()
endmacro()

# List scripts explicitly
install_scripts(
    testers/scripts/can_single_frame_kvcan_run.sh
    testers/scripts/cantp_canopen_kvcan_run.sh
    testers/scripts/cantp_isotp_kvcan_run.sh
    testers/scripts/cantp_j1939_bam_kvcan_run.sh
    testers/scripts/cantp_j1939_rtscts_kvcan_run.sh
    testers/scripts/cantp_nmea2000_kvcan_run.sh
    testers/scripts/konsole_all.sh
    testers/scripts/raweth_run.sh
    testers/scripts/tcpip_run.sh
    testers/scripts/uart_run.sh
    testers/scripts/udp_run.sh
)

