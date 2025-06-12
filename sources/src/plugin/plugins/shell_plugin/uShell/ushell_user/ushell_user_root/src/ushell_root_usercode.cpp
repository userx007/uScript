#include "ushell_core.h"
#include "ushell_core_keys.h"
#include "ushell_user_logger.h"

// used from script components
#include "uPluginLoader.hpp"
#include "CommonSettings.hpp" // get paths to plugins
#include "IScriptInterpreter.hpp"

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
//            INTERNAL INTERFACES                                //
///////////////////////////////////////////////////////////////////

static int privListPlugins      (const char *pstrCaption, const char *pstrPath, const char *pstrExtension);
static int privListScriptItems  (void);
static int privLoadScriptPlugin (char* pstrPluginName);
static int privExecScriptCommand(const char *pstrCommand);

///////////////////////////////////////////////////////////////////
//            EXPORTED VARIABLES DECLARATION                     //
///////////////////////////////////////////////////////////////////


#if (1 == uSHELL_SUPPORTS_EXTERNAL_USER_DATA)
void *pvLocalUserData = nullptr;
#endif /* (1 == uSHELL_SUPPORTS_EXTERNAL_USER_DATA) */


///////////////////////////////////////////////////////////////////
//            USER COMMANDS IMPLEMENTATION                       //
///////////////////////////////////////////////////////////////////


/*------------------------------------------------------------
 * list all the available plugins
------------------------------------------------------------*/
int list(void)
{

#if (1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)
    privListPlugins("shell", SHELL_PLUGINS_PATH, SHELL_PLUGIN_EXTENSION);
#endif /* (1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES) */
    privListPlugins("script", SCRIPT_PLUGINS_PATH, SCRIPT_PLUGIN_EXTENSION);
    return 0;

} /* list() */



/*------------------------------------------------------------
 * load the iplugin
------------------------------------------------------------*/
int ipload (char *pstrPluginName)
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

} /* ipload() */


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
    char *pstrArg = (char*)pstrArgs;

    do {

        // case: [..args] => load a plugin
        if( '.' == *pstrArg )
        {
            ++pstrArg;
            // skip the spaces
            while((uSHELL_KEY_SPACE == *pstrArg) && ('\0' != *pstrArg)) { ++pstrArg; }
            if( '\0' == *pstrArg )
            {
                uSHELL_PRINTF("[..] plugin name not provided!\n");
            }
            else
            {
                uSHELL_PRINTF("[..] loading plugin [%s]\n", pstrArg);
                privLoadScriptPlugin( pstrArg );
            }
            break;
        }

        // case: [.l] => list the script items
        if( ('l' == *pstrArg) && ('\0' == *(pstrArg + 1)) )
        {
            uSHELL_PRINTF("[.l] list script items\n");
            privListScriptItems();
            break;
        }

        // case: [.h] => show the help
        if( ('h' == *pstrArg) && ('\0' == *(pstrArg + 1)) )
        {
            uSHELL_PRINTF("\t[.h] help\n");
            uSHELL_PRINTF("\t[.p] list active plugins\n");
            uSHELL_PRINTF("\t[.m] list active macros\n");
            uSHELL_PRINTF("\t[.arg] execute the command provided as argument\n");
            uSHELL_PRINTF("\t[..arg] load the plugin provided as argument \n");
            break;
        }

        //default [.args] declare a macro or execute a command
        do {
            char *pstrCrtPos = pstrArg;

            // constant macro declaration, i.e:  "XXX := aa bb cc" is converted to uppercase until colon :
            if( NULL != strstr(pstrCrtPos, ":=") )
            {
                while( (uSHELL_KEY_COLON != *pstrCrtPos) && ('\0' != *pstrCrtPos) ) {
                    *pstrCrtPos = (char)toupper(*pstrCrtPos);
                    ++pstrCrtPos;
                }
                break;
            }

            // volatile macro initialized from a command execution i.e.  "XXX ?= FLASH.READ 100" (is converted to uppercase until dot .)
            if( NULL != strstr(pstrCrtPos, "?=") )
            {
                while( (uSHELL_KEY_DOT != *pstrCrtPos) && ('\0' != *pstrCrtPos) ) {
                    *pstrCrtPos = (char)toupper(*pstrCrtPos);
                    ++pstrCrtPos;
                }
            }

            // convert to uppercase until first space where the arguments starts (as they must remain unconverted)
            while( (uSHELL_KEY_SPACE != *pstrCrtPos) && ('\0' != *pstrCrtPos) ) {
                *pstrCrtPos = (char)toupper(*pstrCrtPos);
                ++pstrCrtPos;
            }
        } while(false);

        uSHELL_PRINTF("[.] executing [%s]\n", pstrArg);
        privExecScriptCommand(pstrArg);

    } while(false);

} /* uShellUserHandleShortcut_Dot() */


/******************************************************************************/
void uShellUserHandleShortcut_Slash( const char *pstrArgs )
{
    uSHELL_LOG(LOG_WARNING, "[/] registered but not implemented | args[%s] ", pstrArgs);

} /* uShellUserHandleShortcut_Slash() */

#endif /*(1 == uSHELL_IMPLEMENTS_USER_SHORTCUTS)*/


///////////////////////////////////////////////////////////////////
//               PRIVATE IMPLEMENTATION                          //
///////////////////////////////////////////////////////////////////

static int privListPlugins (const char *pstrCaption, const char *pstrPath, const char *pstrExtension)
{
    #define MAX_WORKBUFFER_SIZE    128U
    char vstrPluginPathName[MAX_WORKBUFFER_SIZE] = {0};
    struct dirent *entry = nullptr;
    DIR *dir = opendir(pstrPath);

    uSHELL_LOG(LOG_INFO, "--- %s plugins ---", pstrCaption);

    if (NULL == dir) {
        uSHELL_LOG(LOG_ERROR, "Failed to open the plugins folder [%s]", pstrPath);
        return 1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, pstrExtension) != NULL) {
            size_t name_len = strlen(entry->d_name);
            size_t ext_len = strlen(pstrExtension);

            // Ensure safe modification of string
            if (name_len > ext_len) {
                entry->d_name[name_len - ext_len] = '\0';
            }

            // Secure snprintf usage with truncation check
            int written = snprintf(vstrPluginPathName, MAX_WORKBUFFER_SIZE, "%30s%s | %s", entry->d_name, pstrExtension, entry->d_name + strlen(PLUGIN_PREFIX));

            if (written >= MAX_WORKBUFFER_SIZE) {
                uSHELL_LOG(LOG_WARNING, "Plugin name truncated: [%s]", vstrPluginPathName);
            }

            uSHELL_LOG(LOG_INFO, "%s", vstrPluginPathName);
        }
    }

    closedir(dir);

    return 0;

} /* privListPlugins() */


/*------------------------------------------------------------
 * execute a script command
------------------------------------------------------------*/
static int privListScriptItems (void)
{
    if( nullptr != pvLocalUserData ) {
        IScriptInterpreter *pScript = reinterpret_cast<IScriptInterpreter*>(pvLocalUserData);
        pScript->listItems();
    }

    return 0;

} /* privListScriptItems() */


/*------------------------------------------------------------
 * load a script plugin
------------------------------------------------------------*/
int privLoadScriptPlugin (char* pstrPluginName)
{
    if( nullptr != pvLocalUserData ) {
        IScriptInterpreter *pScript = reinterpret_cast<IScriptInterpreter*>(pvLocalUserData);
        pScript->loadPlugin(pstrPluginName);
    }

    return 0;

} /* privLoadScriptPlugin() */


/*------------------------------------------------------------
 * execute a script command
------------------------------------------------------------*/
static int privExecScriptCommand (char *pstrCommand)
{
    if( nullptr != pvLocalUserData ) {
        IScriptInterpreter *pScript = reinterpret_cast<IScriptInterpreter*>(pvLocalUserData);
        pScript->executeCmd(pstrCommand);
    }

    return 0;

} /* privExecScriptCommand() */

