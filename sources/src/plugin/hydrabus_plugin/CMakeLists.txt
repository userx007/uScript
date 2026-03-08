cmake_minimum_required(VERSION 3.14)
project(hydrabus_plugin CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

###########################################################################
# Sources
###########################################################################

add_library(${PROJECT_NAME} SHARED
    src/hydrabus_plugin.cpp
    src/hydrabus_spi.cpp
    src/hydrabus_i2c.cpp
    src/hydrabus_uart.cpp
    src/hydrabus_onewire.cpp
    src/hydrabus_rawwire.cpp
    src/hydrabus_swd.cpp
    src/hydrabus_smartcard.cpp
    src/hydrabus_nfc.cpp
    src/hydrabus_mmc.cpp
    src/hydrabus_sdio.cpp
)

###########################################################################
# Include paths
###########################################################################

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${PROJECT_SOURCE_DIR}/inc
        ${PROJECT_SOURCE_DIR}/inc/private
)

# HydraHAL library headers (caller must set HYDRAHAL_INCLUDE_DIR or have
# HydraHAL installed system-wide)
if(DEFINED HYDRAHAL_INCLUDE_DIR)
    target_include_directories(${PROJECT_NAME} PUBLIC ${HYDRAHAL_INCLUDE_DIR})
endif()

# ICommDriver header (provided by the host framework)
if(DEFINED ICOMMDRIVER_INCLUDE_DIR)
    target_include_directories(${PROJECT_NAME} PUBLIC ${ICOMMDRIVER_INCLUDE_DIR})
endif()

###########################################################################
# Dependencies
###########################################################################

# HydraHAL static/shared library
if(DEFINED HYDRAHAL_LIB_DIR)
    target_link_directories(${PROJECT_NAME} PRIVATE ${HYDRAHAL_LIB_DIR})
endif()

target_link_libraries(${PROJECT_NAME}
    PUBLIC
        uSharedConfig
        uIPlugin
        uPluginOps
        uICoreScript
        uCommScriptClient
        uCommScriptCommandInterpreter
        uScriptReader
        uUart
        uUtils
    PRIVATE
        HydraHAL      # the library produced by HydraHAL/CMakeLists.txt
)

###########################################################################
# Install rules
###########################################################################

install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

#install(DIRECTORY ${PROJECT_SOURCE_DIR}/inc/
#    DESTINATION include/hydrabus_plugin
#    FILES_MATCHING PATTERN "*.hpp"
#    PATTERN "private" EXCLUDE
#)
