cmake_minimum_required(VERSION 3.25)
project(slcan_plugin)

add_library(${PROJECT_NAME}
    SHARED
        src/slcan_plugin.cpp
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/inc
        ${CMAKE_CURRENT_SOURCE_DIR}/inc/private
)

# NOTE: uCommScriptClient / uCommScriptCommandInterpreter / uScriptReader are
# intentionally NOT linked here, unlike kvcan_plugin. This plugin implements
# its own small CMD-grammar interpreter (see the "DELIBERATE DEVIATION"
# comment block near the top of src/slcan_plugin.cpp) because uSlcan's
# generic ICommDriver interface is a raw ASCII passthrough rather than
# frame-aware. Swap back to the shared interpreter libs — and delete the
# local interpreter — if uSlcan's tout_read()/tout_write() are ever made
# frame-aware instead.
target_link_libraries( ${PROJECT_NAME}
    PUBLIC
        uIPlugin
        uPluginOps
        uSlcan
)
