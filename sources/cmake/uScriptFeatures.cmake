# =============================================================================
# uScriptFeatures.cmake - single source of truth for "what gets built"
# =============================================================================
#
# WHAT THIS GIVES YOU
# -------------------
#   * One cache option per plugin:   USCRIPT_PLUGIN_<NAME>   (ON/OFF)
#   * Everything below the plugins (drivers, protocols, the CAN-TP stack, the
#     FTDI SDK, the DDS type-ABI header target, loopback testers, ...) is
#     switched on or off AUTOMATICALLY from those options. Nobody sets a
#     driver flag by hand, and a driver can never be "on" for a plugin that
#     is off, nor "off" for a plugin that is on.
#
#   Because the plugin options are CACHE variables and the derived library
#   flags are CACHE INTERNAL, they are visible in every directory of the
#   project - no PARENT_SCOPE relaying is needed, at any depth.
#
# HOW TO USE IT
# -------------
#   Everything except DDS and gRPC:
#       cmake -DUSCRIPT_PLUGIN_DDS=OFF -DUSCRIPT_PLUGIN_DDS_TYPED=OFF \
#             -DUSCRIPT_PLUGIN_GRPC=OFF ...
#
#   Opt-in instead (nothing unless asked for):
#       cmake -DUSCRIPT_PLUGINS_DEFAULT=OFF -DUSCRIPT_PLUGIN_UART=ON \
#             -DUSCRIPT_PLUGIN_MQTT=ON ...
#     (USCRIPT_PLUGINS_DEFAULT only supplies the *initial* value of options
#      that are not in the cache yet - use a fresh build directory, or set the
#      individual options, when changing it.)
#
#   From a CMakeLists.txt, before this module is included (a normal variable
#   wins over the cache entry, CMP0077):
#       set(USCRIPT_PLUGIN_DDS OFF)
#
#   Pick the set once per build tree with a CMake preset / cache script.
#
# ADDING OR CHANGING A PLUGIN / DRIVER
# ------------------------------------
#   1. New driver or protocol directory  -> add a uscript_lib(...) line below
#      (its DEPENDS = the *other* lib keys its CMakeLists links; its TARGETS =
#      the target names it defines) and gate its add_subdirectory() with
#      uscript_add_subdirectory(USCRIPT_LIB_<KEY> <dir>).
#   2. New plugin                        -> add a uscript_plugin(...) line
#      (LIBS = the lib keys its CMakeLists links) and gate its
#      add_subdirectory() with uscript_add_subdirectory(USCRIPT_PLUGIN_<NAME> <dir>).
#   3. uscript_verify_link_graph() (called at the end of the top-level
#      CMakeLists.txt) fails the configure with a precise message if a table
#      entry below is missing/out of date, instead of letting it turn into an
#      obscure "-luFoo not found" at link time.
# =============================================================================

include_guard(GLOBAL)

# -----------------------------------------------------------------------------
# User-facing switches
# -----------------------------------------------------------------------------
option(USCRIPT_PLUGINS_DEFAULT
       "Initial value of every USCRIPT_PLUGIN_<NAME> option (ON = build all, OFF = opt-in)" ON)
option(USCRIPT_BUILD_TESTERS
       "Build the loopback / test-server helper executables (only those matching an enabled plugin)" ON)

set_property(GLOBAL PROPERTY USCRIPT_PLUGIN_NAMES "")
set_property(GLOBAL PROPERTY USCRIPT_LIB_KEYS     "")
set_property(GLOBAL PROPERTY USCRIPT_LIB_TARGETS  "")
set_property(GLOBAL PROPERTY USCRIPT_PLUGINS_UNSUPPORTED "")

# -----------------------------------------------------------------------------
# uscript_lib(<KEY> [DEPENDS <KEY>...] [TARGETS <cmake-target>...])
#
#   Declares a driver / protocol / support library group. <KEY> is what the
#   USCRIPT_LIB_<KEY> flag is named after. DEPENDS are other lib keys that
#   must be built whenever this one is (transitively resolved).
#   TARGETS are only used by uscript_verify_link_graph().
# -----------------------------------------------------------------------------
function(uscript_lib KEY)
    cmake_parse_arguments(L "" "" "DEPENDS;TARGETS" ${ARGN})
    set_property(GLOBAL APPEND PROPERTY USCRIPT_LIB_KEYS ${KEY})
    set_property(GLOBAL PROPERTY USCRIPT_LIB_${KEY}_DEPENDS "${L_DEPENDS}")
    foreach(_t IN LISTS L_TARGETS)
        set_property(GLOBAL PROPERTY USCRIPT_TARGET_OWNER_${_t} ${KEY})
        set_property(GLOBAL APPEND  PROPERTY USCRIPT_LIB_TARGETS ${_t})
    endforeach()
endfunction()

# -----------------------------------------------------------------------------
# uscript_plugin(<NAME> [LIBS <KEY>...] [PLATFORM <if()-condition...>])
#
#   Declares a plugin and creates its USCRIPT_PLUGIN_<NAME> option.
#   LIBS     : lib keys the plugin's CMakeLists links (directly).
#   PLATFORM : an if() condition (e.g. UNIX AND NOT APPLE). When it is false
#              the plugin is forced OFF and the option is hidden - this is the
#              one place the platform restrictions live.
# -----------------------------------------------------------------------------
function(uscript_plugin NAME)
    cmake_parse_arguments(P "" "" "LIBS;PLATFORM" ${ARGN})
    set_property(GLOBAL APPEND PROPERTY USCRIPT_PLUGIN_NAMES ${NAME})
    set_property(GLOBAL PROPERTY USCRIPT_PLUGIN_${NAME}_LIBS "${P_LIBS}")

    set(_supported TRUE)
    if(P_PLATFORM)
        if(${P_PLATFORM})
            set(_supported TRUE)
        else()
            set(_supported FALSE)
        endif()
    endif()

    if(_supported)
        option(USCRIPT_PLUGIN_${NAME} "Build the ${NAME} plugin (and the drivers it needs)"
               ${USCRIPT_PLUGINS_DEFAULT})
    else()
        if(DEFINED USCRIPT_PLUGIN_${NAME} AND USCRIPT_PLUGIN_${NAME})
            message(WARNING "uScript: plugin ${NAME} was requested but is not "
                            "supported on this platform (needs: ${P_PLATFORM}); ignoring.")
        endif()
        # CACHE INTERNAL: global + hidden in the GUI. The normal variable too,
        # so it also overrides a normal variable a parent scope may have set.
        set(USCRIPT_PLUGIN_${NAME} OFF CACHE INTERNAL "Not supported on this platform")
        set(USCRIPT_PLUGIN_${NAME} OFF PARENT_SCOPE)
        set_property(GLOBAL APPEND PROPERTY USCRIPT_PLUGINS_UNSUPPORTED ${NAME})
    endif()
endfunction()

# -----------------------------------------------------------------------------
# uscript_add_subdirectory(<FLAG-VARIABLE> <dir>)
#
#   add_subdirectory(<dir>) only when <FLAG-VARIABLE> is true. A MACRO (not a
#   function) on purpose: add_subdirectory() inside a function would run in a
#   child scope of the function, and the set(... PARENT_SCOPE) relays that
#   some drivers use (FTDI_RUNTIME_DLL, VECTOR_RUNTIME_DLL) would land in that
#   throw-away scope instead of the real parent directory.
# -----------------------------------------------------------------------------
macro(uscript_add_subdirectory FLAG DIR)
    if(${FLAG})
        add_subdirectory(${DIR})
    endif()
endmacro()

# =============================================================================
# LIBRARY GROUPS (drivers, protocols, support) - keep in step with the
# target_link_libraries() of each directory. Verified by
# uscript_verify_link_graph().
# =============================================================================

# ---- shared building blocks --------------------------------------------------
uscript_lib(KMPMATCH        TARGETS uKmpMatch)
uscript_lib(UART            TARGETS uUart              DEPENDS KMPMATCH)
uscript_lib(TCPIP           TARGETS uTcpip             DEPENDS KMPMATCH)
uscript_lib(UDP             TARGETS uUdp               DEPENDS KMPMATCH)
uscript_lib(WEBSOCKET       TARGETS uWebSocket         DEPENDS KMPMATCH TCPIP)

# ---- CAN transport stack (protocols/cantp/*) - used by the CAN plugins/drivers
uscript_lib(CANTP           TARGETS uTpConfig uITpInterface uIsoTp uJ1939Tp
                                    uNmea2000Fp uCanOpenSdo uTpFactory)

# ---- serial / USB bridges -----------------------------------------------------
uscript_lib(CH341           TARGETS uCH341             DEPENDS KMPMATCH)
uscript_lib(CH347           TARGETS uCH347 ch347)
uscript_lib(CP2112          TARGETS cp2112)
uscript_lib(HYDRABUS        TARGETS HydraHAL)
uscript_lib(DIGISPARK)                                  # directory grouping only
uscript_lib(DIGISPARK_I2C   TARGETS uDigisparkI2C      DEPENDS DIGISPARK)
uscript_lib(DIGISPARK_SPI   TARGETS uDigisparkSPI      DEPENDS DIGISPARK)

# ---- FTDI (ftdi2xx/ holds the shared SDK; each chip family is its own lib) ---
uscript_lib(FTDI_SDK)                                   # ftdi2xx/ + third_party/
uscript_lib(FTDI232         TARGETS ft232h  ft232h_base   DEPENDS FTDI_SDK)
uscript_lib(FTDI2232        TARGETS ft2232  ft2232_base   DEPENDS FTDI_SDK)
uscript_lib(FTDI245         TARGETS ft245   ft245_base    DEPENDS FTDI_SDK)
uscript_lib(FTDI4232        TARGETS ft4232  ft4232_base   DEPENDS FTDI_SDK)

# ---- CAN adapters --------------------------------------------------------------
#   CANFRAME : header-only uCanFrame.hpp (drivers/common/canframe) shared by the
#              slcan / candlelight drivers and their plugins.
uscript_lib(CANFRAME        TARGETS uCanFrame)
uscript_lib(SLCAN           TARGETS uSlcan             DEPENDS UART CANFRAME)
uscript_lib(CANDLELIGHT     TARGETS uCandlelight       DEPENDS SLCAN CANFRAME)
uscript_lib(PCAN            TARGETS uPcan              DEPENDS CANTP)
uscript_lib(VECTOR          TARGETS uVector            DEPENDS CANTP)

# ---- Linux-kernel-only drivers (see the PLATFORM notes in the plugin table) --
uscript_lib(KI2C            TARGETS uKI2C              DEPENDS KMPMATCH)
uscript_lib(KSPI            TARGETS uKSpi              DEPENDS KMPMATCH)
uscript_lib(KCAN            TARGETS uKCan              DEPENDS KMPMATCH)
uscript_lib(KVCAN           TARGETS uKVCan             DEPENDS KMPMATCH)
uscript_lib(RAWETH          TARGETS uRawEth            DEPENDS KMPMATCH)
uscript_lib(SYSTEC          TARGETS uSystecCan         DEPENDS KMPMATCH)

# ---- Ethernet MAC/PHY chips ----------------------------------------------------
uscript_lib(W5500           TARGETS uW5500Net)
uscript_lib(ENC28J60        TARGETS uEnc28J60Net)
uscript_lib(LAN8720         TARGETS uLan8720Net)

# ---- Protocol-over-TCP / serial: driver + its protocol library ----------------
uscript_lib(PROTO_MODBUS    TARGETS uModbusProtocol)
uscript_lib(MODBUS          TARGETS uModbusDriver      DEPENDS TCPIP PROTO_MODBUS)
uscript_lib(PROTO_MQTT      TARGETS uMqttProtocol)
uscript_lib(MQTT            TARGETS uMqttDriver        DEPENDS TCPIP PROTO_MQTT)
uscript_lib(PROTO_PROFIBUS  TARGETS uProfibusProtocol)
uscript_lib(PROFIBUS        TARGETS uProfibusDriver    DEPENDS UART PROTO_PROFIBUS)

# ---- Middleware with a heavy external SDK (the reason this module exists) ----
#   DDS_ABI   : the uIDdsTypePluginAbi header target in include/driver, which
#               is the part that calls find_package(CycloneDDS).
uscript_lib(PROTO_DDS       TARGETS uDdsProtocol)
uscript_lib(DDS             TARGETS uDdsDriver         DEPENDS PROTO_DDS)
uscript_lib(DDS_ABI         TARGETS uIDdsTypePluginAbi)
uscript_lib(DDS_TYPED       TARGETS uDdsTypedDriver    DEPENDS DDS_ABI)
uscript_lib(PROTO_GRPC      TARGETS uGrpcProtocol)
uscript_lib(GRPC            TARGETS uGrpcDriver        DEPENDS PROTO_GRPC)

# =============================================================================
# PLUGINS
# =============================================================================
#
# PLATFORM notes (moved here from src/plugins/CMakeLists.txt and
# src/libs/drivers/CMakeLists.txt, where the same guard used to be written
# twice and had to be kept in sync by hand):
#
#   Linux-kernel-only: each of these wraps a driver that FATAL_ERRORs on a
#   non-Linux platform because it talks to a Linux-specific kernel ABI with no
#   Windows equivalent:
#       KI2C    -> linux/i2c-dev.h                 KSPI   -> linux/spi/spidev.h
#       KVCAN   -> linux/can.h, linux/can/raw.h    RAWETH -> linux/if_packet.h
#       SYSTEC  -> linux/can.h + systec_can.ko sysfs layout
#   (message(FATAL_ERROR) is not skippable, so an unguarded add_subdirectory()
#   would abort the configure of the whole project on Windows.)
#
#   VECTOR / VECTOR_ETH: both depend on Vector Informatik's XL Driver Library
#   (vxlapi) - Vector's own SDK on Windows, the unofficial libXlApi.so port on
#   Linux (see drivers/vector/third_party/vxlapi/include/vxlapi_linux.h) - so
#   Windows and Linux only. VECTOR_ETH's Linux path uses the XL-API Network /
#   virtual-switch subsystem rather than the port-based one (see
#   uVectorEth.hpp's class comment).
# -----------------------------------------------------------------------------

set(_USCRIPT_LINUX_ONLY UNIX AND NOT APPLE)
set(_USCRIPT_WIN_OR_LINUX WIN32 OR LINUX)

uscript_plugin(BUSPIRATE    LIBS UART)
uscript_plugin(CH341        LIBS CH341)
uscript_plugin(CH347        LIBS CH347)
uscript_plugin(CP2112       LIBS CP2112)
uscript_plugin(DSPKI2C      LIBS DIGISPARK_I2C)
uscript_plugin(DSPKSPI      LIBS DIGISPARK_SPI)
uscript_plugin(FTDI2232     LIBS FTDI2232)
uscript_plugin(FTDI232H     LIBS FTDI232)
uscript_plugin(FTDI245      LIBS FTDI245)
uscript_plugin(FTDI4232     LIBS FTDI4232)
uscript_plugin(HYDRABUS     LIBS HYDRABUS UART)
uscript_plugin(PCAN         LIBS PCAN)
uscript_plugin(SHELL)
uscript_plugin(SLCAN        LIBS SLCAN CANFRAME CANTP)
uscript_plugin(CANDLELIGHT  LIBS CANDLELIGHT CANFRAME CANTP)
uscript_plugin(DDS          LIBS DDS)
uscript_plugin(DDS_TYPED    LIBS DDS_TYPED)
uscript_plugin(UART         LIBS UART)
uscript_plugin(UARTMON)
uscript_plugin(UDP          LIBS UDP)
uscript_plugin(TCPIP        LIBS TCPIP)
uscript_plugin(W5500        LIBS W5500)
uscript_plugin(ENC28J60     LIBS ENC28J60)
uscript_plugin(LAN8720      LIBS LAN8720)
uscript_plugin(MQTT         LIBS MQTT)
uscript_plugin(WEBSOCKET    LIBS WEBSOCKET)
uscript_plugin(MODBUS       LIBS MODBUS)
uscript_plugin(PROFIBUS     LIBS PROFIBUS)
uscript_plugin(GRPC         LIBS GRPC)

uscript_plugin(KI2C         LIBS KI2C   PLATFORM ${_USCRIPT_LINUX_ONLY})
uscript_plugin(KSPI         LIBS KSPI   PLATFORM ${_USCRIPT_LINUX_ONLY})
uscript_plugin(KCAN         LIBS KCAN CANTP PLATFORM ${_USCRIPT_LINUX_ONLY})
uscript_plugin(KVCAN        LIBS KVCAN CANTP PLATFORM ${_USCRIPT_LINUX_ONLY})
uscript_plugin(RAWETH       LIBS RAWETH PLATFORM ${_USCRIPT_LINUX_ONLY})
uscript_plugin(SYSTEC       LIBS SYSTEC CANTP PLATFORM ${_USCRIPT_LINUX_ONLY})

uscript_plugin(VECTOR       LIBS VECTOR     PLATFORM ${_USCRIPT_WIN_OR_LINUX})
uscript_plugin(VECTOR_ETH   LIBS VECTOR     PLATFORM ${_USCRIPT_WIN_OR_LINUX})

# =============================================================================
# RESOLUTION: enabled plugins -> required libs (transitive closure)
# =============================================================================
function(_uscript_resolve)
    get_property(_keys    GLOBAL PROPERTY USCRIPT_LIB_KEYS)
    get_property(_plugins GLOBAL PROPERTY USCRIPT_PLUGIN_NAMES)

    foreach(_k IN LISTS _keys)
        set(_need_${_k} OFF)
    endforeach()

    set(_work "")
    foreach(_p IN LISTS _plugins)
        if(USCRIPT_PLUGIN_${_p})
            get_property(_libs GLOBAL PROPERTY USCRIPT_PLUGIN_${_p}_LIBS)
            list(APPEND _work ${_libs})
        endif()
    endforeach()

    while(_work)
        list(POP_FRONT _work _k)
        if(NOT _k IN_LIST _keys)
            message(FATAL_ERROR "uScript: unknown lib key '${_k}' referenced from the "
                                "plugin/lib tables in cmake/uScriptFeatures.cmake")
        endif()
        if(NOT _need_${_k})
            set(_need_${_k} ON)
            get_property(_deps GLOBAL PROPERTY USCRIPT_LIB_${_k}_DEPENDS)
            list(APPEND _work ${_deps})
        endif()
    endwhile()

    foreach(_k IN LISTS _keys)
        set(USCRIPT_LIB_${_k} ${_need_${_k}} CACHE INTERNAL "Derived from USCRIPT_PLUGIN_*")
    endforeach()
endfunction()

_uscript_resolve()

# =============================================================================
# SUMMARY (one short block at configure time)
# =============================================================================
function(_uscript_print_summary)
    get_property(_plugins GLOBAL PROPERTY USCRIPT_PLUGIN_NAMES)
    get_property(_keys    GLOBAL PROPERTY USCRIPT_LIB_KEYS)
    get_property(_unsup   GLOBAL PROPERTY USCRIPT_PLUGINS_UNSUPPORTED)

    set(_on "") 
    set(_off "")
    foreach(_p IN LISTS _plugins)
        if(USCRIPT_PLUGIN_${_p})
            list(APPEND _on ${_p})
        elseif(NOT _p IN_LIST _unsup)
            list(APPEND _off ${_p})
        endif()
    endforeach()
    set(_libs "")
    foreach(_k IN LISTS _keys)
        if(USCRIPT_LIB_${_k})
            list(APPEND _libs ${_k})
        endif()
    endforeach()

    list(JOIN _on   " " _on_s)
    list(JOIN _off  " " _off_s)
    list(JOIN _unsup " " _unsup_s)
    list(JOIN _libs " " _libs_s)
    message(STATUS "uScript plugins ON : ${_on_s}")
    message(STATUS "uScript plugins OFF: ${_off_s}")
    if(_unsup_s)
        message(STATUS "uScript plugins n/a on this platform: ${_unsup_s}")
    endif()
    message(STATUS "uScript driver/protocol libs built: ${_libs_s}")
endfunction()

_uscript_print_summary()

# =============================================================================
# SAFETY NET: every registered target that something links must exist
# =============================================================================
# Call once, at the very end of the top-level CMakeLists.txt (after every
# add_subdirectory()). It walks all targets and fails the configure if one of
# them links a registered library whose group is not enabled - i.e. the tables
# above are out of date for that directory. Without this, a plain target name
# that doesn't exist silently degrades into a "-luFoo" linker flag.
# -----------------------------------------------------------------------------
function(_uscript_collect_targets DIR OUT)
    get_property(_t DIRECTORY "${DIR}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(_s DIRECTORY "${DIR}" PROPERTY SUBDIRECTORIES)
    foreach(_d IN LISTS _s)
        _uscript_collect_targets("${_d}" _sub)
        list(APPEND _t ${_sub})
    endforeach()
    set(${OUT} "${_t}" PARENT_SCOPE)
endfunction()

function(uscript_verify_link_graph)
    get_property(_registered GLOBAL PROPERTY USCRIPT_LIB_TARGETS)
    _uscript_collect_targets("${CMAKE_SOURCE_DIR}" _all)

    set(_errors "")
    foreach(_t IN LISTS _all)
        get_target_property(_type ${_t} TYPE)
        if(_type STREQUAL "INTERFACE_LIBRARY")
            set(_props INTERFACE_LINK_LIBRARIES)
        else()
            set(_props LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        endif()
        foreach(_prop IN LISTS _props)
            get_target_property(_links ${_t} ${_prop})
            if(NOT _links)
                continue()
            endif()
            foreach(_l IN LISTS _links)
                if(_l IN_LIST _registered)
                    get_property(_owner GLOBAL PROPERTY USCRIPT_TARGET_OWNER_${_l})
                    # Judged by the group flag, not by TARGET <name>: some libs are
                    # directory-scoped IMPORTED targets (e.g. ch347) that TARGET
                    # cannot see from the top-level directory.
                    if(NOT USCRIPT_LIB_${_owner})
                        list(APPEND _errors
                             "  '${_t}' links '${_l}', but lib group ${_owner} is not enabled")
                    endif()
                endif()
            endforeach()
        endforeach()
    endforeach()

    if(_errors)
        list(REMOVE_DUPLICATES _errors)
        list(JOIN _errors "\n" _msg)
        message(FATAL_ERROR
            "uScript: inconsistent plugin/driver tables (cmake/uScriptFeatures.cmake):\n"
            "${_msg}\n"
            "Add the missing lib key to the LIBS of the plugin (uscript_plugin) or to "
            "the DEPENDS of the lib (uscript_lib) that links it.")
    endif()
endfunction()
