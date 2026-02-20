
cmake_minimum_required(VERSION 3.3)
project(uCH347)

set(SOURCES src/uCH347Drivers.cpp )

add_library(${PROJECT_NAME}
    OBJECT
        ${SOURCES}
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${PROJECT_SOURCE_DIR}/inc
)

add_library(ch347 
    STATIC 
        IMPORTED
)

set_target_properties(ch347
    PROPERTIES
        IMPORTED_LOCATION 
            ${CMAKE_CURRENT_SOURCE_DIR}/lib/x64/static/libch347.a
        INTERFACE_INCLUDE_DIRECTORIES 
            ${CMAKE_CURRENT_SOURCE_DIR}/lib/x64/static
)


target_link_libraries(${PROJECT_NAME} 
    PUBLIC  
        ch347 
        uICommDriver      
)
