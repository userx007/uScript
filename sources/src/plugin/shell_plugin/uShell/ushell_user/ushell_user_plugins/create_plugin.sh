#!/bin/bash

if [ -z "$1" ]; then
    echo "plugin name missing.."
else
    #convert to lowercase
    plugin_name=$1
    use_name=${plugin_name,,}

    echo "Creating ${use_name} ..."
    cp -rf template_plugin ${use_name}_plugin
    toreplace=${use_name}

    sed -i -e "s/template/${toreplace}/g" ${use_name}_plugin/CMakeLists.txt
    echo "add_subdirectory(${use_name}_plugin)" >> CMakeLists.txt

    echo "$plugin_name plugin created ok.."
    echo "Installing the plugin.. "
    sed -i "s/\(set(PLUGINS_LIST.*\))$/\1 ${plugin_name}_plugin)/" ../../CMakeLists.txt
fi



