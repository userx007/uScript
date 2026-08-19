cmake_minimum_required(VERSION 3.25)
project(uVolatileMacroStore)

# Deliberately SHARED, not INTERFACE/STATIC: VolatileMacroStore::instance()
# must have exactly one definition process-wide so that the main "uscript"
# executable (which statically links uScriptInterpreter, the writer side)
# and every dlopen()'d plugin .so (kvcan_plugin, pcan_plugin, ... - the
# reader side, via uCommandExec's generic_send_cyclic()) all resolve to the
# very same instance at runtime instead of each image getting its own
# private copy of the singleton's storage. See uVolatileMacroStore.hpp's
# rationale comment for the full explanation.
add_library(${PROJECT_NAME} SHARED
    src/uVolatileMacroStore.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)
