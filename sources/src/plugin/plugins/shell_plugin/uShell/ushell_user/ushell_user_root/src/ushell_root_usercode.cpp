#include "ushell_core.h"
#include "ushell_user_plugin_loader.h"
#include "ushell_user_logger.h"

#if (1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)
#include <cstring>
#include <memory>

#if defined(_MSC_VER)
    #include <dirent_vs.h>
#else
    #include <dirent.h>
#endif

#ifdef _WIN32
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif /* _WIN32 */
#endif /*(1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)*/

///////////////////////////////////////////////////////////////////
//            LOCAL DEFINES AND DATA TYPES                       //
///////////////////////////////////////////////////////////////////

#if (1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)
    #define MAX_WORKBUFFER_SIZE    (256)

    #define PLUGINS_FOLDER         "plugins"
    #define PLUGINS_PREFIX         "lib"
    #define PLUGINS_POSTFIX        "_plugin"
    #ifdef _WIN32
        #define PLUGINS_EXTENSION  ".dll"
    #else
        #define PLUGINS_EXTENSION  ".so"
    #endif
#endif /* (1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES) */

///////////////////////////////////////////////////////////////////
//            PRIVATE INTERFACES DECLARATION                     //
///////////////////////////////////////////////////////////////////



///////////////////////////////////////////////////////////////////
//            EXPORTED VARIABLES DECLARATION                     //
///////////////////////////////////////////////////////////////////

#if (1 == uSHELL_SUPPORTS_EXTERNAL_USER_DATA)
    void *pvLocalUserData = nullptr;
#endif /* (1 == uSHELL_SUPPORTS_EXTERNAL_USER_DATA) */

///////////////////////////////////////////////////////////////////
//                 USER COMMANDS IMPLEMENTATION                  //
///////////////////////////////////////////////////////////////////

int vtest ( void )
{
    uSHELL_LOG(LOG_INFO, "vtest called ...");

    return 1;

} /* vtest */


///////////////////////////////////////////////////////////////////
//       PLUGIN RELATED  PUBLIC INTERFACES IMPLEMENTATION        //
///////////////////////////////////////////////////////////////////


#if (1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)
/*------------------------------------------------------------
 * list all the available plugins
------------------------------------------------------------*/

#include <cstring>
#include <cstdio>
#include <dirent.h>
#include <unistd.h>


int list(void)
{
    char vstrPluginPathName[MAX_WORKBUFFER_SIZE] = {0};
    struct dirent *entry = nullptr;
    DIR *dir = opendir(PLUGINS_FOLDER);

    if (NULL == dir) {
        uSHELL_LOG(LOG_ERROR, "Failed to open the plugins folder [%s]", PLUGINS_FOLDER);
        return 1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, PLUGINS_EXTENSION) != NULL) {
            size_t name_len = strlen(entry->d_name);
            size_t ext_len = strlen(PLUGINS_EXTENSION);
            size_t postfix_len = strlen(PLUGINS_POSTFIX);

            // Ensure safe modification of string
            if (name_len > (ext_len + postfix_len)) {
                entry->d_name[name_len - ext_len - postfix_len] = '\0';
            }

            // Secure snprintf usage with truncation check
            int written = snprintf(vstrPluginPathName, MAX_WORKBUFFER_SIZE, "%30s%s%s | %s", entry->d_name, PLUGINS_POSTFIX, PLUGINS_EXTENSION, entry->d_name + strlen(PLUGINS_PREFIX));

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

    PluginLoaderFunctor<uShellInst_s> loader;
    auto handle = loader(pstrPluginName);

    if (handle.first && handle.second) {
        uSHELL_LOG(LOG_INFO, "Plugin loaded successfully!");
    } else {
        uSHELL_LOG(LOG_ERROR, "Failed to load plugin.");
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
//            PRIVATE INTERFACES IMPLEMENTATION                  //
///////////////////////////////////////////////////////////////////



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
