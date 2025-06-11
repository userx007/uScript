#ifndef SCRIPT_SETTINGS_HPP
#define SCRIPT_SETTINGS_HPP


// configuration
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


#define    PLUGIN_PATH                         "plugins/"     // script plugins path
#define    IPLUGIN_PATH                        "iplugins/"    // shell  plugins path
#define    PLUGIN_PREFIX                       "lib"
#ifdef _WIN32
#define    PLUGIN_EXTENSION                   "_plugin.dll"   // script plugin extension (win32)
#define    IPLUGIN_EXTENSION                  "_iplugin.dll"  // shell plugin extension  (win32)
#else // linux
#define    PLUGIN_EXTENSION                   "_plugin.so"    // script plugin extension (linux)
#define    IPLUGIN_EXTENSION                  "_iplugin.so"   // shell plugin extension  (linux)
#endif // _WIN32

#define    PLUGIN_ENTRY_POINT_NAME            "pluginEntry"
#define    PLUGIN_EXIT_POINT_NAME             "pluginExit"

#define    IPLUGIN_ENTRY_POINT_NAME           "uShellPluginEntry"
#define    IPLUGIN_EXIT_POINT_NAME            "uShellPluginExit"


#endif /* SCRIPT_SETTINGS_HPP */