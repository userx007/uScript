#ifndef SHELL_PLUGIN_HPP
#define SHELL_PLUGIN_HPP

#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"

#include "uBoolExprParser.hpp"
#include "uLogger.hpp"
#include "CommonSettings.hpp"

#include <string>

///////////////////////////////////////////////////////////////////
//                    PLUGIN VERSION                             //
///////////////////////////////////////////////////////////////////

#define SHELL_PLUGIN_VERSION "1.0.0.0"

///////////////////////////////////////////////////////////////////
//                     LOG DEFINES                               //
///////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR     "SHELL      :"
#define LOG_HDR    LOG_STRING(LT_HDR)


///////////////////////////////////////////////////////////////////
//                   PLUGIN COMMANDS                             //
///////////////////////////////////////////////////////////////////

#define SHELL_PLUGIN_COMMANDS_CONFIG_TABLE    \
SHELL_PLUGIN_CMD_RECORD( INFO               ) \
SHELL_PLUGIN_CMD_RECORD( RUN                ) \


///////////////////////////////////////////////////////////////////
//            PLUGIN SETTINGS KEYWORDS IN INI FILE               //
///////////////////////////////////////////////////////////////////

// the common ones are described in the CommonSettings.hpp file

///////////////////////////////////////////////////////////////////
//                   PLUGIN INTERFACE                            //
///////////////////////////////////////////////////////////////////

/**
  * \brief Shell plugin class definition
*/
class ShellPlugin: public PluginInterface
{
public:

    /**
      * \brief class constructor
    */
    ShellPlugin() : m_strPluginVersion(SHELL_PLUGIN_VERSION)
        , m_bIsInitialized(false)
        , m_bIsEnabled(false)
        , m_bIsFaultTolerant(false)
        , m_bIsPrivileged(false)
        , m_pvUserData(nullptr)
        , m_strResultData("")
    {
#define SHELL_PLUGIN_CMD_RECORD(a) m_mapCmds.insert( std::make_pair( #a, &ShellPlugin::m_Shell_##a ));
        SHELL_PLUGIN_COMMANDS_CONFIG_TABLE
#undef  SHELL_PLUGIN_CMD_RECORD
    }

    /**
      * \brief class destructor
    */
    ~ShellPlugin()
    {

    }

    /**
      * \brief get the plugin initialization status
    */
    bool isInitialized( void ) const
    {
        return m_bIsInitialized;
    }

    /**
      * \brief get enabling status
    */
    bool isEnabled ( void ) const
    {
        return m_bIsEnabled;
    }

    /**
      * \brief function to provide various parameters to plugin
    */
    bool setParams( const PluginDataSet *psSetParams )
    {
        bool bRetVal = false;

        setLogger(psSetParams->shpLogger);
        if (!psSetParams->mapSettings.empty()) {
            do {
                // try to get value for PLUGIN_INI_FAULT_TOLERANT
                if (psSetParams->mapSettings.count(PLUGIN_INI_FAULT_TOLERANT) > 0) {
                    BoolExprParser beParser;
                    if (true == (bRetVal = beParser.evaluate(psSetParams->mapSettings.at(PLUGIN_INI_FAULT_TOLERANT), m_bIsFaultTolerant))) {
                        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("m_bIsFaultTolerant :"); LOG_BOOL(m_bIsFaultTolerant));
                    } else {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("failed to evaluate boolean value for"); LOG_STRING(PLUGIN_INI_FAULT_TOLERANT));
                        break;
                    }
                }

                // try to get value for PRIVILEGED
                if (psSetParams->mapSettings.count(PLUGIN_INI_PRIVILEGED) > 0) {
                    BoolExprParser beParser;
                    if (true == (bRetVal = beParser.evaluate(psSetParams->mapSettings.at(PLUGIN_INI_PRIVILEGED), m_bIsPrivileged))) {
                        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("m_bIsPrivileged :"); LOG_BOOL(m_bIsPrivileged));
                    } else {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("failed to evaluate boolean value for"); LOG_STRING(PLUGIN_INI_PRIVILEGED));
                        break;
                    }
                }

                bRetVal = true;

            } while(false);

        } else {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("no specific settings in .ini (empty)"));
        }

        return bRetVal;
    }

    /**
      * \brief function to retrieve information from plugin
    */
    void getParams( PluginDataGet *psGetParams ) const
    {
        generic_getparams<ShellPlugin>(this, psGetParams);
    }

    /**
      * \brief dispatch commands
    */
    bool doDispatch( const std::string& strCmd, const std::string& strParams ) const
    {
        return generic_dispatch<ShellPlugin>(this, strCmd, strParams);
    }

    /**
      * \brief get a pointer to the plugin map
    */
    const PluginCommandsMap<ShellPlugin> *getMap(void) const
    {
        return &m_mapCmds;
    }

    /**
      * \brief get the plugin version
    */
    const std::string& getVersion(void) const
    {
        return m_strPluginVersion;
    }

    /**
      * \brief get the result data
    */
    const std::string& getData(void) const
    {
        return m_strResultData;
    }

    /**
      * \brief clear the result data (avoid that some data to be returned by other command)
    */
    void resetData(void) const
    {
        m_strResultData.clear();
    }

    /**
      * \brief perform the initialization of modules used by the plugin
      * \note public because it needs to be called explicitely after loading the plugin
    */
    bool doInit(void *pvUserData);

    /**
      * \brief perform the enabling of the plugin
      * \note The un-enabled plugin can validate the command's arguments but doesn't allow the real execution
      *       This mode is used for the command validation
    */
    void doEnable(void)
    {
        m_bIsEnabled = true;
    }

    /**
      * \brief perform the de-initialization of modules used by the plugin
      * \note public because need to be called explicitely before closing/freeing the shared library
    */
    void doCleanup(void);

    /**
      * \brief get fault tolerant flag status
    */
    bool isFaultTolerant ( void ) const
    {
        return m_bIsFaultTolerant;
    }

    /**
      * \brief get the privileged status
    */
    bool isPrivileged ( void ) const
    {
        return m_bIsPrivileged;
    }

private:

    /**
      * \brief map with association between the command string and the execution function
    */
    PluginCommandsMap<ShellPlugin> m_mapCmds;

    /**
      * \brief plugin version
    */
    std::string m_strPluginVersion;

    /**
      * \brief data returned by plugin
    */
    mutable std::string m_strResultData;

    /**
      * \brief plugin initialization status
    */
    bool m_bIsInitialized;

    /**
      * \brief plugin enabling status
    */
    bool m_bIsEnabled;

    /**
      * \brief plugin fault tolerant mode
    */
    bool m_bIsFaultTolerant;

    /**
      * \brief plugin is privileged
    */
    bool m_bIsPrivileged;

    /**
      * \brief pointer to the user data structure
    */
    void *m_pvUserData;

    /**
      * \brief functions associated to the plugin commands
    */
#define SHELL_PLUGIN_CMD_RECORD(a)     bool m_Shell_##a ( const std::string &args ) const;
    SHELL_PLUGIN_COMMANDS_CONFIG_TABLE
#undef  SHELL_PLUGIN_CMD_RECORD
};

#endif /* SHELL_PLUGIN_HPP */