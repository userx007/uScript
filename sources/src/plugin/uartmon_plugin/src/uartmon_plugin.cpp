#include "uartmon_plugin.hpp"
#include "uUartMonitor.hpp"

#include "uNumeric.hpp"
#include "uString.hpp"
#include "uLogger.hpp"

///////////////////////////////////////////////////////////////////
//                     LOG DEFINES                               //
///////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR     "UART_MON   :"
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
    try {
        m_UartMonitor.setPollingInterval(m_u32PollingInterval);
        m_bIsInitialized = true;
    } catch (const std::exception& e) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Initialization failed:"); LOG_STRING(e.what()));
        m_bIsInitialized = false;
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

bool UartmonPlugin::m_Uartmon_INFO ( const std::string &args ) const
{
    bool bRetVal = false;

    do {
        if (false == args.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected no argument(s)"));
            break;
        }

        if (false == m_bIsEnabled) {
            bRetVal = true;
            break;
        }

        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Version:"); LOG_STRING(m_strPluginVersion));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Description: UART Port Monitor Plugin v2.0"));

        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("LIST_PORTS : lists the uart ports reported by the system"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : none"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UARTMON.LIST_PORTS"));

        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("WAIT_INSERT : wait for UART port insertion"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : [timeout] [&]"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UARTMON.WAIT_INSERT 5000"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       NEW_PORT ?= UARTMON.WAIT_INSERT"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       NEW_PORT ?= UARTMON.WAIT_INSERT 5000 &"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Return : the inserted port or empty if the timeout occurs before insertion"));

        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("WAIT_REMOVE : wait for UART port removal"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : [timeout] [&]"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UARTMON.WAIT_REMOVE 5000"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       REMOVED_PORT ?= UARTMON.WAIT_REMOVE"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Return : the removed port or empty if the timeout occurs before removal"));

        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("START : start reporting UART port insertions and removals"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : none"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UARTMON.START"));

        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("STOP : stop reporting UART port insertions and removals"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : none"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UARTMON.STOP"));

        bRetVal = true;

    } while(false);

    return bRetVal;
}

bool UartmonPlugin::m_Uartmon_LIST_PORTS (const std::string &args) const
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

bool UartmonPlugin::m_Uartmon_WAIT_INSERT (const std::string &args) const
{
    return m_GenericWaitFor(args, true /*insert*/);
}

bool UartmonPlugin::m_Uartmon_WAIT_REMOVE (const std::string &args) const
{
    return m_GenericWaitFor(args, false /*remove*/);
}

bool UartmonPlugin::m_Uartmon_START (const std::string &args) const
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

        try {
            m_UartMonitor.startMonitoring();
            m_isRunning = true;
            bRetVal = true;
        } catch (const std::exception& e) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to start monitoring:"); LOG_STRING(e.what()));
        }

    } while(false);

    return bRetVal;
}

bool UartmonPlugin::m_Uartmon_STOP (const std::string &args) const
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

bool UartmonPlugin::m_GenericWaitFor (const std::string &args, bool bInsert) const
{
    bool bRetVal = false;

    do {
        if (false == m_isRunning)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Monitoring not running ..."));
            break;
        }

        uint32_t u32Delay = 0;
        bool bThreaded = false;

        if (false == args.empty()) {
            std::vector<std::string> vstrArgs;
            ustring::tokenizeSpaceQuotesAware(args, vstrArgs);
            size_t szNrArgs = vstrArgs.size();

            if (szNrArgs > 2) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid args, expected [delay] [&]"); LOG_STRING(args));
                break;
            }

            if (1 == szNrArgs) {
                if(vstrArgs[0] == PLUGIN_COMMAND_THREADED) {
                    bThreaded = true;
                } else {
                    if (false == numeric::str2uint32(vstrArgs[0], u32Delay))
                    {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Wrong delay value:"); LOG_STRING(args));
                        break;
                    }
                }
            } else {
                if (vstrArgs[1] != PLUGIN_COMMAND_THREADED)
                {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Wrong threaded symbol"); LOG_STRING(args));
                    break;
                }
                if (false == numeric::str2uint32(vstrArgs[0], u32Delay))
                {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Wrong delay value"); LOG_STRING(args));
                    break;
                }
                bThreaded = true;
            }
        }

        if (false == m_bIsEnabled)
        {
            bRetVal = true;
            break;
        }

        auto monitorPort = [&](bool threaded) {
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

            if (threaded) {
                m_vThreads.emplace_back(action);
            } else {
                action();
            }
        };

        monitorPort(bThreaded);
        bRetVal = true;

    } while(false);

    return bRetVal;
}

bool UartmonPlugin::m_LocalSetParams( const PluginDataSet *psSetParams )
{
    bool bRetVal = false;

    if (false == psSetParams->mapSettings.empty()){
        do {
            if (psSetParams->mapSettings.count(POLLING_INTERVAL) > 0) {
                if (false == numeric::str2uint32(psSetParams->mapSettings.at(POLLING_INTERVAL), m_u32PollingInterval)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("PollingInterval:"); LOG_INT(m_u32PollingInterval));
            }
            bRetVal = true;

        } while(false);
    } else {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
        bRetVal = true;
    }

    return bRetVal;
}