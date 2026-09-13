cmake_minimum_required(VERSION 3.25)
# ─────────────────────────────────────────────────────────────────────────────
# Vector XL Driver Library (XL-API) → single IMPORTED target VectorXL::vxlapi,
# on both Windows (Vector's own SDK) and Linux (the unofficial Linux port -
# see vxlapi_linux.h's own header comment for what that actually is and how
# it was derived). GLOBAL makes it visible to sibling/parent scopes, same
# convention as ftdi2xx/third_party/CMakeLists.txt's ftdi::sdk target.
#
# Vendoring convention (mirrors third_party/ftd2xx/<arch>/ in ftdi2xx):
#   third_party/vxlapi/
#     ├── include/
#     │   ├── vxlapi.h            (Vector's real, unmodified Windows XL Driver Library
#     │   │                        header - used whenever amd64/ below has no .lib/.dll)
#     │   └── vxlapi_linux.h      (the unofficial Linux port's header - used whenever
#     │                            linux/ below has no .so)
#     ├── amd64/                  (Windows only)
#     │   ├── vxlapi.h            (optional override - see below)
#     │   ├── vxlapi64.lib        (import library - REQUIRED for an actual link to succeed)
#     │   └── vxlapi64.dll        (runtime DLL - also becomes VECTOR_RUNTIME_DLL, see below)
#     └── linux/                  (Linux only)
#         ├── vxlapi_linux.h      (optional override - only needed if your installed
#         │                        library's header differs from the vendored one above)
#         └── libXlApi.so.26.20.14  (the versioned .so itself - REQUIRED for an actual
#                                     link to succeed; this repo has no unversioned
#                                     "libXlApi.so" symlink, so IMPORTED_LOCATION points
#                                     straight at the versioned file - see below)
#
# There's no i386 case on either platform: the XL Driver Library hasn't
# shipped a 32-bit build in years and this project only supports 64-bit
# anyway (see the parent CMakeLists.txt's CMAKE_SIZEOF_VOID_P check).
# ─────────────────────────────────────────────────────────────────────────────

if(NOT (WIN32 OR LINUX))
    message(FATAL_ERROR "uVector/third_party: vxlapi ships for Windows and Linux only.")
endif()

set(VXLAPI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vxlapi"
    CACHE PATH "Root of the vendored/overridden Vector XL Driver Library SDK")

if(WIN32)

    set(_vxlapi_vendored_dir "${VXLAPI_ROOT}/amd64")
    set(_vxlapi_vendored_lib "${_vxlapi_vendored_dir}/vxlapi64.lib")
    set(_vxlapi_vendored_dll "${_vxlapi_vendored_dir}/vxlapi64.dll")
    set(_vxlapi_vendored_hdr "${_vxlapi_vendored_dir}/vxlapi.h")

    if(EXISTS "${_vxlapi_vendored_lib}" AND EXISTS "${_vxlapi_vendored_dll}")

        # Real SDK has been dropped into third_party/vxlapi/amd64/ - use it.
        if(EXISTS "${_vxlapi_vendored_hdr}")
            set(_vxlapi_include_dir "${_vxlapi_vendored_dir}")
        else()
            message(STATUS "uVector/third_party: found vxlapi64.lib/.dll under "
                            "${_vxlapi_vendored_dir} but no vxlapi.h next to them - "
                            "falling back to the vendored (real) vxlapi.h under "
                            "${VXLAPI_ROOT}/include for declarations. Drop a copy of "
                            "your installed SDK's own vxlapi.h alongside the .lib/.dll "
                            "instead if you need it to differ from the vendored one.")
            set(_vxlapi_include_dir "${VXLAPI_ROOT}/include")
        endif()

        add_library(VectorXL::vxlapi SHARED IMPORTED GLOBAL)
        set_target_properties(VectorXL::vxlapi PROPERTIES
            IMPORTED_IMPLIB               "${_vxlapi_vendored_lib}"
            IMPORTED_LOCATION             "${_vxlapi_vendored_dll}"
            INTERFACE_INCLUDE_DIRECTORIES "${_vxlapi_include_dir}"
        )

        # Hand the DLL's path up to whatever level assembles the final extlibs/
        # folder next to the uscript executable - same pattern ftdi2xx uses for
        # FTDI_RUNTIME_DLL. This project does not copy it itself: this directory
        # doesn't know where the final binary output tree lives.
        set(VECTOR_RUNTIME_DLL "${_vxlapi_vendored_dll}" PARENT_SCOPE)

        message(STATUS "[vector] XL-API SDK (vendored, Windows): ${_vxlapi_vendored_lib}")

    else()

        message(WARNING "uVector/third_party: no vxlapi64.lib/.dll found under "
                         "${_vxlapi_vendored_dir} - using the vendored vxlapi.h "
                         "(Vector's real header) for declarations only. This lets "
                         "the source compile but there is nothing to actually link "
                         "a final binary against until you drop your real "
                         "vxlapi64.lib/.dll there (and optionally vxlapi.h, if your "
                         "installed SDK's header differs from the vendored one). "
                         "See this file's header comment for the expected layout.")

        add_library(VectorXL::vxlapi INTERFACE IMPORTED GLOBAL)
        set_target_properties(VectorXL::vxlapi PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${VXLAPI_ROOT}/include"
        )

        unset(VECTOR_RUNTIME_DLL PARENT_SCOPE)

    endif()

elseif(LINUX)

    set(_vxlapi_linux_dir "${VXLAPI_ROOT}/linux")
    set(_vxlapi_linux_so  "${_vxlapi_linux_dir}/libXlApi.so.26.20.14")
    set(_vxlapi_linux_hdr "${_vxlapi_linux_dir}/vxlapi_linux.h")

    if(EXISTS "${_vxlapi_linux_so}")

        if(EXISTS "${_vxlapi_linux_hdr}")
            set(_vxlapi_include_dir "${_vxlapi_linux_dir}")
        else()
            message(STATUS "uVector/third_party: found libXlApi.so.26.20.14 under "
                            "${_vxlapi_linux_dir} but no vxlapi_linux.h next to it - "
                            "falling back to the vendored vxlapi_linux.h under "
                            "${VXLAPI_ROOT}/include for declarations. Drop a copy of "
                            "your installed library's own header alongside the .so "
                            "instead if you need it to differ from the vendored one.")
            set(_vxlapi_include_dir "${VXLAPI_ROOT}/include")
        endif()

        add_library(VectorXL::vxlapi SHARED IMPORTED GLOBAL)
        set_target_properties(VectorXL::vxlapi PROPERTIES
            IMPORTED_LOCATION             "${_vxlapi_linux_so}"
            IMPORTED_NO_SONAME            TRUE  # no unversioned libXlApi.so symlink is vendored -
                                                  # link straight against the versioned file itself
            INTERFACE_INCLUDE_DIRECTORIES "${_vxlapi_include_dir}"
        )

        # Same role as VECTOR_RUNTIME_DLL on Windows: hand the shared object's
        # path up to whatever assembles the final output tree, since it needs
        # to sit next to (or be rpath'd from) the final executable at runtime
        # just like the Windows DLL does. Kept under the same variable name
        # rather than a separate VECTOR_RUNTIME_SO so downstream packaging
        # logic only has to branch on WIN32 once, not per-artifact.
        set(VECTOR_RUNTIME_DLL "${_vxlapi_linux_so}" PARENT_SCOPE)

        message(STATUS "[vector] XL-API SDK (vendored, Linux): ${_vxlapi_linux_so}")

    else()

        message(WARNING "uVector/third_party: no libXlApi.so.26.20.14 found under "
                         "${_vxlapi_linux_dir} - using the vendored vxlapi_linux.h "
                         "for declarations only. This lets the source compile but "
                         "there is nothing to actually link a final binary against "
                         "until you drop your real libXlApi.so.26.20.14 there (and "
                         "optionally vxlapi_linux.h, if your installed library's "
                         "header differs from the vendored one). See this file's "
                         "header comment for the expected layout.")

        add_library(VectorXL::vxlapi INTERFACE IMPORTED GLOBAL)
        set_target_properties(VectorXL::vxlapi PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${VXLAPI_ROOT}/include"
        )

        unset(VECTOR_RUNTIME_DLL PARENT_SCOPE)

    endif()

endif()
