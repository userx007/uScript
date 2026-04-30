#include "shell_plugin.hpp"
#include "ushell_core.h"
#include "ushell_core_terminal.h"
#include "uGuiNotify.hpp"

#include <memory>
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
  * \brief RUN command implementation; launches an interactive shell session.
  *        The session blocks until the user exits the shell.
  *
  * \note Usage example: <br>
  *       SHELL.RUN
  *
  * \param[in] args unused (no arguments expected)
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

        // In GUI mode, stdin is a QProcess pipe — isatty(STDIN_FILENO) returns
        // false so TerminalRAII would bail early AND emit "Not a valid terminal."
        // on stdout, corrupting the GUI protocol stream.  Skip it entirely when
        // the front-end is attached; raw character delivery works over the pipe
        // without any termios manipulation.
        std::unique_ptr<TerminalRAII> terminal;
        if (!gui_mode_active()) {
            terminal = std::make_unique<TerminalRAII>();
        }

        std::shared_ptr<Microshell> pShellPtr = Microshell::getShellSharedPtr(uShellPluginEntry(m_pvUserData), "root");

        if (nullptr != pShellPtr) {
            // ── GUI: signal the front-end that the shell session is starting ──
            gui_notify_shell_run();

            pShellPtr->Run();

            // ── GUI: signal that the shell exited (user typed "exit") ──────
            // gui_notify_shell_exit() emits GUI:SHELL_EXIT then blocks on
            // stdin until the Qt side writes "SHELL_DONE\n" back, ensuring
            // the main script only resumes once the UI is in normal mode.
            gui_notify_shell_exit();
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

        LOG_SEP();
        LOG_PRINT(LOG_EMPTY, LOG_STRING(SHELL_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: launch an interactive shell session"));

        LOG_SEP();
        LOG_PRINT(LOG_EMPTY, LOG_STRING("RUN : start an interactive shell session (blocks until the user exits)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  Usage: SHELL.RUN"));
        LOG_SEP();

        bRetVal = true;

    } while(false);

    return bRetVal;

}

///////////////////////////////////////////////////////////////////
//                      PRIVATE IMPLEMENTATION                   //
///////////////////////////////////////////////////////////////////

bool ShellPlugin::m_LocalSetParams( const PluginDataSet *psSetParams )
{
    bool bRetVal = false;

    if (false == psSetParams->mapSettings.empty()) {
        do {

            bRetVal = true;

        } while(false);
    } else {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
        bRetVal = true;
    }

    return bRetVal;
}
