cmake_minimum_required(VERSION 3.16)

# ── CP2112 plugin shared library ─────────────────────────────────────────────
#
#   Produces:  cp2112_plugin  (SHARED — loaded at runtime by the plugin loader)
#
#   Exposes (C ABI):
#     CP2112Plugin* pluginEntry()
#     void          pluginExit(CP2112Plugin*)
#
#   Dependencies:
#     cp2112      — driver library (CP2112Base, CP2112, CP2112Gpio)
#     uIPlugin    — PluginInterface (INTERFACE; from sources/include/plugin)
#     uPluginOps  — PluginOperations.hpp generic helpers
#     uUtils      — uString, uNumeric, uHexlify, uHexdump, uLogger
#
#   cp2112_generic.hpp re-includes ft4232_generic.hpp for the shared template
#   helpers.  The include path to that header is satisfied via the
#   ftdi4232_plugin inc/ directory (same as the ft232h plugin does).
#   Adjust the path below if your source layout differs.
# ─────────────────────────────────────────────────────────────────────────────

add_library(cp2112_plugin SHARED
    src/cp2112_plugin.cpp
    src/cp2112_i2c.cpp
    src/cp2112_gpio.cpp
)

target_include_directories(cp2112_plugin
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc/private>
        # cp2112 driver headers
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/lib/drivers/cp2112/inc>
)

target_link_libraries(cp2112_plugin
    PRIVATE
        cp2112      # CP2112 driver (CP2112Base, CP2112 I2C, CP2112Gpio)
        uIPlugin    # PluginInterface.hpp
        uPluginOps  # PluginOperations.hpp, PluginExport.hpp
        uUtils      # uString, uNumeric, uHexlify, uHexdump, uLogger
        uSharedConfig
)

set_target_properties(cp2112_plugin PROPERTIES
    CXX_STANDARD 20
    OUTPUT_NAME  "cp2112_plugin"
    PREFIX       ""   # no lib prefix — loaded as "cp2112_plugin.so" / ".dll"
)
