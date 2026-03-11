cmake_minimum_required(VERSION 3.16)
project(uCH347 LANGUAGES CXX)

# ============================================================================
# Library target
# ============================================================================
add_library(${PROJECT_NAME} STATIC
    src/uCH347Drivers.cpp
)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

# ============================================================================
# Platform-specific CH347 vendor library
# ============================================================================
if(WIN32)
    # -------------------------------------------------------------------------
    # Windows – import library (.lib) + DLL shipped by WCH
    #
    # Architecture detection order:
    #   1. CMAKE_GENERATOR_PLATFORM  (set by -A flag on the cmake command line,
    #      e.g. -A ARM64 / -A x64 / -A Win32)
    #   2. CMAKE_SYSTEM_PROCESSOR    (set by the toolchain or the host OS)
    #   3. CMAKE_SIZEOF_VOID_P       (pointer width fallback)
    # -------------------------------------------------------------------------

    # Normalise the generator platform string (may be empty on single-arch generators)
    string(TOLOWER "${CMAKE_GENERATOR_PLATFORM}" _gen_plat)

    if(_gen_plat STREQUAL "arm64" OR
       CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
        set(_CH347_WIN_ARCH  arm64)
        set(_CH347_WIN_LIB   CH347DLLA64.lib)
    elseif(_gen_plat STREQUAL "x64" OR
           CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_CH347_WIN_ARCH  amd64)
        set(_CH347_WIN_LIB   CH347DLLA64.lib)
    else()
        # 32-bit x86 / Win32
        set(_CH347_WIN_ARCH  i386)
        set(_CH347_WIN_LIB   CH347DLL.lib)
    endif()

    set(_CH347_LIB_FILE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/windows/${_CH347_WIN_ARCH}/${_CH347_WIN_LIB}")
    set(_CH347_INC_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/windows")

    message(STATUS "uCH347: Windows build – arch=${_CH347_WIN_ARCH}, lib=${_CH347_LIB_FILE}")

    if(NOT EXISTS "${_CH347_LIB_FILE}")
        message(WARNING
            "uCH347: CH347 import library not found at:\n"
            "  ${_CH347_LIB_FILE}\n"
            "Make sure the correct .lib file is present in third_party/windows/${_CH347_WIN_ARCH}/")
    endif()

    # SHARED IMPORTED = DLL with an import library (.lib stub)
    add_library(ch347 SHARED IMPORTED)
    set_target_properties(ch347 PROPERTIES
        IMPORTED_IMPLIB           "${_CH347_LIB_FILE}"
        INTERFACE_INCLUDE_DIRECTORIES "${_CH347_INC_DIR}"
    )

    # The corresponding DLL (CH347DLL.dll / CH347DLLA64.dll) must be on PATH
    # or in the same directory as the executable at run-time.
    # Copy it to the build output directory for convenience if it exists.
    if(_CH347_WIN_ARCH STREQUAL "i386")
        set(_CH347_WIN_DLL_NAME CH347DLL.dll)
    else()
        set(_CH347_WIN_DLL_NAME CH347DLLA64.dll)
    endif()

    set(_CH347_DLL_FILE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/windows/${_CH347_WIN_ARCH}/${_CH347_WIN_DLL_NAME}")

    if(EXISTS "${_CH347_DLL_FILE}")
        # Copy DLL next to the built library so consumers can find it easily
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_CH347_DLL_FILE}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
            COMMENT "Copying ${_CH347_WIN_DLL_NAME} to output directory"
        )
    endif()

else()
    # -------------------------------------------------------------------------
    # Linux – static library (.a) shipped by WCH
    #
    # Selects x64 (64-bit pointer) or x86 (32-bit pointer).
    # -------------------------------------------------------------------------
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_CH347_LINUX_ARCH x64)
    else()
        set(_CH347_LINUX_ARCH x86)
    endif()

    set(_CH347_LIB_FILE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux/${_CH347_LINUX_ARCH}/static/libch347.a")
    set(_CH347_INC_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux")

    message(STATUS "uCH347: Linux build – arch=${_CH347_LINUX_ARCH}, lib=${_CH347_LIB_FILE}")

    if(NOT EXISTS "${_CH347_LIB_FILE}")
        message(WARNING
            "uCH347: CH347 static library not found at:\n"
            "  ${_CH347_LIB_FILE}\n"
            "Make sure libch347.a is present in third_party/linux/${_CH347_LINUX_ARCH}/static/")
    endif()

    add_library(ch347 STATIC IMPORTED)
    set_target_properties(ch347 PROPERTIES
        IMPORTED_LOCATION             "${_CH347_LIB_FILE}"
        INTERFACE_INCLUDE_DIRECTORIES "${_CH347_INC_DIR}"
    )
endif()

# ============================================================================
# Link dependencies
# ============================================================================
target_link_libraries(${PROJECT_NAME} PUBLIC
    ch347
    uICommDriver
    uUtils
)
