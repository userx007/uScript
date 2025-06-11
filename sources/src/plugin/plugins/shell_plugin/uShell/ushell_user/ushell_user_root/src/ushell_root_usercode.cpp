#include "ushell_core.h"
#include "ushell_user_logger.h"

// used from script components
#include "uPluginLoader.hpp"  // share the plugin loader component used by the script component
#include "CommonSettings.hpp" // get paths configured by the script component

#if (1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)
#include <cstring>
#include <memory>
#include <unistd.h>
#if defined(_MSC_VER)
    #include <dirent_vs.h>
#else
    #include <dirent.h>
#endif

#endif /*(1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)*/

///////////////////////////////////////////////////////////////////
//            EXPORTED VARIABLES DECLARATION                     //
///////////////////////////////////////////////////////////////////

#if (1 == uSHELL_SUPPORTS_EXTERNAL_USER_DATA)
    void *pvLocalUserData = nullptr;
#endif /* (1 == uSHELL_SUPPORTS_EXTERNAL_USER_DATA) */


///////////////////////////////////////////////////////////////////
//       PLUGIN RELATED  PUBLIC INTERFACES IMPLEMENTATION        //
///////////////////////////////////////////////////////////////////


#if (1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)

/*------------------------------------------------------------
 * list all the available plugins
------------------------------------------------------------*/
int list(void)
{
    #define MAX_WORKBUFFER_SIZE    (256)
    char vstrPluginPathName[MAX_WORKBUFFER_SIZE] = {0};
    struct dirent *entry = nullptr;
    DIR *dir = opendir(SHELL_PLUGINS_PATH);

    if (NULL == dir) {
        uSHELL_LOG(LOG_ERROR, "Failed to open the plugins folder [%s]", SHELL_PLUGINS_PATH);
        return 1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, SHELL_PLUGIN_EXTENSION) != NULL) {
            size_t name_len = strlen(entry->d_name);
            size_t ext_len = strlen(SHELL_PLUGIN_EXTENSION);

            // Ensure safe modification of string
            if (name_len > ext_len) {
                entry->d_name[name_len - ext_len] = '\0';
            }

            // Secure snprintf usage with truncation check
            int written = snprintf(vstrPluginPathName, MAX_WORKBUFFER_SIZE, "%30s%s | %s", entry->d_name, SHELL_PLUGIN_EXTENSION, entry->d_name + strlen(PLUGIN_PREFIX));

            if (written >= MAX_WORKBUFFER_SIZE) {
                uSHELL_LOG(LOG_WARNING, "Plugin name truncated: [%s]", vstrPluginPathName);
            }

            uSHELL_LOG(LOG_INFO, "%s", vstrPluginPathName);
        }
    }

    closedir(dir);

    return 0;

} /* list() */



/*------------------------------------------------------------
 * load the plugin
------------------------------------------------------------*/
int pload(char *pstrPluginName)
{
    PluginLoaderFunctor<uShellInst_s> loader(PluginPathGenerator(SHELL_PLUGINS_PATH, PLUGIN_PREFIX, SHELL_PLUGIN_EXTENSION),
                                             PluginEntryPointResolver(SHELL_PLUGIN_ENTRY_POINT_NAME, SHELL_PLUGIN_EXIT_POINT_NAME));

    auto handle = loader(pstrPluginName);

    if (handle.first && handle.second) {
        uSHELL_LOG(LOG_INFO, "Plugin loaded successfully!");
    } else {
        uSHELL_LOG(LOG_ERROR, "Failed to load plugin");
    }

    auto typedPtr = std::static_pointer_cast<uShellInst_s>(handle.second);
    uShellInst_s* rawPtr = typedPtr.get();

    /* continue execution with the valid shell instance */
    std::shared_ptr<Microshell> pShellPtr = Microshell::getShellSharedPtr(rawPtr, pstrPluginName);

    /* this call is blocking until the shell is released with #q */
    if (nullptr != pShellPtr) {
        pShellPtr->Run();
    }

    return 0;

} /* pload() */

#endif /* (1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES) */



///////////////////////////////////////////////////////////////////
//                 USER COMMANDS IMPLEMENTATION                  //
///////////////////////////////////////////////////////////////////

int vtest ( void )
{
    uSHELL_LOG(LOG_INFO, "vtest called ...");

    return 1;

} /* vtest */


///////////////////////////////////////////////////////////////////
//               USER SHORTCUTS HANDLERS                         //
///////////////////////////////////////////////////////////////////


#if (1 == uSHELL_IMPLEMENTS_USER_SHORTCUTS)

void uShellUserHandleShortcut_Dot( const char *pstrArgs )
{
    uSHELL_LOG(LOG_WARNING, "[.] registered but not implemented | args[%s] ", pstrArgs);

} /* uShellUserHandleShortcut_Dot() */


/******************************************************************************/
void uShellUserHandleShortcut_Slash( const char *pstrArgs )
{
    uSHELL_LOG(LOG_WARNING, "[/] registered but not implemented | args[%s] ", pstrArgs);

} /* uShellUserHandleShortcut_Slash() */

#endif /*(1 == uSHELL_IMPLEMENTS_USER_SHORTCUTS)*/
