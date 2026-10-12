cmake_minimum_required(VERSION 3.25)
project(ftdi_project VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# ── SDK root overrides ────────────────────────────────────────────────────────
set(LIBFTDI1_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libftdi1"
    CACHE PATH "Root of the vendored libftdi1 SDK (Linux / macOS)")
set(FTD2XX_ROOT   "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ftd2xx"
    CACHE PATH "Root of the vendored FTD2XX SDK (Windows)")

# ── Warning flags ─────────────────────────────────────────────────────────────
if(MSVC)
    set(FTDI_WARN_FLAGS /W4 /permissive-)
else()
    set(FTDI_WARN_FLAGS -Wall -Wextra -Wpedantic)
endif()

# ── Third-party imported targets (defines ftdi::sdk) ─────────────────────────
add_subdirectory(third_party)

# Relay FTDI_RUNTIME_DLL up one more level (to src/lib/drivers/'s scope) —
# third_party/CMakeLists.txt's own PARENT_SCOPE only reaches here. See the
# top-level CMakeLists.txt's "Windows runtime DLL packaging" section for
# why this relay chain exists and where it ultimately terminates.
if(DEFINED FTDI_RUNTIME_DLL)
    set(FTDI_RUNTIME_DLL "${FTDI_RUNTIME_DLL}" PARENT_SCOPE)
endif()

# ── Project modules ───────────────────────────────────────────────────────────
# Each chip family is only configured if its plugin is enabled (the shared SDK
# above is configured whenever at least one of them is).
uscript_add_subdirectory(USCRIPT_LIB_FTDI232  ftdi232)
uscript_add_subdirectory(USCRIPT_LIB_FTDI2232 ftdi2232)
uscript_add_subdirectory(USCRIPT_LIB_FTDI4232 ftdi4232)
uscript_add_subdirectory(USCRIPT_LIB_FTDI245  ftdi245)