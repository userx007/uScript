cmake_minimum_required(VERSION 3.14)

# Check if INSTALL_APP_DIR is defined
if(NOT DEFINED INSTALL_APP_DIR)
    message(WARNING "INSTALL_APP_DIR is not defined. Testers will not be installed.")
    return()
endif()

# Define the source directory for executables
set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

# List all source files for executables
set(TESTER_SOURCES
    ${SOURCE_DIR}/eth_raw_loopback_server.cpp
    ${SOURCE_DIR}/kvcan_loopback.c
    ${SOURCE_DIR}/tcpip_loopback_server.cpp
    ${SOURCE_DIR}/uart_loopback.c
    ${SOURCE_DIR}/udp_loopback_server.cpp
)

# Define the output directory for executables during build
# This creates: testers/bin/linux_<Type>/testers/
set(OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bin/linux_${CMAKE_BUILD_TYPE}/testers)

# Set runtime output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR})

# Collect executable names and create targets
set(TESTER_EXECUTABLES "")
foreach(source ${TESTER_SOURCES})
    get_filename_component(exec_name ${source} NAME_WE)

    add_executable(${exec_name} ${source})

    if(source MATCHES "\\.cpp$")
        target_compile_features(${exec_name} PRIVATE cxx_std_20)
    endif()

    list(APPEND TESTER_EXECUTABLES ${exec_name})
endforeach()

# --- Define Installation Paths ---
# Base path for testers
set(TESTER_BASE_DIR ${INSTALL_APP_DIR}/testers)

# Subdirectories for separation
set(TESTER_BIN_SUBDIR "bin")
set(TESTER_SCRIPTS_SUBDIR "scripts")

# Full installation paths
set(TESTER_BIN_INSTALL_PATH ${TESTER_BASE_DIR}/${TESTER_BIN_SUBDIR})
set(TESTER_SCRIPTS_INSTALL_PATH ${TESTER_BASE_DIR}/${TESTER_SCRIPTS_SUBDIR})

# Verify paths are not empty
if(NOT TESTER_BIN_INSTALL_PATH OR NOT TESTER_SCRIPTS_INSTALL_PATH)
    message(FATAL_ERROR "Invalid install paths: BIN=${TESTER_BIN_INSTALL_PATH}, SCRIPTS=${TESTER_SCRIPTS_INSTALL_PATH}")
endif()

# --- Install Executables into 'bin' subfolder ---
# Note: For TARGETS, PERMISSIONS is often not strictly needed for executables as they are executable by default,
# but we add it to be safe. The standard way for TARGETS is:
install(TARGETS ${TESTER_EXECUTABLES}
    RUNTIME DESTINATION ${TESTER_BIN_INSTALL_PATH}
)
message(STATUS "Installing testers executables to: ${TESTER_BIN_INSTALL_PATH}")

# --- Install Scripts into 'scripts' subfolder ---
# Define the source directory for scripts
set(TESTER_SCRIPTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/scripts)

# List all script files
set(TESTER_SCRIPTS
    ${TESTER_SCRIPTS_DIR}/konsole_all.sh
    ${TESTER_SCRIPTS_DIR}/kvcan_run.sh
    ${TESTER_SCRIPTS_DIR}/raweth_run.sh
    ${TESTER_SCRIPTS_DIR}/tcpip_run.sh
    ${TESTER_SCRIPTS_DIR}/uart_run.sh
    ${TESTER_SCRIPTS_DIR}/udp_run.sh
)

# Check if scripts exist
if(NOT EXISTS ${TESTER_SCRIPTS_DIR})
    message(WARNING "Scripts directory ${TESTER_SCRIPTS_DIR} does not exist.")
else()
    install(FILES ${TESTER_SCRIPTS}
        DESTINATION ${TESTER_SCRIPTS_INSTALL_PATH}
        # Explicit permissions: rwxr-xr-x (755)
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                    GROUP_READ GROUP_EXECUTE
                    WORLD_READ WORLD_EXECUTE
    )
    message(STATUS "Installing testers scripts to: ${TESTER_SCRIPTS_INSTALL_PATH}")
endif()
