#ifndef UARTMON_PLUGIN_HPP
#define UARTMON_PLUGIN_HPP

#include "CommonSettings.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"

#include "uUartMonitor.hpp"

#include <string>
#include <vector>
#include <thread>

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
UARTMON_PLUGIN_CMD_RECORD( STOP              ) \
UARTMON_PLUGIN_CMD_RECORD( LIST_PORTS        ) \
UARTMON_PLUGIN_CMD_RECORD( WAIT_INSERT       ) \
UARTMON_PLUGIN_CMD_RECORD( WAIT_REMOVE       ) \

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
            , m_u32PollingInterval(PLUGIN_DEFAULT_UARTMON_POLLING_INTERVAL)
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
          for (std::thread& t : m_vThreads) {
              if (t.joinable()) {
                  t.join();
              }
          }
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
          * \brief polling interval
        */
        uint32_t m_u32PollingInterval;

        /**
          * \brief UART ports monitor
        */
        UartMonitor m_UartMonitor;

        /**
          * \brief vector of threads
        */
        mutable std::vector<std::thread> m_vThreads;

        /**
          * \brief running state of monitoring
        */
        mutable bool m_isRunning = false;

        bool m_GenericWaitFor (const std::string &args, bool bInsert) const;

        /**
          * \brief functions associated to the plugin commands
        */
        #define UARTMON_PLUGIN_CMD_RECORD(a)     bool m_Uartmon_##a ( const std::string &args ) const;
        UARTMON_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  UARTMON_PLUGIN_CMD_RECORD
};

#endif /* UARTMON_PLUGIN_HPP */