#ifndef UARTMON_PLUGIN_HPP
#define UARTMON_PLUGIN_HPP

#include "uSharedConfig.hpp"
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

#define UARTMON_PLUGIN_VERSION "2.0.0.0"

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

class UartmonPlugin: public PluginInterface
{
    public:
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

        ~UartmonPlugin()
        {
          for (std::thread& t : m_vThreads) {
              if (t.joinable()) {
                  t.join();
              }
          }
        }

        bool isInitialized( void ) const { return m_bIsInitialized; }
        bool isEnabled ( void ) const { return m_bIsEnabled; }
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
        void getParams( PluginDataGet *psGetParams ) const { generic_getparams<UartmonPlugin>(this, psGetParams); }
        bool doDispatch( const std::string& strCmd, const std::string& strParams ) const { return generic_dispatch<UartmonPlugin>(this, strCmd, strParams); }
        const PluginCommandsMap<UartmonPlugin> *getMap(void) const { return &m_mapCmds; }
        const std::string& getVersion(void) const { return m_strPluginVersion; }
        const std::string& getData(void) const { return m_strResultData; }
        void resetData(void) const { m_strResultData.clear(); }
        bool doInit(void *pvUserData);
        void doEnable(void) { m_bIsEnabled = true; }
        void doCleanup(void);
        bool isFaultTolerant ( void ) const { return m_bIsFaultTolerant; }
        bool isPrivileged ( void ) const { return m_bIsPrivileged; }

    private:
        bool m_LocalSetParams( const PluginDataSet *psSetParams );
        PluginCommandsMap<UartmonPlugin> m_mapCmds;
        std::string m_strPluginVersion;
        mutable std::string m_strResultData;
        bool m_bIsInitialized;
        bool m_bIsEnabled;
        bool m_bIsFaultTolerant;
        bool m_bIsPrivileged;
        uint32_t m_u32PollingInterval;
        
        // Changed from UartMonitor to uart::PortMonitor
        mutable uart::PortMonitor m_UartMonitor;
        
        mutable std::vector<std::thread> m_vThreads;
        mutable bool m_isRunning = false;

        bool m_GenericWaitFor (const std::string &args, bool bInsert) const;

        #define UARTMON_PLUGIN_CMD_RECORD(a)     bool m_Uartmon_##a ( const std::string &args ) const;
        UARTMON_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  UARTMON_PLUGIN_CMD_RECORD
};

#endif /* UARTMON_PLUGIN_HPP */