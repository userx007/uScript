cmake_minimum_required(VERSION 3.25)
add_subdirectory(drivers)
add_subdirectory(plugin_ops)
add_subdirectory(utils)
add_subdirectory(protocols)

# Relay both vendor-DLL variables up one more level (to src/'s scope) —
# drivers/'s own PARENT_SCOPE exports only reach here. See the top-level
# CMakeLists.txt's "Windows runtime DLL packaging" section.
if(DEFINED FTDI_RUNTIME_DLL)
    set(FTDI_RUNTIME_DLL "${FTDI_RUNTIME_DLL}" PARENT_SCOPE)
endif()
if(DEFINED VECTOR_RUNTIME_DLL)
    set(VECTOR_RUNTIME_DLL "${VECTOR_RUNTIME_DLL}" PARENT_SCOPE)
endif()


