cmake_minimum_required(VERSION 3.16)

set(TARGET_NAME ftdi4232_plugin)

# ──────────────────────────────
# FTDI SDK (Windows dynamic linking)
# ──────────────────────────────
if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_arch amd64)
    else()
        set(_arch i386)
    endif()

    set(FTD2XX_INCLUDE_DIR "${FTD2XX_ROOT}/include")
    set(FTD2XX_DLL "${FTD2XX_ROOT}/${_arch}/FTD2XX64.dll")
    set(FTD2XX_IMPLIB "${FTD2XX_ROOT}/${_arch}/libftd2xx.a")

    if(NOT EXISTS "${FTD2XX_INCLUDE_DIR}/ftd2xx.h")
        message(FATAL_ERROR "ftd2xx.h not found in ${FTD2XX_INCLUDE_DIR}")
    endif()
    if(NOT EXISTS "${FTD2XX_DLL}")
        message(FATAL_ERROR "FTD2XX DLL not found: ${FTD2XX_DLL}")
    endif()
    if(NOT EXISTS "${FTD2XX_IMPLIB}")
        message(FATAL_ERROR "FTD2XX import library not found: ${FTD2XX_IMPLIB}")
    endif()

    # Only create the imported target if it doesn't exist yet
    if(NOT TARGET ftdi::sdk)
        add_library(ftdi::sdk SHARED IMPORTED GLOBAL)
        set_target_properties(ftdi::sdk PROPERTIES
            IMPORTED_IMPLIB "${FTD2XX_IMPLIB}"
            IMPORTED_LOCATION "${FTD2XX_DLL}"
            INTERFACE_INCLUDE_DIRECTORIES "${FTD2XX_INCLUDE_DIR}"
        )
    endif()

    set(FTDI_EXTRA_LIBS setupapi user32 advapi32)
endif()

# ──────────────────────────────
# Plugin library
# ──────────────────────────────
add_library(${TARGET_NAME} SHARED
    src/ft4232_plugin.cpp
    src/ft4232_spi.cpp
    src/ft4232_i2c.cpp
    src/ft4232_gpio.cpp
    src/ft4232_uart.cpp
)

target_include_directories(${TARGET_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/inc
        ${CMAKE_CURRENT_SOURCE_DIR}/inc/private
)

target_link_libraries(${TARGET_NAME}
    PRIVATE
        ft4232
        uPluginOps
        uIPlugin
        uSharedConfig
        uICoreScript
        uCommScriptClient
        uCommScriptCommandInterpreter
        uScriptReader        
        ftdi::sdk
        ${FTDI_EXTRA_LIBS}
)

target_compile_options(${TARGET_NAME}
    PRIVATE
        ${FTDI_WARN_FLAGS}
)

# ──────────────────────────────
# Copy DLL to output folder AFTER target is created
# ──────────────────────────────
if(WIN32)
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FTD2XX_DLL}"
        $<TARGET_FILE_DIR:${TARGET_NAME}>
    )
endif()