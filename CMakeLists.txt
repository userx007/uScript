cmake_minimum_required(VERSION 3.3)

project (uScript)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set Microsoft Visual Studio specific macros
if(MSVC)
  set(BUILD_LOCAL "ON")
  set(FOLDER_SUFFIX "_vs")
  set(CMAKE_SHARED_LIBRARY_PREFIX "lib")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
else()
  set(FOLDER_SUFFIX "")
endif()

set(INSTALL_WIN_APP_DIR                   "bin/windows${FOLDER_SUFFIX}_${CMAKE_BUILD_TYPE}")
set(INSTALL_WIN_PLUGIN_DIR                "bin/windows${FOLDER_SUFFIX}_${CMAKE_BUILD_TYPE}/plugins")
set(INSTALL_WIN_IPLUGIN_DIR               "bin/windows${FOLDER_SUFFIX}_${CMAKE_BUILD_TYPE}/iplugins")
set(INSTALL_LINUX_APP_DIR                 "bin/linux_${CMAKE_BUILD_TYPE}")
set(INSTALL_LINUX_PLUGIN_DIR              "bin/linux_${CMAKE_BUILD_TYPE}/plugins")
set(INSTALL_LINUX_IPLUGIN_DIR             "bin/linux_${CMAKE_BUILD_TYPE}/iplugins")
set(INSTALL_LINUX_SHAREDLIBS_DIR          "bin/extlibs")

if(MSVC OR MSYS OR MINGW)
    set(INSTALL_APP_DIR                   ${INSTALL_WIN_APP_DIR})
    set(INSTALL_PLUGIN_DIR                ${INSTALL_WIN_PLUGIN_DIR})
    set(INSTALL_IPLUGIN_DIR               ${INSTALL_WIN_IPLUGIN_DIR})
    SET(LIB_INSTALL_TYPE                  RUNTIME)
else()
    set(INSTALL_APP_DIR                   ${INSTALL_LINUX_APP_DIR})
    set(INSTALL_PLUGIN_DIR                ${INSTALL_LINUX_PLUGIN_DIR})
    set(INSTALL_IPLUGIN_DIR               ${INSTALL_LINUX_IPLUGIN_DIR})
    SET(LIB_INSTALL_TYPE                  LIBRARY)
endif()


if( CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT )
  message(STATUS "Setting default CMAKE_INSTALL_PREFIX path to ../")
  set(CMAKE_INSTALL_PREFIX "../" CACHE STRING "The path to use for make install" FORCE)
endif()

if(MINGW)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -static ")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static ")

  if( (CMAKE_BUILD_TYPE STREQUAL "Release") OR (CMAKE_BUILD_TYPE STREQUAL "MinSizeRel") )

#    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s -Os -fsanitize=address")
#    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s -Os -fsanitize=address")

    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s -Os")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s -Os")

  endif()
endif()

if(UNIX AND NOT APPLE)
  set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/extlibs")
endif()

add_subdirectory(sources)


# --- application ---
install( TARGETS uscript                                           DESTINATION ${INSTALL_APP_DIR}        )
# --- plugins     ---
install( TARGETS shell_plugin                  ${LIB_INSTALL_TYPE} DESTINATION ${INSTALL_PLUGIN_DIR}     )
install( TARGETS template_plugin               ${LIB_INSTALL_TYPE} DESTINATION ${INSTALL_PLUGIN_DIR}     )
# --- iplugins    ---
install( TARGETS math_iplugin                  ${LIB_INSTALL_TYPE} DESTINATION ${INSTALL_IPLUGIN_DIR}    )



