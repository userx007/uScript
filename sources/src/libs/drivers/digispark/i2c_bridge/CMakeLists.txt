cmake_minimum_required(VERSION 3.25)
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


# hidapi:
#   Linux : sudo apt install libhidapi-dev        (ships pkg-config .pc files)
#   Windows: vcpkg install hidapi                 (ships a CMake package, not pkg-config)
#
# pkg_check_modules(... REQUIRED hidapi-hidraw) unconditionally required a
# .pc file that only exists on Linux (hidapi-hidraw is that backend's own
# name for its Linux hidraw-kernel-subsystem implementation — Windows uses
# hidapi's native Win32 HID backend instead, under a different package
# entirely), so this used to hard-fail CMake configuration on Windows
# before ever reaching target_link_libraries() below.
if(WIN32)
    find_package(hidapi CONFIG REQUIRED)
    set(HIDAPI_LIBRARIES hidapi::hidapi)
    set(HIDAPI_INCLUDE_DIRS "")
    set(HIDAPI_CFLAGS_OTHER "")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HIDAPI REQUIRED hidapi-hidraw)   # or hidapi-libusb
endif()

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

