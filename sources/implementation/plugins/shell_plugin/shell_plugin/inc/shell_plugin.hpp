#ifndef SHELL_PLUGIN_HPP
#define SHELL_PLUGIN_HPP

#include "CommonSettings.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "uLogger.hpp"

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
    bool is_initialized( void ) const
    {
        return m_bIsInitialized;
    }

    /**
      * \brief get enabling status
    */
    bool is_enabled ( void ) const
    {
        return m_bIsEnabled;
    }

    /**
      * \brief Import external settings into the plugin
    */
    bool set_params( const PluginDataSet *psSetParams )
    {
        bool bRetVal = false;

        if (true == generic_setparams<ShellPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
            if (true == m_LocalSetParams(psSetParams)) {
                bRetVal = true;
            }
        }

        return bRetVal;
    }

    /**
      * \brief function to retrieve information from plugin
    */
    void get_params( PluginDataGet *psGetParams ) const
    {
        generic_getparams<ShellPlugin>(this, psGetParams);
    }

    /**
      * \brief dispatch commands
    */
    bool do_dispatch( const std::string& strCmd, const std::string& strParams ) const
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
    const std::string& get_data(void) const
    {
        return m_strResultData;
    }

    /**
      * \brief clear the result data (avoid that some data to be returned by other command)
    */
    void reset_data(void) const
    {
        m_strResultData.clear();
    }

    /**
      * \brief perform the initialization of modules used by the plugin
      * \note public because it needs to be called explicitely after loading the plugin
    */
    bool do_init(void *pvUserData);

    /**
      * \brief perform the enabling of the plugin
      * \note The un-enabled plugin can validate the command's arguments but doesn't allow the real execution
      *       This mode is used for the command validation
    */
    void do_enable(void)
    {
        m_bIsEnabled = true;
    }

    /**
      * \brief perform the de-initialization of modules used by the plugin
      * \note public because need to be called explicitely before closing/freeing the shared library
    */
    void do_cleanup(void);

    /**
      * \brief get fault tolerant flag status
    */
    bool is_fault_tolerant ( void ) const
    {
        return m_bIsFaultTolerant;
    }

    /**
      * \brief get the privileged status
    */
    bool is_privileged ( void ) const
    {
        return m_bIsPrivileged;
    }

private:

    /**
      * \brief processing of the plugin specific settings
    */
    bool m_LocalSetParams( const PluginDataSet *psSetParams );

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