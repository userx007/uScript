#!/bin/bash

APP_NAME=streamer
MINGW_TOOLCHAIN=mingw-w64-x86_64.cmake

# -DCMAKE_VERBOSE_MAKEFILE:BOOL=ON


#----------------------------------------------
# remove the build files
#----------------------------------------------

do_cleanup() {

    echo "======================================================"
    echo "Cleaning up $1 $1"
    echo "======================================================"

    echo "Cleaning up the build folder .."
    rm -rf build_$1_$2/*

    echo "Cleaning up the bin folder .."
    rm -rf bin/$1_$2/plugins
    rm -rf bin/$1_$2/${APP_NAME}*

    echo "Cleaning up finished!"

}


#----------------------------------------------
# build and install
#----------------------------------------------

do_build() {

    if [[ $1 == "windows" || $1 == "linux" ]]; then

        if [[ $2 == "Debug" || $2 == "Release" || $2 == "RelWithDebInfo" || $2 == "MinSizeRel" ]]; then

        echo "======================================================"
        echo "Building for $1, mode $2"
        echo "======================================================"

        mkdir -p build_$1_$2
        mkdir -p bin/$1_$2/plugins

        cd build_$1_$2
        echo "Cleaning up the build_$1_$2 folder"
        rm -rf *
        echo "Building in build_$1_$2 ..."

        if [[ $1 == "windows" ]]; then
            cmake -DCMAKE_BUILD_TYPE=$2 -DCMAKE_TOOLCHAIN_FILE=mingw-w64-x86_64.cmake ..
        else
            cmake -DCMAKE_BUILD_TYPE=$2 ..
        fi

        make install
        echo "$1 build finished !"
        cd -

        else
            echo "Invalid option, use: Debug | Release | RelWithDebInfo | MinSizeRel"
        fi

    else
        echo "Invalid option, use: windows linux"
    fi

}


#----------------------------------------------
# main
#----------------------------------------------

if [[ $1 == "clean" ]]; then

    if [[ $2 == "linux" || $2 == "windows" ]]; then
        do_cleanup $2 $3
    elif [[ $2 == "all" ]]; then
        do_cleanup linux $3
        do_cleanup windows $3
    else
        echo "Invalid option, use: ./build.sh linux|windows|all Debug|Release|RelWithDebInfo|MinSizeRel"
    fi

elif [[ $1 == "linux" || $1 == "windows" ]]; then
    do_build $1 $2

elif [[ $1 == "all" ]]; then
    do_build $1 $2
    do_build $1 $2

else
    echo "Invalid option. Try: all, linux, windows, clean"
    echo " clean   : cleanup build and bin folder for: all, linux, windows"
    echo " linux   : builds linux only artefacts"
    echo " windows : builds windows only artefacts"
    echo " all     : builds both windows and linux artefacts"
    echo " ./build.sh linux|windows|all  Debug|Release|RelWithDebInfo|MinSizeRel"
fi

