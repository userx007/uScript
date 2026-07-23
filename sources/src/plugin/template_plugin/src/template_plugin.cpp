#include "template_plugin.hpp"

#include "uPluginSettings.hpp"

#include <string>


///////////////////////////////////////////////////////////////////
//                          PLUGIN ENTRY POINT                   //
///////////////////////////////////////////////////////////////////


/**
  * \brief The plugin's entry points
*/
extern "C"
{
    EXPORTED TemplatePlugin* pluginEntry()
    {
        return new TemplatePlugin();
    }

    EXPORTED void pluginExit( TemplatePlugin *ptrPlugin )
    {
        if (nullptr != ptrPlugin) {
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

bool TemplatePlugin::doInit(void *pvUserData)
{
    m_bIsInitialized = true;

    return m_bIsInitialized;

}


/**
  * \brief Function where to execute de-initialization of sub-modules
*/

void TemplatePlugin::doCleanup(void)
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
  *       TEMPLATE.DUMMY arg1 arg2 <br>
  *       will perform your dummy action with two argument
  *       TEMPLATE.DUMMY arg1 arg2 arg3 <br>
  *       will perform your dummy action with three arguments
  *
  * \param[in] pstrArgs space separated arguments
  *
  * \return true if succeeded, false otherwise
*/

bool TemplatePlugin::m_Template_DUMMY1( const std::string &args, std::stop_token st ) const
{
    bool bRetVal = false;

    do {
        // expected arguments
        if (false == args.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected no argument(s)"));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled) {
            bRetVal = true;
            break;
        }

        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Executing DUMMY1 (no-args, no-return)"));

        // implementation here..
        bRetVal = true;

    } while(false);

    return bRetVal;

}

bool TemplatePlugin::m_Template_DUMMY2( const std::string &args, std::stop_token st ) const
{
    bool bRetVal = false;

    do {

        // expected no arguments
        if (true == args.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected argument(s)"));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled) {
            bRetVal = true;
            break;
        }

        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Executing DUMMY2 (args, return)"); LOG_STRING("Arg:"); LOG_STRING(args));
        m_strResultData = args;

        // implementation here..
        bRetVal = true;

    } while(false);

    return bRetVal;

}


bool TemplatePlugin::m_Template_DUMMY3( const std::string &args, std::stop_token st ) const
{
    bool bRetVal = false;

    do {

        // expected arguments
        if (true == args.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected argument(s)"));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled) {
            bRetVal = true;
            break;
        }

        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Executing DUMMY3 (blocking, args, no-return)"); LOG_STRING("Arg:"); LOG_STRING(args));

        // DUMMY3 is declared blocking (bBlocking=true) in the command table.
        // It must be launched with '&' — the interpreter enforces this at
        // validation time.  The stop_token st is polled in the loop so the
        // thread exits cooperatively when the script engine calls request_stop()
        // at the end of script execution.
        while (!st.stop_requested()) {
            // implementation here..
        }

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
  *       TEMPLATE.INFO
  *
  * \param[in] pstrArgs NULL (NULL means that no arguments are provided to this function)
  *
  * \return true on success, false otherwise
*/

bool TemplatePlugin::m_Template_INFO ( const std::string &args, std::stop_token st ) const
{

    // expected no arguments
    if (!args.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected no argument(s)"));
        return false;
    }

    // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
    if (!m_bIsEnabled)
    {
        return true;
    }

    LOG_SEP();
    LOG_PRINT(LOG_INFO,  LOG_HDR; LOG_STRING("Executing INFO"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Version:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: "));
    LOG_SEP();

    return true;

}


///////////////////////////////////////////////////////////////////
//                      PRIVATE IMPLEMENTATION                   //
///////////////////////////////////////////////////////////////////


bool TemplatePlugin::m_LocalSetParams( const PluginDataSet *psSetParams )
{
    if (true == psSetParams->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
        return true;
    }

    // Bind each ini key to the class member (or validated setter) it should
    // initialize, e.g.:
    //   sSettings.Bind(MY_KEY, m_strMyMember);
    //   sSettings.Bind(MY_VALIDATED_KEY, [this](const std::string& v) { return setMyMember(v); });
    PluginSettingsBinder sSettings;

    return sSettings.Apply(psSetParams->mapSettings,
        [](const std::string& strKey, const std::string& strRawValue) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strKey); LOG_STRING(":"); LOG_STRING(strRawValue));
        });
}
