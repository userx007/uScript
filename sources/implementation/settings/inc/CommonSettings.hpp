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

// plugin operations
#define    PLUGIN_COMMAND_THREADED                      "&"

// script related keywords in the ini file
#define    SCRIPT_INI_SECTION_NAME                      "SCRIPT"
#define    SCRIPT_INI_CMD_EXEC_DELAY                    "CMD_EXEC_DELAY"

// common plugin related keywords in the ini file
#define    PLUGIN_INI_FAULT_TOLERANT                    "FAULT_TOLERANT"
#define    PLUGIN_INI_PRIVILEGED                        "PRIVILEGED"

// separators
#define    CHAR_SEPARATOR_VERTICAL_BAR                  '|'
#define    CHAR_SEPARATOR_COLON                         ':'
#define    CHAR_SEPARATOR_SPACE                         ' '
#define    CHAR_SEPARATOR_COMMA                         ','
#define    CHAR_SEPARATOR_DOT                           '.'
#define    CHAR_SEPARATOR_NEWLINE                       '\n'


// decorators
#define    DECORATOR_FILENAME_START                     "F\""   // filename
#define    DECORATOR_REGEX_START                        "R\""   // regex
#define    DECORATOR_HEXLIFY_START                      "H\""   // hexvalues as string AA3F2CBF
#define    DECORATOR_TOKEN_START                        "T\""   // token (subsequence of values)
#define    DECORATOR_LINE_START                         "L\""   // line (teminated with \n), in principle to be read as we don't know the expected content but read until \n is found
#define    DECORATOR_SIZE_START                         "S\""   // size (number of bytes to be read)
#define    DECORATOR_STRING_START                       "\""    // standard string
#define    DECORATOR_ANY_END                            "\""

// sizes
#define    PLUGIN_DEFAULT_FILEREAD_CHUNKSIZE            1024U
#define    PLUGIN_DEFAULT_RECEIVE_SIZE                  1024U
#define    PLUGIN_SCRIPT_DEFAULT_CMDS_DELAY                0U

// intervals
#define    PLUGIN_DEFAULT_UARTMON_POLLING_INTERVAL       100U


#endif /* COMMON_SETTINGS_HPP */