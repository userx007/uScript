cmake_minimum_required(VERSION 3.16)
project(cp2112_plugin)

add_library(${PROJECT_NAME}  
    SHARED
        src/cp2112_plugin.cpp
        src/cp2112_i2c.cpp
        src/cp2112_gpio.cpp
)

target_include_directories(${PROJECT_NAME} 
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc/private>
        # cp2112 driver headers
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/lib/drivers/cp2112/inc>
)

target_link_libraries(${PROJECT_NAME} 
    PRIVATE
        cp2112      
        uIPlugin    
        uPluginOps  
        uUtils      
        uSharedConfig
        uICoreScript
        uCommScriptClient
        uCommScriptCommandInterpreter
        uScriptReader        
)


if(WIN32)
    target_link_libraries(cp2112_plugin 
        PRIVATE 
            hid 
            setupapi
    )
endif()