
cmake_minimum_required(VERSION 3.3)

project(ftdi245)

if(MSVC OR MSYS OR MINGW)
    add_library(${PROJECT_NAME}
        OBJECT
            src/ftdi245_windows.cpp
    )
else()
    add_library(${PROJECT_NAME}
        OBJECT
            src/ftdi245_linux.cpp
    )
endif()

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${PROJECT_SOURCE_DIR}/inc
        ${PROJECT_SOURCE_DIR}/inc/private
)

target_link_libraries( ${PROJECT_NAME}
    PUBLIC
        uUtils
)

if(NOT (MSVC OR MSYS OR MINGW))
    target_link_libraries( ${PROJECT_NAME}
        PUBLIC
            dl
    )
endif()


