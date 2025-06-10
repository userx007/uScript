#include "ushell_core_utils.h"
#include "ushell_user_logger.h"

///////////////////////////////////////////////////////////////////
//                  USER'S FUNCTIONS                             //
///////////////////////////////////////////////////////////////////


int test(void)
{
    uSHELL_LOG(LOG_VERBOSE, "test() called..." );

    return 1;
}

///////////////////////////////////////////////////////////////////
//               USER'S SHORTCUTS HANDLERS                       //
///////////////////////////////////////////////////////////////////


/*----------------------------------------------------------------------------*/

void uShellUserHandleShortcut_Slash( const char *pstrArgs )
{
    uSHELL_LOG(LOG_VERBOSE, "[/] shortcut handler | args [%s] called..", pstrArgs);
    uSHELL_LOG(LOG_WARNING, "Not implemented");

} /* uShellUserHandleShortcut_Slash() */


/*----------------------------------------------------------------------------*/

void uShellUserHandleShortcut_Dot( const char *pstrArgs )
{
    uSHELL_LOG(LOG_VERBOSE, "[.] shortcut handler | args [%s] called..", pstrArgs);
    uSHELL_LOG(LOG_WARNING, "Not implemented");

} /* uShellUserHandleShortcut_Dot() */
