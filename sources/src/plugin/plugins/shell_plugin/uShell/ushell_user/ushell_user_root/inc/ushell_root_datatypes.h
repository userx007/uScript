#pragma once

#include "ushell_core_settings.h"

#define uSHELL_COMMANDS_CONFIG_FILE               "ushell_root_commands.cfg"
#if (1 == uSHELL_IMPLEMENTS_USER_SHORTCUTS)
    #define uSHELL_USER_SHORTCUTS_CONFIG_FILE    "ushell_root_shortcuts.cfg"
#endif /*(1 == uSHELL_IMPLEMENTS_USER_SHORTCUTS)*/

/* shell generated types and structures */
#define  uSHELL_COMMANDS_TABLE_BEGIN
#define  uSHELL_COMMAND_PARAMS_PATTERN(t)
#define  uSHELL_COMMAND(a,b,c)
#define  uSHELL_COMMANDS_TABLE_END
#include uSHELL_COMMANDS_CONFIG_FILE      /* define function parameter list */
#undef   uSHELL_COMMAND_PARAMS_PATTERN
#define  uSHELL_COMMAND_PARAMS_PATTERN(t)   typedef int (*t##_fctptr_t)(t##_params);
#include uSHELL_COMMANDS_CONFIG_FILE      /* define function pointer typedefs */
#undef   uSHELL_COMMAND
#undef   uSHELL_COMMAND_PARAMS_PATTERN
#define  uSHELL_COMMAND_PARAMS_PATTERN(t)
#define  uSHELL_COMMAND(a,b,c)              extern int a(b##_params);
#include uSHELL_COMMANDS_CONFIG_FILE      /* define function effective prototypes */
#undef   uSHELL_COMMANDS_TABLE_BEGIN
#undef   uSHELL_COMMAND_PARAMS_PATTERN
#undef   uSHELL_COMMAND
#undef   uSHELL_COMMANDS_TABLE_END

/** \brief define union of prototypes */
#define  uSHELL_COMMANDS_TABLE_BEGIN        typedef union fctype_u_{
#undef   uSHELL_COMMAND_PARAMS_PATTERN
#define  uSHELL_COMMAND_PARAMS_PATTERN(t)       t##_fctptr_t  t##_fct;
#define  uSHELL_COMMAND(a,b,c)
#define  uSHELL_COMMANDS_TABLE_END          }fctype_u;
#include uSHELL_COMMANDS_CONFIG_FILE
#undef   uSHELL_COMMANDS_TABLE_BEGIN
#undef   uSHELL_COMMAND_PARAMS_PATTERN
#undef   uSHELL_COMMAND
#undef   uSHELL_COMMANDS_TABLE_END

/** \brief function types (parameter pattern) enumeration */
#define  uSHELL_COMMANDS_TABLE_BEGIN        typedef enum fctype_e_{
#undef   uSHELL_COMMAND_PARAMS_PATTERN
#define  uSHELL_COMMAND_PARAMS_PATTERN(t)       t##_type,
#define  uSHELL_COMMAND(a,b,c)
#define  uSHELL_COMMANDS_TABLE_END          LAST_PARAMS_PATTERN } fctype_e;
#include uSHELL_COMMANDS_CONFIG_FILE
#undef   uSHELL_COMMANDS_TABLE_BEGIN
#undef   uSHELL_COMMAND_PARAMS_PATTERN
#undef   uSHELL_COMMAND
#undef   uSHELL_COMMANDS_TABLE_END

/** \brief local data types */
typedef struct fctDefEx_s_ {
    fctype_u    uFctType;
    fctype_e    eParamType;
} fctDefEx_s;

#if (1 == uSHELL_SUPPORTS_EXTERNAL_USER_DATA)
    /** pointer to the user data structure */
    extern void *pvLocalUserData;
#endif /*(1 == uSHELL_SUPPORTS_EXTERNAL_USER_DATA)*/
