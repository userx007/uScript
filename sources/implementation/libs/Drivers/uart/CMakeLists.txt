
cmake_minimum_required(VERSION 3.3)
project(uUart)

set(SOURCES  src/uUart_Common.cpp )

if(WIN32)
    list(APPEND SOURCES src/uUart_Windows.cpp)
elseif(UNIX)
    list(APPEND SOURCES src/uUart_Linux.cpp)
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
        DriverInterfaces
        uUtils
)
