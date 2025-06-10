cmake_minimum_required(VERSION 3.3)

project(ushell_user_plugin_loader)

add_library( ${PROJECT_NAME}
    INTERFACE
)

target_include_directories(${PROJECT_NAME}
    INTERFACE
        ${PROJECT_SOURCE_DIR}/inc
)

if( NOT (MSVC OR MSYS OR MINGW) )
target_link_libraries( ${PROJECT_NAME}
    INTERFACE
        dl
)
endif()