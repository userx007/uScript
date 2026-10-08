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
set(INSTALL_BASE_TESTERS "testers")      # testers
set(INSTALL_BASE_IPLUGINS "iplugins")    # interactive shell plugins
set(INSTALL_BASE_SPLUGINS "splugins")    # script plugins
set(INSTALL_BASE_UPLUGINS "uplugins")    # user plugins (e.g. dds custom plugins, etc.)
set(INSTALL_BASE_EXTLIBS "extlibs")      # external or inter-component shared libraries

# Helper to build the full path based on OS
if(MSVC OR MSYS OR MINGW)
    set(INSTALL_OS_PREFIX "windows${FOLDER_SUFFIX}_${CMAKE_BUILD_TYPE}")
    set(LIB_INSTALL_TYPE RUNTIME)
else()
    set(INSTALL_OS_PREFIX "linux_${CMAKE_BUILD_TYPE}")
    set(LIB_INSTALL_TYPE LIBRARY)
endif()

set(INSTALL_APP_DIR               "bin/${INSTALL_OS_PREFIX}")
set(INSTALL_IPLUGIN_DIR           "bin/${INSTALL_OS_PREFIX}/${INSTALL_BASE_IPLUGINS}")
set(INSTALL_SPLUGIN_DIR           "bin/${INSTALL_OS_PREFIX}/${INSTALL_BASE_SPLUGINS}")
set(INSTALL_UPLUGIN_DIR           "bin/${INSTALL_OS_PREFIX}/${INSTALL_BASE_UPLUGINS}")
set(INSTALL_EXTLIBS_DIR           "bin/${INSTALL_OS_PREFIX}/${INSTALL_BASE_EXTLIBS}")
set(INSTALL_TESTERS_DIR           "bin/${INSTALL_OS_PREFIX}/${INSTALL_BASE_TESTERS}/bin")
set(INSTALL_TESTERS_SCRIPTS_DIR   "bin/${INSTALL_OS_PREFIX}/${INSTALL_BASE_TESTERS}/scripts")
set(INSTALL_TESTERS_KSCRIPTS_DIR  "bin/${INSTALL_OS_PREFIX}/${INSTALL_BASE_TESTERS}/kscripts")

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

if(UNIX AND NOT APPLE)
  set(CMAKE_INSTALL_RPATH "$ORIGIN/${INSTALL_BASE_EXTLIBS}")
  set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
endif()


# Add subdirectories
add_subdirectory(sources)

# --- 1. Applications ---
install(TARGETS uscript ScriptFrontend DESTINATION ${INSTALL_APP_DIR})


if(UNIX AND NOT APPLE)
    set_target_properties(uscript PROPERTIES
        BUILD_RPATH ""
        SKIP_BUILD_RPATH TRUE
    )
endif()


# --- 1b. External runtime libraries ---
set(EXTLIB_TARGETS
    uVolatileMacroStore
)

install(TARGETS ${EXTLIB_TARGETS} ${LIB_INSTALL_TYPE} DESTINATION ${INSTALL_EXTLIBS_DIR})

# --- 1c. Windows runtime DLL packaging ---

if(WIN32)
    if(DEFINED FTDI_RUNTIME_DLL)
        install(FILES "${FTDI_RUNTIME_DLL}" DESTINATION ${INSTALL_APP_DIR})
    endif()
    if(DEFINED VECTOR_RUNTIME_DLL)
        install(FILES "${VECTOR_RUNTIME_DLL}" DESTINATION ${INSTALL_APP_DIR})
    endif()
endif()

# --- 2. SPlugins ---
set(SPLUGIN_TARGETS_COMMON
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
    lan8720net_plugin
    mqtt_plugin
    pcan_plugin
    shell_plugin
    slcan_plugin
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
    dds_plugin
    ddsty_plugin
    vector_plugin
    vector_eth_plugin
)

set(SPLUGIN_TARGETS_LINUX_ONLY
    kcan_plugin
    ki2c_plugin
    kspi_plugin
    kvcan_plugin
    raweth_plugin
    systec_plugin
)


set(SPLUGIN_TARGETS ${SPLUGIN_TARGETS_COMMON})
if(UNIX AND NOT APPLE)
    list(APPEND SPLUGIN_TARGETS ${SPLUGIN_TARGETS_LINUX_ONLY})
elseif(WIN32)
    list(APPEND SPLUGIN_TARGETS ${SPLUGIN_TARGETS_WINDOWS_ONLY})
endif()

install(TARGETS ${SPLUGIN_TARGETS} ${LIB_INSTALL_TYPE} DESTINATION ${INSTALL_SPLUGIN_DIR})

# --- 3. IPlugins ---
set(IPLUGIN_TARGETS
    test_iplugin
)

install(TARGETS ${IPLUGIN_TARGETS} ${LIB_INSTALL_TYPE} DESTINATION ${INSTALL_IPLUGIN_DIR})

# --- 4. Testers (Executables) ---
set(TESTER_TARGETS_COMMON
    cantp_loopback
    tcpip_loopback_server
    udp_loopback_server
    uart_loopback
    cross_loopback
    modbus_server
)

set(TESTER_TARGETS_LINUX_ONLY
    eth_raw_loopback_server
    kvcan_loopback
)

set(TESTER_TARGETS ${TESTER_TARGETS_COMMON})
if(UNIX AND NOT APPLE)
    list(APPEND TESTER_TARGETS ${TESTER_TARGETS_LINUX_ONLY})
endif()

install(TARGETS ${TESTER_TARGETS} DESTINATION ${INSTALL_TESTERS_DIR})

# --- 5. Testers' Scripts ---
install(
    DIRECTORY "${CMAKE_SOURCE_DIR}/sources/src/testers/scripts/"
    DESTINATION "${INSTALL_TESTERS_SCRIPTS_DIR}"
    FILE_PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
    FILES_MATCHING
        PATTERN "*.sh"
)

install(
    DIRECTORY "${CMAKE_SOURCE_DIR}/sources/src/testers/kscripts/"
    DESTINATION "${INSTALL_TESTERS_KSCRIPTS_DIR}"
    FILE_PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
    FILES_MATCHING
        PATTERN "*.sh"
)

# --- 6. User plugins ----

set(UPLUGIN_TARGETS
    SimpleTypes
)

install(TARGETS ${UPLUGIN_TARGETS} ${LIB_INSTALL_TYPE} DESTINATION ${INSTALL_UPLUGIN_DIR})

# ---------------------------------------------------------------------------
# Install Vector XL API Linux Libraries
# ---------------------------------------------------------------------------

set(XLAPI_LINUX_SOURCE_DIR "${CMAKE_SOURCE_DIR}/sources/src/libs/drivers/vector/third_party/vxlapi/linux")

set(XLAPI_LINUX_FILES
    "${XLAPI_LINUX_SOURCE_DIR}/libXlApi.so"
    "${XLAPI_LINUX_SOURCE_DIR}/libXlApi.so.26"
    "${XLAPI_LINUX_SOURCE_DIR}/libXlApi.so.26.30.16"
)

file(GLOB EXISTS_FILES ${XLAPI_LINUX_FILES})
if(EXISTS_FILES)
    install(
        FILES ${XLAPI_LINUX_FILES}
        DESTINATION ${INSTALL_EXTLIBS_DIR}
        PERMISSIONS
            OWNER_READ OWNER_WRITE OWNER_EXECUTE
            GROUP_READ GROUP_EXECUTE
            WORLD_READ
    )
else()
    message(WARNING "Vector XL API Linux libraries not found in ${XLAPI_LINUX_SOURCE_DIR}. Install step will skip them.")
endif()
