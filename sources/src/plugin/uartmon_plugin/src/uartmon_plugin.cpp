#include "uartmon_plugin.hpp"
#include "uUartMonitor.hpp"

#include "uNumeric.hpp"
#include "uString.hpp"
#include "uLogger.hpp"
#include "uPluginSettings.hpp"

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "UART_MONITOR|"
#define LOG_HDR    LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                 //
///////////////////////////////////////////////////////////////////

#define    POLLING_INTERVAL   "POLLING_INTERVAL"

///////////////////////////////////////////////////////////////////
//                          PLUGIN ENTRY POINT                   //
///////////////////////////////////////////////////////////////////

extern "C"
{
    EXPORTED UartmonPlugin* pluginEntry()
    {
        return new UartmonPlugin();
    }

    EXPORTED void pluginExit( UartmonPlugin *ptrPlugin )
    {
        if (nullptr != ptrPlugin) {
            delete ptrPlugin;
        }
    }
}

///////////////////////////////////////////////////////////////////
//                          INIT / CLEANUP                       //
///////////////////////////////////////////////////////////////////

bool UartmonPlugin::doInit(void *pvUserData)
{
    m_bIsInitialized = m_UartMonitor.setPollingInterval(m_u32PollingInterval);

    if (!m_bIsInitialized) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Initialization failed: invalid polling interval or monitor already active"));
    }

    return m_bIsInitialized;
}


void UartmonPlugin::doCleanup(void)
{
    if (m_isRunning) {
        m_UartMonitor.stopMonitoring();
        m_isRunning = false;
    }
    
    m_bIsInitialized = false;
    m_bIsEnabled     = false;
}

///////////////////////////////////////////////////////////////////
//                          COMMAND HANDLERS                     //
///////////////////////////////////////////////////////////////////

bool UartmonPlugin::m_Uartmon_INFO ( const std::string &args , std::stop_token st ) const
{
    if (!args.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected no argument(s)"));
        return false;
    }

    if (!m_bIsEnabled)
    {
        return true;
    }

    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING(UARTMON_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: UART port monitor - detect insertions and removals"));

    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("LIST_PORTS : list the UART ports currently reported by the system"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Usage: UARTMON.LIST_PORTS"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : output is printed to console; monitoring does not need to be started"));

    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("START : start monitoring UART port insertions and removals"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Usage: UARTMON.START"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : must be called before WAIT_INSERT / WAIT_REMOVE"));

    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("STOP : stop monitoring UART port insertions and removals"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Usage: UARTMON.STOP"));

    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("WAIT_INSERT : wait for a UART port to be inserted"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Args : [timeout_ms] [&]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         timeout_ms : max wait time in milliseconds (0 or omitted = wait forever)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         &          : run in background (non-blocking, threaded)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Usage: UARTMON.WAIT_INSERT"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         UARTMON.WAIT_INSERT 5000"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         NEW_PORT ?= UARTMON.WAIT_INSERT"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         NEW_PORT ?= UARTMON.WAIT_INSERT 5000 &"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Return : inserted port name, or empty string on timeout"));

    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("WAIT_REMOVE : wait for a UART port to be removed"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Args : [timeout_ms] [&]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         timeout_ms : max wait time in milliseconds (0 or omitted = wait forever)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         &          : run in background (non-blocking, threaded)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Usage: UARTMON.WAIT_REMOVE"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         UARTMON.WAIT_REMOVE 5000"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         REMOVED_PORT ?= UARTMON.WAIT_REMOVE"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         REMOVED_PORT ?= UARTMON.WAIT_REMOVE 5000 &"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Return : removed port name, or empty string on timeout"));
    LOG_SEP();

    return true;

}

bool UartmonPlugin::m_Uartmon_LIST_PORTS (const std::string &args, std::stop_token st ) const
{
   bool bRetVal = false;

    do {
        if (false == args.empty())
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unexpected arguments:"); LOG_STRING(args));
            break;
        }

        if (false == m_bIsEnabled)
        {
            bRetVal = true;
            break;
        }

        // listPorts() now returns vector<string> instead of string
        auto ports = m_UartMonitor.listPorts();
        std::string portsList;
        for (size_t i = 0; i < ports.size(); ++i) {
            portsList += ports[i];
            if (i < ports.size() - 1) {
                portsList += ", ";
            }
        }
        
        if (portsList.empty()) {
            portsList = "(no ports found)";
        }
        
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Ports:"); LOG_STRING(portsList));
        bRetVal = true;

    } while(false);

    return bRetVal;
}

bool UartmonPlugin::m_Uartmon_WAIT_INSERT (const std::string &args, std::stop_token st ) const
{
    return m_GenericWaitFor(args, true /*insert*/, st);
}

bool UartmonPlugin::m_Uartmon_WAIT_REMOVE (const std::string &args, std::stop_token st ) const
{
    return m_GenericWaitFor(args, false /*remove*/, st);
}

bool UartmonPlugin::m_Uartmon_START (const std::string &args, std::stop_token st ) const
{
    bool bRetVal = false;

    do {
        if (false == args.empty())
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("No argument expected"));
            break;
        }

        if (true == m_isRunning)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Monitoring already running ..."));
            break;
        }

        if (false == (m_isRunning = m_UartMonitor.startMonitoring())) 
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to start monitoring ..."));
            break;
        }

        bRetVal = true;

    } while(false);

    return bRetVal;
}

bool UartmonPlugin::m_Uartmon_STOP (const std::string &args, std::stop_token st ) const
{
    bool bRetVal = false;

    do {
        if (false == args.empty())
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("No argument expected"));
            break;
        }

        if (false == m_isRunning)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Monitoring not running ..."));
            break;
        }

        m_UartMonitor.stopMonitoring();
        m_isRunning = false;
        bRetVal = true;

    } while(false);

    return bRetVal;
}

///////////////////////////////////////////////////////////////////
//                      PRIVATE IMPLEMENTATION                   //
///////////////////////////////////////////////////////////////////

bool UartmonPlugin::m_GenericWaitFor (const std::string &args, bool bInsert, std::stop_token st) const
{
    bool bRetVal = false;

    do {
        if (false == m_isRunning)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Monitoring not running ..."));
            break;
        }

        uint32_t u32Delay = 0;

        if (false == args.empty()) {
            std::vector<std::string> vstrArgs;
            ustring::tokenizeSpaceQuotesAware(args, vstrArgs);
            size_t szNrArgs = vstrArgs.size();

            if (szNrArgs > 1) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid args, expected [delay]"); LOG_STRING(args));
                break;
            }

            if (1 == szNrArgs) {
                if (false == numeric::str2uint32(vstrArgs[0], u32Delay))
                {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Wrong delay value:"); LOG_STRING(args));
                    break;
                }
            }
        }

        if (false == m_bIsEnabled)
        {
            bRetVal = true;
            break;
        }

        // Register a stop callback so that if the script engine requests
        // cancellation (via stop_token), the monitor is signalled to stop,
        // which causes waitForInsert/waitForRemoval to return immediately
        // with WaitResult::Stopped.
        std::stop_callback stopCb(st, [this]() {
            m_UartMonitor.stopMonitoring();
        });

        auto action = [&]() {
            // Use new PortWaitResult API
            uart::PortWaitResult result;

            if (bInsert) {
                if (u32Delay != 0) {
                    result = m_UartMonitor.waitForInsert(std::chrono::milliseconds(u32Delay));
                } else {
                    result = m_UartMonitor.waitForInsert(std::nullopt);
                }
            } else {
                if (u32Delay != 0) {
                    result = m_UartMonitor.waitForRemoval(std::chrono::milliseconds(u32Delay));
                } else {
                    result = m_UartMonitor.waitForRemoval(std::nullopt);
                }
            }

            // Handle the result based on WaitResult enum
            if (result.result == uart::WaitResult::Success) {
                LOG_PRINT(LOG_INFO, LOG_HDR;
                         LOG_STRING("Port");
                         LOG_STRING(bInsert ? "insertion" : "removal");
                         LOG_STRING("detected:");
                         LOG_STRING(result.port_name));
                this->m_strResultData.assign(result.port_name);
            } else if (result.result == uart::WaitResult::Timeout) {
                LOG_PRINT(LOG_INFO, LOG_HDR;
                         LOG_STRING("Timeout waiting for port");
                         LOG_STRING(bInsert ? "insertion" : "removal"));
                this->m_strResultData.clear();
            } else { // WaitResult::Stopped
                LOG_PRINT(LOG_WARNING, LOG_HDR;
                         LOG_STRING("Monitoring stopped during wait"));
                this->m_strResultData.clear();
            }
        };

        action();
        bRetVal = true;

    } while(false);

    return bRetVal;
}

bool UartmonPlugin::m_LocalSetParams( const PluginDataSet *psSetParams )
{
    if (true == psSetParams->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
        return true;
    }

    PluginSettingsBinder sSettings;
    sSettings.Bind(POLLING_INTERVAL, m_u32PollingInterval);

    return sSettings.Apply(psSetParams->mapSettings,
        [](const std::string& strKey, const std::string& strRawValue) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strKey); LOG_STRING(":"); LOG_STRING(strRawValue));
        });
}