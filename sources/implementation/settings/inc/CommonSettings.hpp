#ifndef COMMON_SETTINGS_HPP
#define COMMON_SETTINGS_HPP


// configuration
#define    SCRIPT_DEFAULT                               "script.txt"
#define    SCRIPT_INI_CONFIG                            "uscript.ini"


// comments
#define    SCRIPT_LINE_COMMENT                          '#'
#define    SCRIPT_BEGIN_BLOCK_COMMENT                   "---"
#define    SCRIPT_END_BLOCK_COMMENT                     "!--"

// separators
#define    SCRIPT_CONSTANT_MACRO_SEPARATOR              ":="
#define    SCRIPT_VARIABLE_MACRO_SEPARATOR              "?="
#define    SCRIPT_PLUGIN_COMMAND_SEPARATOR              "."
#define    SCRIPT_COMMAND_PARAMS_SEPARATOR              " "

// macro sign
#define    SCRIPT_MACRO_MARKER                          '$'

// TRUE FALSE declarations
#define    SCRIPT_COND_TRUE                             "TRUE"
#define    SCRIPT_COND_FALSE                            "FALSE"

// paths, prefixes, names, extensions
#define    SCRIPT_PLUGINS_PATH                          "plugins/"
#define    SHELL_PLUGINS_PATH                           "iplugins/"
#define    PLUGIN_PREFIX                                "lib"
#ifdef _WIN32
#define    SCRIPT_PLUGIN_EXTENSION                      "_plugin.dll"
#define    SHELL_PLUGIN_EXTENSION                       "_iplugin.dll"
#else // linux
#define    SCRIPT_PLUGIN_EXTENSION                      "_plugin.so"
#define    SHELL_PLUGIN_EXTENSION                       "_iplugin.so"
#endif // _WIN32

// plugin entry points
#define    SCRIPT_PLUGIN_ENTRY_POINT_NAME               "pluginEntry"
#define    SCRIPT_PLUGIN_EXIT_POINT_NAME                "pluginExit"
#define    SHELL_PLUGIN_ENTRY_POINT_NAME                "uShellPluginEntry"
#define    SHELL_PLUGIN_EXIT_POINT_NAME                 "uShellPluginExit"

// script related keywords in the ini file
#define    SCRIPT_INI_SECTION_NAME                      "SCRIPT"
#define    SCRIPT_INI_FAULT_TOLERANT                    "FAULT_TOLERANT"

// common plugin related keywords in the ini file
#define    PLUGIN_INI_FAULT_TOLERANT                    SCRIPT_INI_FAULT_TOLERANT
#define    PLUGIN_INI_PRIVILEGED                        "PRIVILEGED"

// separators
#define    CHAR_SEPARATOR_VERTICAL_BAR                  '|'
#define    CHAR_SEPARATOR_COLON                         ':'
#define    CHAR_SEPARATOR_SPACE                         ' '
#define    CHAR_SEPARATOR_COMMA                         ','

// decorators
#define    DECORATOR_FILENAME_START                     "F\""
#define    DECORATOR_REGEX_START                        "R\""
#define    DECORATOR_HEXLIFY_START                      "H\""
#define    DECORATOR_ANY_END                            "\""
#define    DECORATOR_STRING                             "\""


#endif /* COMMON_SETTINGS_HPP */