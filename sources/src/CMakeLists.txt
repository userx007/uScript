cmake_minimum_required(VERSION 3.25)

add_subdirectory(app)
add_subdirectory(config)
add_subdirectory(libs)
add_subdirectory(plugins)
add_subdirectory(scriptgui)
add_subdirectory(scripts)
add_subdirectory(testers)

# Relay both vendor-DLL variables up one more level (to sources/'s scope) —
# lib/'s own PARENT_SCOPE exports only reach here. See the top-level
# CMakeLists.txt's "Windows runtime DLL packaging" section.
if(DEFINED FTDI_RUNTIME_DLL)
    set(FTDI_RUNTIME_DLL "${FTDI_RUNTIME_DLL}" PARENT_SCOPE)
endif()
if(DEFINED VECTOR_RUNTIME_DLL)
    set(VECTOR_RUNTIME_DLL "${VECTOR_RUNTIME_DLL}" PARENT_SCOPE)
endif()


