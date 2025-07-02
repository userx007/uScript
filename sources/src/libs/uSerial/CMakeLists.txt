
cmake_minimum_required(VERSION 3.3)
project(uSerial)

set(SOURCES  src/uSerial_Common.cpp )

if(WIN32)
    list(APPEND SOURCES src/uSerial_Windows.cpp)
elseif(UNIX)
    list(APPEND SOURCES src/uSerial_Linux.cpp)
endif()

add_library(${PROJECT_NAME}
    OBJECT
        ${SOURCES}
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${PROJECT_SOURCE_DIR}/inc
)

target_link_libraries(${PROJECT_NAME}
    PUBLIC
        uUtils
)
