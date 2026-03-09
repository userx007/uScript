cmake_minimum_required(VERSION 3.16)
project(ftdi245)

add_library(${PROJECT_NAME} STATIC
    $<IF:$<BOOL:${WIN32}>,src/ftdi245_windows.cpp,src/ftdi245_linux.cpp>
)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc/private>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME} PUBLIC uUtils)

if(NOT WIN32)
    target_link_libraries(${PROJECT_NAME} PUBLIC dl)
endif()
