# =============================================================================
# uScriptPlugins.cmake - the ONE place to switch plugins ON / OFF
# =============================================================================
# Edit the values below, then configure with:
#
#     cmake -S . -B build -C sources/cmake/uScriptPlugins.cmake
#
# (-C pre-loads these values into the cache of a NEW build directory.
#  For an existing build directory use `cmake -DUSCRIPT_PLUGIN_X=ON/OFF .`
#  or ccmake / cmake-gui.)
#
# Drivers, protocols, the CAN-TP stack, the FTDI SDK and the loopback testers
# are enabled automatically from the plugins you select here.
# Plugins not supported on the platform (KI2C/KSPI/KVCAN/RAWETH/SYSTEC off
# Linux, VECTOR* off Windows/Linux) are ignored with a warning.
# =============================================================================

# ---- Serial / USB bridges ---------------------------------------------------
set(USCRIPT_PLUGIN_UART        ON  CACHE BOOL "")
set(USCRIPT_PLUGIN_UARTMON     ON  CACHE BOOL "")
set(USCRIPT_PLUGIN_BUSPIRATE   ON  CACHE BOOL "")
set(USCRIPT_PLUGIN_HYDRABUS    ON  CACHE BOOL "")
set(USCRIPT_PLUGIN_CH341       ON  CACHE BOOL "")
set(USCRIPT_PLUGIN_CH347       ON  CACHE BOOL "")
set(USCRIPT_PLUGIN_CP2112      ON  CACHE BOOL "")
set(USCRIPT_PLUGIN_DSPKI2C     ON  CACHE BOOL "")
set(USCRIPT_PLUGIN_DSPKSPI     ON  CACHE BOOL "")

# ---- FTDI (needs libusb-1.0 on Linux) --------------------------------------
set(USCRIPT_PLUGIN_FTDI232H    ON  CACHE BOOL "")
set(USCRIPT_PLUGIN_FTDI2232    ON  CACHE BOOL "")
set(USCRIPT_PLUGIN_FTDI245     ON  CACHE BOOL "")
set(USCRIPT_PLUGIN_FTDI4232    ON  CACHE BOOL "")

# ---- CAN adapters -----------------------------------------------------------
set(USCRIPT_PLUGIN_SLCAN       ON  CACHE BOOL "")
set(USCRIPT_PLUGIN_CANDLELIGHT ON  CACHE BOOL "")
set(USCRIPT_PLUGIN_PCAN        ON  CACHE BOOL "")
set(USCRIPT_PLUGIN_VECTOR      ON  CACHE BOOL "")   # Windows / Linux only
set(USCRIPT_PLUGIN_VECTOR_ETH  ON  CACHE BOOL "")   # Windows / Linux only
set(USCRIPT_PLUGIN_KVCAN       ON  CACHE BOOL "")   # Linux only
set(USCRIPT_PLUGIN_SYSTEC      ON  CACHE BOOL "")   # Linux only

# ---- Ethernet / network ------------------------------------------------------
set(USCRIPT_PLUGIN_TCPIP       ON  CACHE BOOL "")
set(USCRIPT_PLUGIN_UDP         ON  CACHE BOOL "")
set(USCRIPT_PLUGIN_WEBSOCKET   ON  CACHE BOOL "")
set(USCRIPT_PLUGIN_W5500       ON  CACHE BOOL "")
set(USCRIPT_PLUGIN_ENC28J60    ON  CACHE BOOL "")
set(USCRIPT_PLUGIN_LAN8720     ON  CACHE BOOL "")
set(USCRIPT_PLUGIN_RAWETH      ON  CACHE BOOL "")   # Linux only

# ---- Linux kernel interfaces -------------------------------------------------
set(USCRIPT_PLUGIN_KI2C        ON  CACHE BOOL "")   # Linux only
set(USCRIPT_PLUGIN_KSPI        ON  CACHE BOOL "")   # Linux only

# ---- Protocols (MQTT needs OpenSSL) ------------------------------------------
set(USCRIPT_PLUGIN_MODBUS      ON  CACHE BOOL "")
set(USCRIPT_PLUGIN_MQTT        ON  CACHE BOOL "")
set(USCRIPT_PLUGIN_PROFIBUS    ON  CACHE BOOL "")

# ---- Heavy external SDKs: set ON only if the SDK is installed ----------------
set(USCRIPT_PLUGIN_DDS         ON  CACHE BOOL "")   # needs CycloneDDS
set(USCRIPT_PLUGIN_DDS_TYPED   ON  CACHE BOOL "")   # needs CycloneDDS
set(USCRIPT_PLUGIN_GRPC        ON  CACHE BOOL "")   # needs gRPC + Protobuf

# ---- Misc --------------------------------------------------------------------
set(USCRIPT_PLUGIN_SHELL       ON  CACHE BOOL "")

# ---- Helper executables (loopback servers etc.) ------------------------------
set(USCRIPT_BUILD_TESTERS      ON  CACHE BOOL "")
