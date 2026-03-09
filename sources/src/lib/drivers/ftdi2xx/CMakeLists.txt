cmake_minimum_required(VERSION 3.16)
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

# ── Project modules ───────────────────────────────────────────────────────────
add_subdirectory(ftdi232)
add_subdirectory(ftdi2232)
add_subdirectory(ftdi4232)
add_subdirectory(ftdi245)