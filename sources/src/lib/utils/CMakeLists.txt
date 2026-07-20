cmake_minimum_required(VERSION 3.25)
project(uUtils)

add_library(${PROJECT_NAME} INTERFACE)

target_include_directories(${PROJECT_NAME} INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

# uGuiNotify.hpp includes CommDumpProtocol.hpp (CommFamily/CommDetails/gui_notify_comm_dump).
# That header now lives only under include/driver/inc (the stale duplicate that used
# to live in this directory was removed). Depend on uICommDriver explicitly so any
# consumer of uUtils gets a correct include path regardless of what else it links.
target_link_libraries(${PROJECT_NAME} INTERFACE uICommDriver)
