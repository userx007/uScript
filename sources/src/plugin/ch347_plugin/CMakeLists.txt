cmake_minimum_required(VERSION 3.16)
project(ch347_plugin)

###################################################################
#                     SOURCES                                     #
###################################################################

add_library(${PROJECT_NAME} SHARED
    src/ch347_plugin.cpp
    src/ch347_spi.cpp
    src/ch347_i2c.cpp
    src/ch347_gpio.cpp
    src/ch347_jtag.cpp
)

###################################################################
#                     INCLUDE PATHS                               #
###################################################################

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/inc
        ${CMAKE_CURRENT_SOURCE_DIR}/inc/private
)

###################################################################
#                     LINK DEPENDENCIES                           #
###################################################################

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        uCH347          # CH347 driver wrapper (uCH347Spi/I2c/Gpio/Jtag)
        uPluginOps
        uIPlugin
        uSharedConfig
        uICoreScript
        uCommScriptClient
        uCommScriptCommandInterpreter
        uScriptReader 	
        uICommDriver    # ICommDriver interface
        uUtils          # uLogger, uString, uHexlify, uNumeric, uFile, etc.
        uPlugin         # IPlugin, PluginInterface, PluginOperations
)

###################################################################
#                     COMPILER FLAGS                              #
###################################################################

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:
        -Wall -Wextra -Wpedantic -Wshadow
        -Wno-unused-parameter>
)

###################################################################
#                     INSTALL                                     #
###################################################################

#install(TARGETS ${PROJECT_NAME}
#    LIBRARY DESTINATION lib/plugins
#)
