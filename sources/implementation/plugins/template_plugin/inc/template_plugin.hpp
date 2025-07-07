#ifndef TEMPLATE_PLUGIN_HPP
#define TEMPLATE_PLUGIN_HPP

#include "CommonSettings.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"

#include "uBoolExprParser.hpp"
#include "uLogger.hpp"

#include <string>

///////////////////////////////////////////////////////////////////
//                    PLUGIN VERSION                             //
///////////////////////////////////////////////////////////////////

#define TEMPLATE_PLUGIN_VERSION "1.0.0.0"

///////////////////////////////////////////////////////////////////
//                     LOG DEFINES                               //
///////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR     "TEMPLATE   :"
#define LOG_HDR    LOG_STRING(LT_HDR)


///////////////////////////////////////////////////////////////////
//                   PLUGIN COMMANDS                             //
///////////////////////////////////////////////////////////////////

#define TEMPLATE_PLUGIN_COMMANDS_CONFIG_TABLE    \
TEMPLATE_PLUGIN_CMD_RECORD( INFO               ) \
TEMPLATE_PLUGIN_CMD_RECORD( DUMMY1             ) \
TEMPLATE_PLUGIN_CMD_RECORD( DUMMY2             ) \
TEMPLATE_PLUGIN_CMD_RECORD( DUMMY3             ) \


///////////////////////////////////////////////////////////////////
//            PLUGIN SETTINGS KEYWORDS IN INI FILE               //
///////////////////////////////////////////////////////////////////

// the common ones are described in the CommonSettings.hpp file


///////////////////////////////////////////////////////////////////
//                   PLUGIN INTERFACE                            //
///////////////////////////////////////////////////////////////////

/**
  * \brief Template plugin class definition
*/
class TemplatePlugin: public PluginInterface
{
public:

    /**
      * \brief class constructor
    */
    TemplatePlugin() : m_strPluginVersion(TEMPLATE_PLUGIN_VERSION)
        , m_bIsInitialized(false)
        , m_bIsEnabled(false)
        , m_bIsFaultTolerant(false)
        , m_bIsPrivileged(false)
        , m_strResultData("")
    {
#define TEMPLATE_PLUGIN_CMD_RECORD(a) m_mapCmds.insert( std::make_pair( #a, &TemplatePlugin::m_Template_##a ));
        TEMPLATE_PLUGIN_COMMANDS_CONFIG_TABLE
#undef  TEMPLATE_PLUGIN_CMD_RECORD
    }

    /**
      * \brief class destructor
    */
    ~TemplatePlugin()
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
      * \brief Import external settings into the plugin
    */
    bool setParams( const PluginDataSet *psSetParams )
    {
        bool bRetVal = false;

        if (true == generic_setparams<TemplatePlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
            if (true == m_LocalSetParams(psSetParams)) {
                bRetVal = true;
            }
        }

        return bRetVal;
    }

    /**
      * \brief function to retrieve information from plugin
    */
    void getParams( PluginDataGet *psGetParams ) const
    {
        generic_getparams<TemplatePlugin>(this, psGetParams);
    }

    /**
      * \brief dispatch commands
    */
    bool doDispatch( const std::string& strCmd, const std::string& strParams ) const
    {
        return generic_dispatch<TemplatePlugin>(this, strCmd, strParams);
    }

    /**
      * \brief get a pointer to the plugin map
    */
    const PluginCommandsMap<TemplatePlugin> *getMap(void) const
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
      * \brief processing of the plugin specific settings
    */
    bool m_LocalSetParams( const PluginDataSet *psSetParams );

    /**
      * \brief map with association between the command string and the execution function
    */
    PluginCommandsMap<TemplatePlugin> m_mapCmds;

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
      * \brief functions associated to the plugin commands
    */
#define TEMPLATE_PLUGIN_CMD_RECORD(a)     bool m_Template_##a ( const std::string &args ) const;
    TEMPLATE_PLUGIN_COMMANDS_CONFIG_TABLE
#undef  TEMPLATE_PLUGIN_CMD_RECORD
};

#endif /* TEMPLATE_PLUGIN_HPP */