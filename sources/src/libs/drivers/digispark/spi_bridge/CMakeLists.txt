cmake_minimum_required(VERSION 3.25)
project(uDigisparkSPI)

set(SOURCES src/uSPICommon.cpp)

if(WIN32)
    list(APPEND SOURCES src/uSPIWindows.cpp)
elseif(UNIX)
    list(APPEND SOURCES src/uSPILinux.cpp)
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
# See i2c_bridge/CMakeLists.txt's identical comment for why this needs a
# WIN32 branch at all: pkg_check_modules(... REQUIRED hidapi-hidraw) only
# resolves on Linux.
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

