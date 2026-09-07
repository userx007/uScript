cmake_minimum_required(VERSION 3.25)

# ─────────────────────────────────────────────────────────────────────────────
# MSVC-specific compiler switches — applied globally (directory-scoped
# add_compile_options() below propagates to every target added via the
# add_subdirectory() calls that follow, so individual driver/plugin
# CMakeLists.txt files don't each need their own copy of this block).
#
#   /utf-8
#       Forces both the *source* character set (how cl.exe reads this
#       repo's .cpp/.hpp files) and the *execution* character set (how
#       string/char literals are encoded into the compiled binary) to
#       UTF-8. Without it, cl.exe defaults to the active Windows ANSI code
#       page for both. This is not a theoretical concern for this codebase:
#       comments and, more importantly, runtime LOG_STRING(...) literals
#       throughout the driver layer use UTF-8 em dashes (e.g.
#       grpc_driver.cpp: LOG_STRING("Host not configured — GRPC.CONFIG ...")).
#       Under the default (non-UTF-8) code page, cl.exe emits C4819
#       ("file contains a character that cannot be represented in the
#       current code page") and silently mangles those bytes into '?' or
#       garbage in the compiled string — a silent data-corruption bug in
#       every log/error message that contains one, not just a warning.
#       GCC/Clang (MinGW-w64 included) assume UTF-8 source and execution
#       encoding unconditionally, which is why this only ever surfaced
#       when cross-compiling with MinGW and never showed up before.
#
#   /Zc:__cplusplus
#       cl.exe reports __cplusplus as 199711L regardless of /std:c++20
#       unless this flag is passed — /std:c++20 alone only changes which
#       language features are *accepted*, not what the macro *reports*.
#       Nothing in this codebase currently branches on __cplusplus's value
#       (only bare #ifdef __cplusplus / extern "C" guards, which are
#       unaffected), but any vendored SDK header added in the future
#       (FTD2XX, PCANBasic, hidapi, CH347DLL — several of which already
#       vendor both a Windows and Linux variant) could easily start doing
#       so, so this is cheap insurance rather than a fix for an observed
#       failure.
#
#   /permissive-
#       Switches cl.exe from its historically lenient parsing mode to
#       standards-conformant mode (two-phase name lookup, correct
#       `typename`/`template` disambiguation requirements, etc.) — the
#       same conformance level GCC/Clang enforce by default. Since this
#       codebase was written and already compiles cleanly under both GCC
#       and MinGW-w64 (see the CI-equivalent check that produced this
#       comment), enabling conformant mode here should surface nothing new;
#       it mainly exists to keep MSVC from silently accepting non-portable
#       constructs that would break under a future GCC/Clang upgrade.
#       (uCP2112/CMakeLists.txt previously set the non-existent `/permissive`
#       — missing the trailing '-' — which cl.exe does not recognise as
#       disabling anything; fixed to /permissive- alongside this change so
#       the two don't fight over precedence on the command line.)
#
#   /bigobj
#       Raises the number of sections a single .obj can hold. The
#       plugin/protocol layers (uTpFactory, ITransportProtocol, the
#       template- and macro-heavy LOG_PRINT/LOG_STRING call sites repeated
#       hundreds of times per translation unit) are exactly the pattern
#       that hits cl.exe's default section-count ceiling (error C1128),
#       especially in Debug builds. Harmless to set unconditionally — the
#       only cost is marginally larger intermediate .obj files.
# ─────────────────────────────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/utf-8 /Zc:__cplusplus /permissive- /bigobj)
endif()

add_subdirectory(include)
add_subdirectory(src)

# Final relay: src/'s PARENT_SCOPE exports land here; re-export once more so
# the true top-level CMakeLists.txt (the one that does add_subdirectory(sources))
# can see them and install the DLLs next to the built executables. See that
# file's "Windows runtime DLL packaging" section for the full rationale.
if(DEFINED FTDI_RUNTIME_DLL)
    set(FTDI_RUNTIME_DLL "${FTDI_RUNTIME_DLL}" PARENT_SCOPE)
endif()
if(DEFINED VECTOR_RUNTIME_DLL)
    set(VECTOR_RUNTIME_DLL "${VECTOR_RUNTIME_DLL}" PARENT_SCOPE)
endif()
