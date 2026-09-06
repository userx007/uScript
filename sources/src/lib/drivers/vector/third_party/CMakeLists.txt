cmake_minimum_required(VERSION 3.25)
# ─────────────────────────────────────────────────────────────────────────────
# Vector XL Driver Library (XL-API) → single IMPORTED target VectorXL::vxlapi
# GLOBAL makes it visible to sibling/parent scopes, same convention as
# ftdi2xx/third_party/CMakeLists.txt's ftdi::sdk target.
#
# Vendoring convention (mirrors third_party/ftd2xx/<arch>/ in ftdi2xx):
#   third_party/vxlapi/
#     ├── include/vxlapi.h        (bundled compatibility stub, used only as fallback)
#     └── amd64/
#         ├── vxlapi.h            (drop your real SDK header here to override the stub)
#         ├── vxlapi64.lib        (import library)
#         └── vxlapi64.dll        (runtime DLL - also becomes VECTOR_RUNTIME_DLL, see below)
#
# There's no i386 case: the XL Driver Library hasn't shipped a 32-bit build in
# years and this project only supports 64-bit anyway (see the parent
# CMakeLists.txt's CMAKE_SIZEOF_VOID_P check).
# ─────────────────────────────────────────────────────────────────────────────

if(NOT WIN32)
    message(FATAL_ERROR "uVector/third_party: vxlapi is Windows-only.")
endif()

set(VXLAPI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vxlapi"
    CACHE PATH "Root of the vendored/overridden Vector XL Driver Library SDK")

set(_vxlapi_vendored_dir "${VXLAPI_ROOT}/amd64")
set(_vxlapi_vendored_lib "${_vxlapi_vendored_dir}/vxlapi64.lib")
set(_vxlapi_vendored_dll "${_vxlapi_vendored_dir}/vxlapi64.dll")
set(_vxlapi_vendored_hdr "${_vxlapi_vendored_dir}/vxlapi.h")

if(EXISTS "${_vxlapi_vendored_lib}" AND EXISTS "${_vxlapi_vendored_dll}")

    # Real SDK has been dropped into third_party/vxlapi/amd64/ - use it.
    if(EXISTS "${_vxlapi_vendored_hdr}")
        set(_vxlapi_include_dir "${_vxlapi_vendored_dir}")
    else()
        message(WARNING "uVector/third_party: found vxlapi64.lib/.dll under "
                         "${_vxlapi_vendored_dir} but no vxlapi.h next to them - "
                         "falling back to the bundled stub header for declarations. "
                         "Drop the real vxlapi.h alongside the .lib/.dll to silence this.")
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

    message(STATUS "[vector] XL-API SDK (vendored): ${_vxlapi_vendored_lib}")

else()

    message(WARNING "uVector/third_party: no vxlapi64.lib/.dll found under "
                     "${_vxlapi_vendored_dir} - using the bundled compatibility "
                     "stub header. This lets the source compile but there is "
                     "nothing to actually link a final binary against until you "
                     "drop your real vxlapi64.lib/.dll/.h there. See this file's "
                     "header comment for the expected layout.")

    add_library(VectorXL::vxlapi INTERFACE IMPORTED GLOBAL)
    set_target_properties(VectorXL::vxlapi PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${VXLAPI_ROOT}/include"
    )

    # Nothing to hand up in stub mode.
    unset(VECTOR_RUNTIME_DLL PARENT_SCOPE)

endif()
