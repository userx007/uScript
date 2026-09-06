cmake_minimum_required(VERSION 3.20)

project(uVector
    VERSION     1.0.0
    DESCRIPTION "Vector Informatik XL-API driver wrapper implementing ICommDriver (Windows only)"
    LANGUAGES   CXX
)

# ---------------------------------------------------------------------------
# Platform check — the XL Driver Library only ships for Windows.
# ---------------------------------------------------------------------------
if(NOT WIN32)
    message(FATAL_ERROR "uVector: Vector's XL Driver Library (vxlapi) is Windows-only. "
                         "This component cannot be built for this target platform.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "uVector: only 64-bit (x64) builds are supported (vxlapi64.dll).")
endif()

option(UVECTOR_BUILD_SHARED "Build uVector as a shared library instead of static" OFF)

# ---------------------------------------------------------------------------
# Third-party: Vector XL Driver Library (XL-API) -> VectorXL::vxlapi
#
# Same layering as ftdi2xx/CMakeLists.txt + ftdi2xx/third_party/CMakeLists.txt:
# resolution logic lives in third_party/, which defines the imported target
# and (when the real SDK is present) hands VECTOR_RUNTIME_DLL up via
# PARENT_SCOPE for whatever assembles the final extlibs/ folder to pick up -
# see third_party/CMakeLists.txt's header comment for the vendoring layout
# (third_party/vxlapi/amd64/vxlapi64.lib + .dll + .h).
# ---------------------------------------------------------------------------
add_subdirectory(third_party)

# Re-export one level further up, same as ftdi2xx's third_party/CMakeLists.txt
# does directly (this project just has one extra level of nesting: uVector/
# sits between third_party/ and whatever consumes it).
if(DEFINED VECTOR_RUNTIME_DLL)
    set(VECTOR_RUNTIME_DLL "${VECTOR_RUNTIME_DLL}" PARENT_SCOPE)
endif()

# ---------------------------------------------------------------------------
# uVector library target
# ---------------------------------------------------------------------------
if(UVECTOR_BUILD_SHARED)
    add_library(uVector SHARED ${CMAKE_CURRENT_SOURCE_DIR}/src/uVector.cpp)
else()
    add_library(uVector STATIC ${CMAKE_CURRENT_SOURCE_DIR}/src/uVector.cpp)
endif()
add_library(uVector::uVector ALIAS uVector)

set_target_properties(uVector PROPERTIES
    CXX_STANDARD          20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS        OFF
)

target_include_directories(uVector
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(uVector
    PUBLIC
        VectorXL::vxlapi
        uICommDriver
        uITpInterface
        uTpFactory
        uTpConfig
        uIsoTp
        uJ1939Tp
        uUtils
)

target_compile_definitions(uVector PRIVATE _WIN32_WINNT=0x0601)

# Convenience for local dev/debug builds only: copy vxlapi64.dll next to
# uVector's own build output so a quick standalone run/debug session finds it
# without needing the full extlibs/ packaging step. This is IN ADDITION TO
# VECTOR_RUNTIME_DLL - not a replacement for it - since the real extlibs/
# folder sits next to the final uscript executable, not next to this
# component's intermediate build artifacts.
if(UVECTOR_BUILD_SHARED AND DEFINED VECTOR_RUNTIME_DLL)
    add_custom_command(TARGET uVector POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${VECTOR_RUNTIME_DLL}" "$<TARGET_FILE_DIR:uVector>"
        COMMENT "uVector: copying vxlapi64.dll next to build output for local runs"
    )
endif()
