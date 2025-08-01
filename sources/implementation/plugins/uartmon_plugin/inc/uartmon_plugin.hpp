#ifndef UARTMON_PLUGIN_HPP
#define UARTMON_PLUGIN_HPP

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

#define UARTMON_PLUGIN_VERSION "1.0.0.0"



///////////////////////////////////////////////////////////////////
//                   PLUGIN COMMANDS                             //
///////////////////////////////////////////////////////////////////

#define UARTMON_PLUGIN_COMMANDS_CONFIG_TABLE   \
UARTMON_PLUGIN_CMD_RECORD( INFO              ) \
UARTMON_PLUGIN_CMD_RECORD( START             ) \
UARTMON_PLUGIN_CMD_RECORD( LIST_PORTS        ) \
UARTMON_PLUGIN_CMD_RECORD( WAIT_INSERT       ) \
UARTMON_PLUGIN_CMD_RECORD( WAIT_REMOVE       ) \


///////////////////////////////////////////////////////////////////
//            PLUGIN SETTINGS KEYWORDS IN INI FILE               //
///////////////////////////////////////////////////////////////////

// the common ones are described in the CommonSettings.hpp file


///////////////////////////////////////////////////////////////////
//                   PLUGIN INTERFACE                            //
///////////////////////////////////////////////////////////////////

/**
  * \brief Uartmon plugin class definition
*/
class UartmonPlugin: public PluginInterface
{
    public:

        /**
          * \brief class constructor
        */
        UartmonPlugin() : m_strPluginVersion(UARTMON_PLUGIN_VERSION)
            , m_bIsInitialized(false)
            , m_bIsEnabled(false)
            , m_bIsFaultTolerant(false)
            , m_bIsPrivileged(false)
            , m_strResultData("")
        {
            #define UARTMON_PLUGIN_CMD_RECORD(a) m_mapCmds.insert( std::make_pair( #a, &UartmonPlugin::m_Uartmon_##a ));
            UARTMON_PLUGIN_COMMANDS_CONFIG_TABLE
            #undef  UARTMON_PLUGIN_CMD_RECORD
        }

        /**
          * \brief class destructor
        */
        ~UartmonPlugin()
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

            if (true == generic_setparams<UartmonPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
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
            generic_getparams<UartmonPlugin>(this, psGetParams);
        }

        /**
          * \brief dispatch commands
        */
        bool doDispatch( const std::string& strCmd, const std::string& strParams ) const
        {
            return generic_dispatch<UartmonPlugin>(this, strCmd, strParams);
        }

        /**
          * \brief get a pointer to the plugin map
        */
        const PluginCommandsMap<UartmonPlugin> *getMap(void) const
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
        PluginCommandsMap<UartmonPlugin> m_mapCmds;

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
          * \brief array of threads used for monitoring things in background
        */
        mutable std::vector<THREAD_TYPE> m_vThreadArray;

        /**
          * \brief polling interval
        */
        static uint32_t m_u32PollingInterval;

        /**
          * \brief UART ports monitoring flags
        */
        static std::atomic<bool> m_bUartMonitoring;

        /**
          * \brief UART ports monitoring callbacks
        */
        #if defined(_MSC_VER)
        static void m_threadUartMonitoring (std::atomic<bool> & bRun);
        #else // Linux & MINGW
        static void *m_threadUartMonitoring (void *pvThreadArgs);
        #endif

        /**
          * \brief generic handler for UART operations
        */
        bool m_GenericUartHandling (const char *args, PFUARTHDL pfUartHdl) const;


        /**
          * \brief functions associated to the plugin commands
        */
        #define UARTMON_PLUGIN_CMD_RECORD(a)     bool m_Uartmon_##a ( const std::string &args ) const;
        UARTMON_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  UARTMON_PLUGIN_CMD_RECORD
};

#endif /* UARTMON_PLUGIN_HPP */