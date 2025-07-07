#include "shell_plugin.hpp"
#include "ushell_core.h"
#include "ushell_core_terminal.h"

#include <string>


///////////////////////////////////////////////////////////////////
//                          PLUGIN ENTRY POINT                   //
///////////////////////////////////////////////////////////////////


/**
  * \brief The plugin's entry points
*/
extern "C"
{
    EXPORTED ShellPlugin* pluginEntry()
    {
        return new ShellPlugin();
    }

    EXPORTED void pluginExit( ShellPlugin *ptrPlugin )
    {
        if (nullptr != ptrPlugin ) {
            delete ptrPlugin;
        }
    }
}


///////////////////////////////////////////////////////////////////
//                          INIT / CLEANUP                       //
///////////////////////////////////////////////////////////////////


/**
  * \brief Function where to execute initialization of sub-modules
*/

bool ShellPlugin::doInit(void *pvUserData)
{
    m_bIsInitialized = true;
    m_pvUserData = pvUserData;

    return m_bIsInitialized;

}


/**
  * \brief Function where to execute de-initialization of sub-modules
*/

void ShellPlugin::doCleanup(void)
{
    m_bIsInitialized = false;
    m_bIsEnabled     = false;
}

///////////////////////////////////////////////////////////////////
//                          COMMAND HANDLERS                     //
///////////////////////////////////////////////////////////////////

/**
  * \brief DUMMY command implementation; perform your dummy actions
  *
  * \note Usage example: <br>
  *       SHELL.RUN
    * \param[in] args string of space separated arguments
  *
  * \return true if succeeded, false otherwise
*/

bool ShellPlugin::m_Shell_RUN( const std::string &args ) const
{
    bool bRetVal = false;

    do {

        if (!args.empty() ) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected no argument(s)"));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled ) {
            bRetVal = true;
            break;
        }

        TerminalRAII terminal;
        std::shared_ptr<Microshell> pShellPtr = Microshell::getShellSharedPtr(uShellPluginEntry(m_pvUserData), "root");

        if (nullptr != pShellPtr) {
            pShellPtr->Run();
        }

        // implementation here..
        bRetVal = true;

    } while(false);

    return bRetVal;

}



/**
  * \brief INFO command implementation; shows details about plugin and
  *        describe the supported functions with examples of usage.
  *        This command takes no arguments and is executed even if the plugin initialization fails
  *
  * \note Usage example: <br>
  *       SHELL.INFO
  *
  * \param[in] pstrArgs NULL (NULL means that no arguments are provided to this function)
  *
  * \return true on success, false otherwise
*/

bool ShellPlugin::m_Shell_INFO ( const std::string &args ) const
{
    bool bRetVal = false;

    do {

        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Executing INFO"));

        // expected no arguments
        if (!args.empty() ) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected no argument(s)"));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled ) {
            bRetVal = true;
            break;
        }

        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Version:"); LOG_STRING(m_strPluginVersion.c_str()));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Description: "));

        bRetVal = true;

    } while(false);

    return bRetVal;

}

///////////////////////////////////////////////////////////////////
//                      PRIVATE IMPLEMENTATION                   //
///////////////////////////////////////////////////////////////////

bool ShellPlugin::m_LocalSetParams( const PluginDataSet *psSetParams )
{
    // add here the specific handling
    (void)psSetParams;

    return true;
}
