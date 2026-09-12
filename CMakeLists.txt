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

# --- RPATH ---
# Must match INSTALL_EXTLIBS_DIR below - both uscript/ScriptFrontend and
# extlibs/ live directly under bin/<os>/, so "$ORIGIN/extlibs" (the
# executable's own directory, a linker/loader token resolved by ld.so, NOT a
# CMake variable) always finds it regardless of the process's working
# directory or the exact value of CMAKE_INSTALL_PREFIX.
#
# IMPORTANT: do NOT build this from "${CMAKE_INSTALL_PREFIX}/..." instead -
# CMAKE_INSTALL_PREFIX is set to the literal relative string "../" a few
# lines below, and a RUNPATH entry that doesn't start with $ORIGIN is
# resolved by ld.so relative to the CURRENT WORKING DIRECTORY of the
# process at launch, not relative to the binary's own location. That
# previously baked in "..//bin/<os>/extlibs" verbatim, which only happened
# to work when uscript was launched from one specific CWD.
if(UNIX AND NOT APPLE)
  set(CMAKE_INSTALL_RPATH "$ORIGIN/${INSTALL_BASE_EXTLIBS}")
endif()

# Add subdirectories
add_subdirectory(sources)
add_subdirectory(testers)

# --- 1. Applications ---
install(TARGETS uscript ScriptFrontend DESTINATION ${INSTALL_APP_DIR})

# --- 1b. External runtime libraries ---
# These are regular, non-dlopen()'d SHARED dependencies of the apps
# themselves (e.g. uVolatileMacroStore, a link-time dependency of uscript
# via uUtils) - unlike splugins/iplugins, which get loaded on demand by
# an explicit configured path, the dynamic loader must resolve these
# automatically at process startup. CMAKE_INSTALL_RPATH above already
# points every installed executable at "${CMAKE_INSTALL_PREFIX}/${INSTALL_EXTLIBS_DIR}"
# (i.e. bin/<os>/extlibs, next to uscript/ScriptFrontend themselves, same
# level as splugins/iplugins) on Unix, so this is where any such library
# belongs. Add future non-plugin SHARED libs here as they show up.
#
# NOTE (Windows): CMAKE_INSTALL_RPATH only applies on Unix (see the
# "if(UNIX AND NOT APPLE)" guard above) - Windows has no rpath equivalent,
# and its DLL search order only checks the executable's own directory (or
# PATH) automatically. A DLL installed only to extlibs/ will NOT be found
# by uscript.exe there as-is; on Windows this either needs extlibs/ added
# to PATH at launch, or the DLL copied/installed alongside the app instead.
set(EXTLIB_TARGETS
    uVolatileMacroStore
)

install(TARGETS ${EXTLIB_TARGETS} ${LIB_INSTALL_TYPE} DESTINATION ${INSTALL_EXTLIBS_DIR})

# --- 1c. Windows runtime DLL packaging ---
#
# Follows through on the NOTE (Windows) comment directly above: a DLL
# installed only to extlibs/ is invisible to uscript.exe's default search
# order, so vendored proprietary SDK runtime DLLs (FTD2XX, Vector's vxlapi)
# need installing next to the app itself instead.
#
# FTDI_RUNTIME_DLL / VECTOR_RUNTIME_DLL originate deep in the tree
# (src/lib/drivers/ftdi2xx/third_party/CMakeLists.txt and
# src/lib/drivers/vector/third_party/CMakeLists.txt respectively) and are
# only ever set via PARENT_SCOPE, which only reaches the *immediate*
# parent directory's scope — reaching this file requires an explicit
# relay at every intermediate add_subdirectory() level in between
# (ftdi2xx/, drivers/, lib/, src/, sources/), each one re-exporting the
# variable with its own PARENT_SCOPE. That full chain has been added
# alongside this section; without it, both variables would silently be
# empty/undefined here even on a real vendored-SDK Windows build, and
# this whole section would be dead code.
#
# Both are conditional on WIN32 *and* being defined at all: they're only
# set in the first place when a real vendored SDK was found (see each
# third_party/CMakeLists.txt's own EXISTS check) — building against the
# bundled compatibility stub headers (no real SDK dropped in) leaves them
# unset, same as building on Linux where these drivers aren't even
# add_subdirectory()'d.
#
# PCANBasic.dll and CH347DLLA64.dll are NOT included here: uPcan's and
# uCH347's own CMakeLists.txt already copy their DLL next to that
# target's own build output via a POST_BUILD step, and neither exports a
# PARENT_SCOPE variable equivalent to FTDI_RUNTIME_DLL/VECTOR_RUNTIME_DLL
# for this file to install from — that convenience copy is not a
# substitute for a real install() of the final packaged binary tree.
# Extend those two third_party/CMakeLists.txt files with the same
# PARENT_SCOPE relay pattern used for FTDI/Vector if you need them here too.
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
    DIRECTORY "${CMAKE_SOURCE_DIR}/testers/scripts/"
    DESTINATION "${INSTALL_TESTERS_SCRIPTS_DIR}"
    FILE_PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
    FILES_MATCHING
        PATTERN "*.sh"
)

install(
    DIRECTORY "${CMAKE_SOURCE_DIR}/testers/kscripts/"
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
    customer1_types
)

install(TARGETS ${UPLUGIN_TARGETS} ${LIB_INSTALL_TYPE} DESTINATION ${INSTALL_UPLUGIN_DIR})
