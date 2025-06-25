#ifndef COMMON_SETTINGS_HPP
#define COMMON_SETTINGS_HPP


// configuration
#define    SCRIPT_DEFAULT                     "script.txt"
#define    SCRIPT_INI_CONFIG                  "uscript.ini"

// comments
#define    SCRIPT_LINE_COMMENT                 '#'
#define    SCRIPT_BEGIN_BLOCK_COMMENT          "---"
#define    SCRIPT_END_BLOCK_COMMENT            "!--"

// separators
#define    SCRIPT_CONSTANT_MACRO_SEPARATOR     ":="
#define    SCRIPT_VARIABLE_MACRO_SEPARATOR     "?="
#define    SCRIPT_PLUGIN_COMMAND_SEPARATOR     "."
#define    SCRIPT_COMMAND_PARAMS_SEPARATOR     " "

// macro sign
#define    SCRIPT_MACRO_MARKER                 '$'

// TRUE FALSE declarations
#define    SCRIPT_COND_TRUE                    "TRUE"
#define    SCRIPT_COND_FALSE                   "FALSE"


#define    SCRIPT_PLUGINS_PATH                 "plugins/"
#define    SHELL_PLUGINS_PATH                  "iplugins/"
#define    PLUGIN_PREFIX                       "lib"
#ifdef _WIN32
#define    SCRIPT_PLUGIN_EXTENSION             "_plugin.dll"
#define    SHELL_PLUGIN_EXTENSION              "_iplugin.dll"
#else // linux
#define    SCRIPT_PLUGIN_EXTENSION             "_plugin.so"
#define    SHELL_PLUGIN_EXTENSION              "_iplugin.so"
#endif // _WIN32

#define    SCRIPT_PLUGIN_ENTRY_POINT_NAME      "pluginEntry"
#define    SCRIPT_PLUGIN_EXIT_POINT_NAME       "pluginExit"

#define    SHELL_PLUGIN_ENTRY_POINT_NAME       "uShellPluginEntry"
#define    SHELL_PLUGIN_EXIT_POINT_NAME        "uShellPluginExit"


#endif /* COMMON_SETTINGS_HPP */