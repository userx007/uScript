#ifndef USHELL_CORE_SETTINGS_H
#define USHELL_CORE_SETTINGS_H

#include <stdint.h>
#include <stdbool.h>

/* script mode will disable different settings, see below */
#define uSHELL_SCRIPT_MODE                       0

/* user-app settings */
#define uSHELL_SUPPORTS_MULTIPLE_INSTANCES       1  /* allow a nested shell for plugins */
#define uSHELL_SUPPORTS_EXTERNAL_USER_DATA       1  /* allow the shell to access external data */
#define uSHELL_SUPPORTS_COMMAND_AS_PARAMETER     0  /* enable Execute(command) interface */

/* major features */
#define uSHELL_IMPLEMENTS_HISTORY                1
#define uSHELL_IMPLEMENTS_SAVE_HISTORY           1
#define uSHELL_IMPLEMENTS_AUTOCOMPLETE           1
#define uSHELL_IMPLEMENTS_EDITMODE               1
#define uSHELL_IMPLEMENTS_SMART_PROMPT           1
#define uSHELL_IMPLEMENTS_COMMAND_HELP           1
#define uSHELL_IMPLEMENTS_USER_SHORTCUTS         1

/* minor features */
#define uSHELL_SUPPORTS_COLORS                   1
#define uSHELL_IMPLEMENTS_SHELL_EXIT             1
#define uSHELL_IMPLEMENTS_CONFIRM_REQUEST        0
#define uSHELL_IMPLEMENTS_DISABLE_ECHO           0
/* utilities */
#define uSHELL_IMPLEMENTS_DUMP                   0
#define uSHELL_IMPLEMENTS_KEY_DECODER            0
#define uSHELL_IMPLEMENTS_HEXLIFY                1
/* data types */
#define uSHELL_SUPPORTS_NUMBERS_64BIT            1  /* l (long)   */
#define uSHELL_SUPPORTS_NUMBERS_32BIT            1  /* i (int)    */
#define uSHELL_SUPPORTS_NUMBERS_16BIT            0  /* w (word)   */
#define uSHELL_SUPPORTS_NUMBERS_8BIT             0  /* b (byte)   */
#define uSHELL_SUPPORTS_NUMBERS_FLOAT            0  /* f (float)  */
#define uSHELL_SUPPORTS_STRINGS                  1  /* s (string) */
#define uSHELL_SUPPORTS_BOOLEAN                  1  /* o (bool)   */
#if (1 == uSHELL_SUPPORTS_STRINGS)
#define uSHELL_SUPPORTS_SPACED_STRINGS           1
#endif /*(1 == uSHELL_SUPPORTS_STRINGS)*/
#define uSHELL_SUPPORTS_SIGNED_TYPES             0
/* max number of params of type */
#define uSHELL_MAX_PARAMS_NUM64                  (1U)
#define uSHELL_MAX_PARAMS_NUM32                  (5U)
#define uSHELL_MAX_PARAMS_NUM16                  (0U)
#define uSHELL_MAX_PARAMS_NUM8                   (0U)
#define uSHELL_MAX_PARAMS_FLOAT                  (0U)
#define uSHELL_MAX_PARAMS_STRING                 (5U)
#define uSHELL_MAX_PARAMS_BOOLEAN                (1U)
/* implementation specific */
#define uSHELL_MAX_INPUT_BUF_LEN                 (128U)
#define uSHELL_PROMPT_MAX_LEN                    (20U)
#define uSHELL_HISTORY_DEPTH                     (100U)
#if (1 == uSHELL_SUPPORTS_COLORS)
#define uSHELL_PROMPT_COLOR                      "\033[96m"     // Bright Cyan
#define uSHELL_INFO_HEADER_COLOR                 "\033[94m"     // Bright Blue
#define uSHELL_INFO_BODY_COLOR                   "\033[93m"     // Bright Yellow
#define uSHELL_INFO_LIST_COLOR                   "\033[95m"     // Bright Magenta
#define uSHELL_SUCCESS_COLOR                     "\033[92m"     // Bright Green
#define uSHELL_VERBOSE_COLOR                     "\033[33m"     // Yellow
#define uSHELL_DEBUG_COLOR                       "\033[94m"     // Bright Blue
#define uSHELL_INFO_COLOR                        "\033[97m"     // Bright White
#define uSHELL_WARNING_COLOR                     "\033[35m"     // Magenta
#define uSHELL_ERROR_COLOR                       "\033[91m"     // Bright Red
#define uSHELL_RESET_COLOR                       "\033[0m"      // Reset to default
#else /* (0 == uSHELL_SUPPORTS_COLORS) */
#define uSHELL_PROMPT_COLOR                      ""
#define uSHELL_INFO_HEADER_COLOR                 ""
#define uSHELL_INFO_BODY_COLOR                   ""
#define uSHELL_INFO_LIST_COLOR                   ""
#define uSHELL_SUCCESS_COLOR                     ""
#define uSHELL_VERBOSE_COLOR                     ""
#define uSHELL_DEBUG_COLOR                       ""
#define uSHELL_INFO_COLOR                        ""
#define uSHELL_WARNING_COLOR                     ""
#define uSHELL_ERROR_COLOR                       ""
#define uSHELL_RESET_COLOR                       ""
#endif /* (1 == uSHELL_SUPPORTS_COLORS) */

/* disable history if depth is 0 */
#if (0 == uSHELL_HISTORY_DEPTH)
    #undef  uSHELL_IMPLEMENTS_HISTORY
    #define uSHELL_IMPLEMENTS_HISTORY            0
#endif /*(0 == uSHELL_HISTORY_DEPTH)*/

/* disable save history if the history itself is disabled */
#if (0 == uSHELL_IMPLEMENTS_HISTORY)
    #undef  uSHELL_IMPLEMENTS_SAVE_HISTORY
    #define uSHELL_IMPLEMENTS_SAVE_HISTORY       0
#endif /*(0 == uSHELL_IMPLEMENTS_HISTORY)*/

/* script mode will disable all the "exotic" features */
#if (1 == uSHELL_SCRIPT_MODE)
    #undef  uSHELL_IMPLEMENTS_HISTORY
    #define uSHELL_IMPLEMENTS_HISTORY            0
    #undef  uSHELL_IMPLEMENTS_SAVE_HISTORY
    #define uSHELL_IMPLEMENTS_SAVE_HISTORY       0
    #undef  uSHELL_IMPLEMENTS_AUTOCOMPLETE
    #define uSHELL_IMPLEMENTS_AUTOCOMPLETE       0
    #undef  uSHELL_IMPLEMENTS_EDITMODE
    #define uSHELL_IMPLEMENTS_EDITMODE           0
    #undef  uSHELL_SUPPORTS_SPACED_STRINGS
    #define uSHELL_SUPPORTS_SPACED_STRINGS       0
    #undef  uSHELL_IMPLEMENTS_COMMAND_HELP
    #define uSHELL_IMPLEMENTS_COMMAND_HELP       0
    #undef  uSHELL_IMPLEMENTS_KEY_DECODER
    #define uSHELL_IMPLEMENTS_KEY_DECODER        0
    #undef  uSHELL_IMPLEMENTS_CONFIRM_REQUEST
    #define uSHELL_IMPLEMENTS_CONFIRM_REQUEST    0
    #undef  uSHELL_IMPLEMENTS_SHELL_EXIT
    #define uSHELL_IMPLEMENTS_SHELL_EXIT         0
    #undef  uSHELL_IMPLEMENTS_DISABLE_ECHO
    #define uSHELL_IMPLEMENTS_DISABLE_ECHO       0
    #undef  uSHELL_IMPLEMENTS_DUMP
    #define uSHELL_IMPLEMENTS_DUMP               0
    #undef  uSHELL_SUPPORTS_COLORS
    #define uSHELL_SUPPORTS_COLORS               0
    #undef  uSHELL_SUPPORTS_MULTIPLE_INSTANCES
    #define uSHELL_SUPPORTS_MULTIPLE_INSTANCES   0
    #undef  uSHELL_SUPPORTS_EXTERNAL_USER_DATA
    #define uSHELL_SUPPORTS_EXTERNAL_USER_DATA   0
#endif /* (1 == uSHELL_SCRIPT_MODE) */

/* if not explicitely disabled then enable edit mode if autocompl and history are disabled */
#if ((0 == uSHELL_IMPLEMENTS_AUTOCOMPLETE) && (0 == uSHELL_IMPLEMENTS_HISTORY) && (1 == uSHELL_IMPLEMENTS_EDITMODE))
    #define uSHELL_EDIT_MODE_DEFAULT_ACTIVE
#endif /*((0 == uSHELL_IMPLEMENTS_AUTOCOMPLETE) && (0 == uSHELL_IMPLEMENTS_HISTORY) && (1 == uSHELL_IMPLEMENTS_EDITMODE))*/

/* smart prompt is useless when all features whose status is shown in prompt are disabled */
#if ((0 == uSHELL_IMPLEMENTS_AUTOCOMPLETE) && (0 == uSHELL_IMPLEMENTS_HISTORY) && (0 == uSHELL_IMPLEMENTS_EDITMODE))
    #undef uSHELL_IMPLEMENTS_SMART_PROMPT
    #define uSHELL_IMPLEMENTS_SMART_PROMPT       0
#endif /*((0 == uSHELL_IMPLEMENTS_AUTOCOMPLETE) && (0 == uSHELL_IMPLEMENTS_HISTORY)) && (0 == uSHELL_IMPLEMENTS_EDITMODE)*/

#if ((0 == uSHELL_IMPLEMENTS_HISTORY) && (0 == uSHELL_IMPLEMENTS_SHELL_EXIT))
    #undef uSHELL_IMPLEMENTS_CONFIRM_REQUEST
    #define uSHELL_IMPLEMENTS_CONFIRM_REQUEST    0
#endif /* ((0 == uSHELL_IMPLEMENTS_HISTORY) && (0 == uSHELL_IMPLEMENTS_SHELL_EXIT)) */

#if (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)
    #define uSHELL_INIT_AUTOCOMPL_MODE           true /*true:on, false:off*/
    #define uSHELL_AUTOCOMPL_RELOAD              true
#endif /*(1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)*/
#if (1 == uSHELL_IMPLEMENTS_HISTORY)
    #define uSHELL_INIT_HISTORY_MODE             true /*true:on, false:off*/
#endif /*(1 == uSHELL_IMPLEMENTS_HISTORY)*/

#if (1 == uSHELL_SUPPORTS_NUMBERS_64BIT)
    #define uSHELL_MAX_VALUE_64BIT               (0xFFFFFFFFFFFFFFFFU)
    typedef uint64_t num64_t;
    #define BIGNUM_T uint64_t
    #if (uSHELL_MAX_PARAMS_NUM64 > 0)
        #define uSHELL_IMPLEMENTS_NUMBERS_64BIT
    #endif /* #if (uSHELL_MAX_PARAMS_NUM64 > 0)*/
#endif /* #if (1 == uSHELL_SUPPORTS_NUMBERS_64BIT)*/

#if (1 == uSHELL_SUPPORTS_NUMBERS_32BIT)
    #define uSHELL_MAX_VALUE_32BIT               (0xFFFFFFFFU)
    typedef uint32_t num32_t;
    #if !defined(BIGNUM_T)
        #define BIGNUM_T uint32_t
    #endif /* !defined(BIGNUM_T) */
    #if (uSHELL_MAX_PARAMS_NUM32 > 0)
        #define uSHELL_IMPLEMENTS_NUMBERS_32BIT
    #endif /* #if (uSHELL_MAX_PARAMS_NUM32 > 0)*/
#endif /* #if (1 == uSHELL_SUPPORTS_NUMBERS_32BIT)*/

#if (1 == uSHELL_SUPPORTS_NUMBERS_16BIT)
    #define uSHELL_MAX_VALUE_16BIT               (0xFFFFU)
    typedef uint16_t num16_t;
    #if !defined(BIGNUM_T)
        #define BIGNUM_T uint16_t
    #endif /* !defined(BIGNUM_T) */
    #if (uSHELL_MAX_PARAMS_NUM16 > 0)
        #define uSHELL_IMPLEMENTS_NUMBERS_16BIT
    #endif /* #if (uSHELL_MAX_PARAMS_NUM16 > 0)*/
#endif /* #if (1 == uSHELL_SUPPORTS_NUMBERS_16BIT)*/

#if (1 == uSHELL_SUPPORTS_NUMBERS_8BIT)
    #define uSHELL_MAX_VALUE_8BIT                (0xFFU)
    typedef uint8_t num8_t;
    #if !defined(BIGNUM_T)
        #define BIGNUM_T uint8_t
    #endif /* !defined(BIGNUM_T) */
    #if (uSHELL_MAX_PARAMS_NUM8 > 0)
        #define uSHELL_IMPLEMENTS_NUMBERS_8BIT
    #endif /* #if (uSHELL_MAX_PARAMS_NUM8 > 0)*/
#endif /* #if (1 == uSHELL_SUPPORTS_NUMBERS_8BIT)*/

#if (1 == uSHELL_SUPPORTS_NUMBERS_FLOAT)
    typedef float numfp_t;
    #if (uSHELL_MAX_PARAMS_FLOAT > 0)
        #define uSHELL_IMPLEMENTS_NUMBERS_FLOAT
    #endif /* #if (uSHELL_MAX_PARAMS_FLOAT > 0)*/
#endif /* #if (1 == uSHELL_SUPPORTS_NUMBERS_FLOAT)*/

#if (1 == uSHELL_SUPPORTS_STRINGS)
    typedef char str_t;
    #if (uSHELL_MAX_PARAMS_STRING > 0)
        #define uSHELL_IMPLEMENTS_STRINGS
    #endif /* #if (uSHELL_MAX_PARAMS_STRING > 0)*/
#endif /* #if (1 == uSHELL_SUPPORTS_STRINGS)*/

#if (1 == uSHELL_SUPPORTS_BOOLEAN)
    #define uSHELL_MAX_VALUE_BOOLEAN             (1U)
    #if (uSHELL_MAX_PARAMS_BOOLEAN > 0)
        #define uSHELL_IMPLEMENTS_BOOLEAN
    #endif /* #if (uSHELL_MAX_PARAMS_BOOLEAN > 0)*/
#endif /* #if (1 == uSHELL_SUPPORTS_BOOLEAN)*/

#if !(defined(__linux__) || defined(__MINGW32__) || defined(_MSC_VER))
    #undef uSHELL_IMPLEMENTS_SAVE_HISTORY
    #define uSHELL_IMPLEMENTS_SAVE_HISTORY 0
#endif /*defined(__linux__) || defined(__MINGW32__) || defined(_MSC_VER)*/

/* useful macros */
#define uSHELL_NR_ELEMS(a) ((int)(sizeof(a)/sizeof(a[0])))

/* configuration files */
#define uSHELL_DATA_TYPES_CONFIG_FILE  "ushell_core_datatypes.cfg"
#define uSHELL_PROMPT_CONFIG_FILE      "ushell_core_prompt.cfg"

#endif /* USHELL_CORE_SETTINGS_H */
