#include "uartmon_plugin.hpp"
#include "uUartMonitor.hpp"

#include "uNumeric.hpp"
#include "uLogger.hpp"
#include

///////////////////////////////////////////////////////////////////
//                     LOG DEFINES                               //
///////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR     "UARTMON    :"
#define LOG_HDR    LOG_STRING(LT_HDR)


///////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                 //
///////////////////////////////////////////////////////////////////

#define    POLLING_INTERVAL   "POLLING_INTERVAL"

///////////////////////////////////////////////////////////////////
//                          PLUGIN ENTRY POINT                   //
///////////////////////////////////////////////////////////////////


/**
  * \brief The plugin's entry points
*/
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


/**
  * \brief Function where to execute initialization of sub-modules
*/

bool UartmonPlugin::doInit(void *pvUserData)
{
    m_UartMonitor.setPollingInterval(m_u32PollingInterval);
    m_bUartMonitoring.store(false);
    m_bIsInitialized = true;

    return m_bIsInitialized;
}


/**
  * \brief Function where to execute de-initialization of sub-modules
*/

void UartmonPlugin::doCleanup(void)
{

    m_bIsInitialized = false;
    m_bIsEnabled     = false;
}


///////////////////////////////////////////////////////////////////
//                          COMMAND HANDLERS                     //
///////////////////////////////////////////////////////////////////


/**
  * \brief INFO command implementation; shows details about plugin and
  *        describe the supported functions with examples of usage.
  *        This command takes no arguments and is executed even if the plugin initialization fails
  *
  * \note Usage example: <br>
  *       UARTMON.INFO
  *
  * \param[in] pstrArgs NULL (NULL means that no arguments are provided to this function)
  *
  * \return true on success, false otherwise
*/

bool UartmonPlugin::m_Uartmon_INFO ( const std::string &args ) const
{
    bool bRetVal = false;

    do {

        // expected no arguments
        if (false == args.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected no argument(s)"));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled) {
            bRetVal = true;
            break;
        }

        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Version:"); LOG_STRING(m_strPluginVersion));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Description: "));

        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("LIST_UART_PORTS : lists the uart ports reported by the system"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : none"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UARTMON.LIST_UART_PORTS"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("WAIT_INSERT : wait for UART port insertion"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : [timeout] (if 0 or absent then wait forever)"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UARTMON.WAIT_INSERT 5000"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       NEW_PORT ?= UARTMON.WAIT_INSERT"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       NEW_PORT ?= UARTMON.WAIT_INSERT 5000"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       UARTMON.PRINT $NEW_PORT"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Return : the inserted port or empty if the timeout occurs before insertion"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Note   : the expected port must be absent at the call time"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("WAIT_REMOVE : wait for UART port removal"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : [timeout] (if 0 or absent then wait forever)"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UARTMON.WAIT_REMOVE 5000"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       REMOVED_PORT ?= UARTMON.WAIT_REMOVE"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       REMOVED_PORT ?= UARTMON.WAIT_REMOVE 5000"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       UARTMON.PRINT $REMOVED_PORT"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Return : the inserted port or empty if the timeout occurs before removal"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Note   : the expected port must be present at the call time"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("START : start reporting UART port insertions and removals"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : none"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UARTMON.START"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Note : runs until the end of script; for experimental monitoring use as:"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       UARTMON.START"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       UARTMON.DELAY 10000"));

        bRetVal = true;

    } while(false);

    return bRetVal;

}



/**
  * \brief list UART ports reported by the system
  *
  * \note Usage example: <br>
  *       UARTMON.LIST_PORTS
  *
  * \param[in] none
  *
  * \return true on success, false otherwise
*/

bool UartmonPlugin::m_Uartmon_LIST_PORTS (const std::string &args) const
{
   bool bRetVal = false;

    do {

        // no arguments are expected
        if (false == args.empty()) {
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unexpected arguments:"); LOG_STRING(args));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled)
        {
            bRetVal = true;
            break;
        }

        m_UartMonitor.listPorts();
        bRetVal = true;

    } while(false);

    return bRetVal;

}


/**
  * \brief WAIT_INSERT wait for uart port insertion with a specified timeout (if not provided then wait forever)
  *
  * \note Usage example: <br>
  *       UARTMON.WAIT_INSERT
  *       UARTMON.WAIT_INSERT 3000
  *
  * \param[in] none or timeout to wait for the UART insertion
  *
  * \return true on success, false otherwise and the inserted port is available in m_strResultData
*/

bool UartmonPlugin::m_Uartmon_WAIT_INSERT (const std::string &args) const
{
    return m_GenericWaitFor(args, true /*insert*/);
}


/**
  * \brief WAIT_REMOVE wait for uart port removal with a specified timeout (if not provided then wait forever)
  *
  * \note Usage example: <br>
  *       UARTMON.WAIT_REMOVE
  *       UARTMON.WAIT_REMOVE 3000
  *
  * \note If no port is available at the moment of call then the command returns immediatelly
  *
  * \param[in] none or timeout to wait for the UART removal
  *
  * \return true on success, false otherwise and the removed port is available in m_strResultData
*/

bool UartmonPlugin::m_Uartmon_WAIT_REMOVE (const std::string &args) const
{
    return m_GenericWaitFor(args, false /*remove*/);
}


/**
  * \brief Start UART ports monitoring
  *
  * \note Usage example: <br>
  *      UARTMON.START
  *
  * \param none
  *
  * \return true if the execution succeeded, false otherwise
*/

bool UartmonPlugin::m_Uartmon_START (const std::string &args) const
{
    bool bRetVal = false;

    do {

        // arguments expected
        if (false == args.empty()) {
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("No argument expected"));
            break;
        }

        if (true == m_isRunning)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Monitoring already running ..."));
            break;
        }

        m_UartMonitor.startMonitoring();
        m_isRunning = true;

        bRetVal = true;

    } while(false);

    return bRetVal;

}


/**
  * \brief Stop UART ports monitoring
  *
  * \note Usage example: <br>
  *      UARTMON.STOP
  *
  * \param none
  *
  * \return true if the execution succeeded, false otherwise
*/

bool UartmonPlugin::m_Uartmon_STOP (const std::string &args) const
{
    bool bRetVal = false;

    do {

        // arguments expected
        if (false == args.empty()) {
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

            if (1 == szNrArgs) { // only one argument
                if(vstrArgs[0] == PLUGIN_COMMAND_THREADED) { // arg is & (command threaded)
                    bThreaded = true;
                } else { // expected delay only
                    if (false == numeric::str2uint32(vstrArgs[0], u32Delay))
                    {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Wrong delay value:"); LOG_STRING(args));
                        break;
                    }
                }
            } else { // delay and &
                if ((false == numeric::str2uint32(vstrArgs[0], u32Delay)) || (vstrArgs[0] != PLUGIN_COMMAND_THREADED))
                {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Wrong delay value or threaded symbol"); LOG_STRING(args));
                    break;
                }
                bThreaded = true;
            }
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled)
        {
            bRetVal = true;
            break;
        }

        auto monitorPort = [&](bool threaded) {
            auto action = [&]() {
                std::string port;
                if (bInsert)
                    port = (u32Delay != 0)
                        ? m_UartMonitor.waitForInsert(std::chrono::milliseconds(u32Delay))
                        : m_UartMonitor.waitForInsert();
                else
                    port = (u32Delay != 0)
                        ? m_UartMonitor.waitForRemoval(std::chrono::milliseconds(u32Delay))
                        : m_UartMonitor.waitForRemoval();

                if (!port.empty()) {
                    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Port");
                              LOG_STRING(bInsert ? "insertion" : "removal");
                              LOG_STRING("detected:"); LOG_STRING(port));
                    this->m_strResultData.assign(port);
                }
            };

            threaded ? m_vThreads.emplace_back(action) : action();
        };

        monitorPort(bThreaded);

        bRetVal = true;

    } while(false);

    return bRetVal;
}


bool UartmonPlugin::m_LocalSetParams( const PluginDataSet *psSetParams )
{
    bool bRetVal = false;

    if (false == psSetParams->mapSettings.empty()) {
        do {

            if (psSetParams->mapSettings.count(POLLING_INTERVAL) > 0) {
                if (false == numeric::str2int(psSetParams->mapSettings.at(VENDOR_ID), m_iVendorID)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("PollingInterval:"); LOG_INT(m_u32PollingInterval));
            }
            bRetVal = true;
        } while(false);
    }

    return bRetVal;

}
