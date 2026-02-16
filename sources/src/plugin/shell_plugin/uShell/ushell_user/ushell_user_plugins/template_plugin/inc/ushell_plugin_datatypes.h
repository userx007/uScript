#ifndef USHELL_PLUGIN_DATATYPES_H
#define USHELL_PLUGIN_DATATYPES_H

#include "ushell_core_settings.h"

#define uSHELL_COMMANDS_CONFIG_FILE                   "ushell_plugin_commands.cfg"
#if (1 == uSHELL_IMPLEMENTS_USER_SHORTCUTS)
    #define uSHELL_USER_SHORTCUTS_CONFIG_FILE         "ushell_plugin_shortcuts.cfg"
#endif /*(1 == uSHELL_IMPLEMENTS_USER_SHORTCUTS)*/

#include "ushell_core_datatypes_user.h"

#if (1 == uSHELL_SUPPORTS_EXTERNAL_USER_DATA)
    /** pointer to the user data structure */
    extern void *pvLocalUserData;
#endif /*(1 == uSHELL_SUPPORTS_EXTERNAL_USER_DATA)*/

#endif /* USHELL_PLUGIN_DATATYPES_H */