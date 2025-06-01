#!/bin/bash

if [ -z "$1" ]; then
    echo "Plugin name missing.."
else
    #convert to lowercase
    plugin_name=$1
    use_name=${plugin_name,,}

    echo ${use_name}

    cp -rf template_plugin ${use_name}_plugin

    mv ${use_name}_plugin/inc/template_plugin.hpp ${use_name}_plugin/inc/${use_name}_plugin.hpp
    mv ${use_name}_plugin/src/template_plugin_dd.hpp ${use_name}_plugin/src/${use_name}_plugin_dd.hpp
    mv ${use_name}_plugin/src/template_plugin.cpp ${use_name}_plugin/src/${use_name}_plugin.cpp

    toreplace=${use_name}

    sed -i -e "s/template/${toreplace}/g" -e "s/Template/${toreplace^}/g" -e "s/TEMPLATE/${toreplace^^}/g" ${use_name}_plugin/inc/${use_name}_plugin.hpp
    sed -i -e "s/template/${toreplace}/g" -e "s/Template/${toreplace^}/g" -e "s/TEMPLATE/${toreplace^^}/g" ${use_name}_plugin/src/${use_name}_plugin_dd.hpp
    sed -i -e "s/template/${toreplace}/g" -e "s/Template/${toreplace^}/g" -e "s/TEMPLATE/${toreplace^^}/g" ${use_name}_plugin/src/${use_name}_plugin.cpp
    sed -i -e "s/template/${toreplace}/g" ${use_name}_plugin/CMakeLists.txt

    echo -n "add_subdirectory(${use_name}_plugin)" >> CMakeLists.txt

    echo "plugin created ok.."
    echo "Add a new install section in the main CMakeLists.txt for this plugin .. "

fi



