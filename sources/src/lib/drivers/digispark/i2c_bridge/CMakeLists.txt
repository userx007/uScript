cmake_minimum_required(VERSION 3.16)
project(uDigisparkI2C)

set(SOURCES src/uI2CCommon.cpp)

if(WIN32)
    list(APPEND SOURCES src/uI2CWindows.cpp)
elseif(UNIX)
    list(APPEND SOURCES src/uI2CLinux.cpp)
endif()


add_library(${PROJECT_NAME} STATIC ${SOURCES})


target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)


# hidapi — install via:  sudo apt install libhidapi-dev
find_package(PkgConfig REQUIRED)
pkg_check_modules(HIDAPI REQUIRED hidapi-hidraw)   # or hidapi-libusb

target_include_directories(${PROJECT_NAME}
    PUBLIC  
        ${HIDAPI_INCLUDE_DIRS}
)

target_compile_options(${PROJECT_NAME}
    PRIVATE 
        ${HIDAPI_CFLAGS_OTHER}
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    ${HIDAPI_LIBRARIES}
    uICommDriver
    uUtils
)

