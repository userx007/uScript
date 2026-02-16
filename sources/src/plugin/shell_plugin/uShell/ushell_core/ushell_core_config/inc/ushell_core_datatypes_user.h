#ifndef USHELL_CORE_DATATYPES_USER_H
#define USHELL_CORE_DATATYPES_USER_H

#include "ushell_core_settings.h"

#if (1 == uSHELL_IMPLEMENTS_USER_SHORTCUTS)
#define  uSHELL_USER_SHORTCUTS_TABLE_BEGIN
#define  uSHELL_USER_SHORTCUT(a,b,c)                void uShellUserHandleShortcut_##b( const char *pstrArgs );
#define  uSHELL_USER_SHORTCUTS_TABLE_END
#include uSHELL_USER_SHORTCUTS_CONFIG_FILE
#undef   uSHELL_USER_SHORTCUTS_TABLE_BEGIN
#undef   uSHELL_USER_SHORTCUT
#undef   uSHELL_USER_SHORTCUTS_TABLE_END
#endif /*(1 == uSHELL_IMPLEMENTS_USER_SHORTCUTS)*/

#define  uSHELL_COMMANDS_TABLE_BEGIN
#define  uSHELL_COMMAND_PARAMS_PATTERN(t)
#define  uSHELL_COMMAND(a,b,c)
#define  uSHELL_COMMANDS_TABLE_END
#include uSHELL_COMMANDS_CONFIG_FILE                /* generate the functions's list of parameters */
#undef   uSHELL_COMMAND_PARAMS_PATTERN

#define  uSHELL_COMMAND_PARAMS_PATTERN(t)           typedef int (*t##_fctptr_t)(t##_params);
#include uSHELL_COMMANDS_CONFIG_FILE                /* functions's pointer type */
#undef   uSHELL_COMMAND
#undef   uSHELL_COMMAND_PARAMS_PATTERN
#define  uSHELL_COMMAND_PARAMS_PATTERN(t)
#define  uSHELL_COMMAND(a,b,c)                      extern int a(b##_params);
#include uSHELL_COMMANDS_CONFIG_FILE                /* functions's prototypes */
#undef   uSHELL_COMMANDS_TABLE_BEGIN
#undef   uSHELL_COMMAND_PARAMS_PATTERN
#undef   uSHELL_COMMAND
#undef   uSHELL_COMMANDS_TABLE_END

#define  uSHELL_COMMANDS_TABLE_BEGIN                typedef union fctype_u_{
#undef   uSHELL_COMMAND_PARAMS_PATTERN
#define  uSHELL_COMMAND_PARAMS_PATTERN(t)               t##_fctptr_t  t##_fct;
#define  uSHELL_COMMAND(a,b,c)
#define  uSHELL_COMMANDS_TABLE_END                  } fctype_u;
#include uSHELL_COMMANDS_CONFIG_FILE
#undef   uSHELL_COMMANDS_TABLE_BEGIN
#undef   uSHELL_COMMAND_PARAMS_PATTERN
#undef   uSHELL_COMMAND
#undef   uSHELL_COMMANDS_TABLE_END

#define  uSHELL_COMMANDS_TABLE_BEGIN                typedef enum fctype_e_{
#undef   uSHELL_COMMAND_PARAMS_PATTERN
#define  uSHELL_COMMAND_PARAMS_PATTERN(t)               t##_type,
#define  uSHELL_COMMAND(a,b,c)
#define  uSHELL_COMMANDS_TABLE_END                  LAST_PARAMS_PATTERN } fctype_e;
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

#endif /* USHELL_CORE_DATATYPES_USER_H */