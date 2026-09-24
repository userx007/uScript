cmake_minimum_required(VERSION 3.25)
project(AlarmsTypes C)

find_package(CycloneDDS REQUIRED)

idlc_generate(TARGET Alarms_types_idl FILES idl/Alarms_PSM.idl idl/LDM_Common.idl)

add_library(${PROJECT_NAME} SHARED
    src/Alarms_adapter.c
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include/driver/inc # DdsTypePluginAbi.h
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    uICommDriver
    uKvParser
    Alarms_types_idl
    CycloneDDS::ddsc

)
