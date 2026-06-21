#ifndef USHARED_CONFIG_HPP
#define USHARED_CONFIG_HPP


// configuration
#define    SCRIPT_DEFAULT                               "script.txt"
#define    SCRIPT_INI_CONFIG                            "uscript.ini"


// comments
#define    SCRIPT_LINE_COMMENT                          '#'
#define    SCRIPT_BEGIN_BLOCK_COMMENT                   "---"
#define    SCRIPT_END_BLOCK_COMMENT                     "!--"

// include directive (handled by ScriptReader before the IR stage)
// Format:   INCLUDE "relative/or/absolute/path"
// The path is resolved relative to the directory of the file that contains
// the directive. Circular / duplicate includes are detected and rejected.
// This constant is defined here so the GUI highlighter (ScriptHighlighter)
// can reference the same string without a dependency on uScriptReader.hpp.
#define    SCRIPT_INCLUDE_KEYWORD                       "INCLUDE"

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
#define    SCRIPT_PLUGINS_PATH                          "splugins/"
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

// keywords in the ini file
#define    COMMON_INI_SECTION_NAME                      "COMMON"
#define    SCRIPT_INI_SECTION_NAME                      "SCRIPT"
#define    LOGGING_INI_SECTION_NAME                     "LOGGING"

// COMMON / SCRIPT section keys
#define    SCRIPT_INI_CMD_EXEC_DELAY                    "CMD_EXEC_DELAY"

// LOGGING section keys
#define    LOG_INI_SEVERITY_CONSOLE                     "LOG_SEVERITY_CONSOLE"
#define    LOG_INI_SEVERITY_FILE                        "LOG_SEVERITY_FILE"
#define    LOG_INI_INCLUDE_DATE                         "LOG_INCLUDE_DATE"
#define    LOG_INI_CONSOLE_COLORED                      "LOG_CONSOLE_COLORED"
#define    LOG_INI_FILE_ENABLED                         "LOG_FILE_ENABLED"
#define    LOG_INI_INCLUDE_THREAD_ID                    "LOG_INCLUDE_THREAD_ID"


// common plugin related keywords in the ini file
#define    PLUGIN_INI_FAULT_TOLERANT                    "FAULT_TOLERANT"
#define    PLUGIN_INI_PRIVILEGED                        "PRIVILEGED"

// char separators
#define    CHAR_SEPARATOR_PIPE                          '|'
#define    CHAR_SEPARATOR_COLON                         ':'
#define    CHAR_SEPARATOR_SPACE                         ' '
#define    CHAR_SEPARATOR_COMMA                         ','
#define    CHAR_SEPARATOR_DOT                           '.'
#define    CHAR_SEPARATOR_NEWLINE                       '\n'

// string separators
#define    STRING_SEPARATOR_PIPE                        "|"

// decorators
#define    DECORATOR_FILENAME_START                     "F\""   // filename
#define    DECORATOR_REGEX_START                        "R\""   // regex
#define    DECORATOR_HEXLIFY_START                      "H\""   // hexvalues as string AA3F2CBF
#define    DECORATOR_TOKEN_STRING_START                 "T\""   // token string (substring in a string)
#define    DECORATOR_TOKEN_HEXSTREAM_START              "X\""   // token hexstream (sub-buffer in a buffer)
#define    DECORATOR_LINE_START                         "L\""   // line (teminated with \n), in principle to be read as we don't know the expected content but read until \n is found
#define    DECORATOR_SIZE_START                         "S\""   // size (number of bytes to be read)
#define    DECORATOR_STRING_START                       "\""    // standard string
#define    DECORATOR_ANY_END                            "\""

// time resolution
#define    TIME_MICROSECONDS                            "us"
#define    TIME_MILISECONDS                             "ms"
#define    TIME_SECONDS                                 "sec"

// sizes
#define    PLUGIN_DEFAULT_FILEREAD_CHUNKSIZE            1024U
#define    PLUGIN_DEFAULT_RECEIVE_SIZE                  1024U
#define    PLUGIN_SCRIPT_DEFAULT_CMDS_DELAY                0U

// intervals
#define    PLUGIN_DEFAULT_UARTMON_POLLING_INTERVAL       100U

// headers

#define    LOG_HEADER_CMACROS                            "___ CMACROS"
#define    LOG_HEADER_VMACROS                            "___ VMACROS"
#define    LOG_HEADER_ARRAYS                             "___ ARRAYS"
#define    LOG_HEADER_PLUGINS                            "___ PLUGINS"
#define    LOG_HEADER_COMMANDS                           "___ COMMANDS"

#endif /* USHARED_CONFIG_HPP */