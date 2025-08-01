#include "uartmon_plugin.hpp"

#include <string>

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

#define COM_PORT    "COM_PORT"

///////////////////////////////////////////////////////////////////
//            LOCAL DEFINES AND DATA TYPES                       //
///////////////////////////////////////////////////////////////////

// thread callback type declaration
#if defined(_MSC_VER)
    using THREADFUNCPTR = void (*)(std::atomic<bool> &);
#else
    using THREADFUNCPTR = void* (*)(void*);
#endif

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
//                          CLASS STATIC MEMBERS                 //
///////////////////////////////////////////////////////////////////

uint32_t UtilsPlugin::m_u32PollingInterval;
std::atomic<bool> UtilsPlugin::m_bUartMonitoring;


///////////////////////////////////////////////////////////////////
//                          INIT / CLEANUP                       //
///////////////////////////////////////////////////////////////////


/**
  * \brief Function where to execute initialization of sub-modules
*/

bool UartmonPlugin::doInit(void *pvUserData)
{
    m_bIsInitialized = true;
    m_bUartMonitoring.store(false);

    return m_bIsInitialized;
}


/**
  * \brief Function where to execute de-initialization of sub-modules
*/

void UartmonPlugin::doCleanup(void)
{

    int iThreadRetVal = 0;

    if (false == m_vThreadArray.empty())
    {
        // if started then stop the UART insertion monitoring
        if (true == m_bUartMonitoring.load())
        {
            uart_list_ports("Stopping UART monitoring =>");
            m_bUartMonitoring.store(false);
        }

#if defined(_MSC_VER)
        // join threads
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Joining threads created by plugin:"); LOG_UINT32((uint32_t)m_vThreadArray.size()));
        for( unsigned int i = 0; i < m_vThreadArray.size(); ++i)
        {
            m_vThreadArray.at(i).join();
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("["); LOG_UINT32(i); LOG_STRING("] std::thread.join() OK"));
        }
#else
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Stopping & joining threads created by plugin:"); LOG_UINT32((uint32_t)m_vThreadArray.size()));
        for( unsigned int i = 0; i < m_vThreadArray.size(); ++i)
        {
            void *pvJoinRetVal = nullptr;

            // cancel threads (ensure that they were set as cancelable)
            iThreadRetVal = pthread_cancel(m_vThreadArray.at(i));
            LOG_PRINT(((0 == iThreadRetVal) ? LOG_VERBOSE : LOG_ERROR), LOG_HDR; LOG_STRING("["); LOG_UINT32(i); LOG_STRING("] pthread_cancel()"); LOG_STRING((0 == iThreadRetVal) ? "ok" : "failed"));

            // join threads
            iThreadRetVal = pthread_join(m_vThreadArray.at(i), &pvJoinRetVal);
            LOG_PRINT(((0 == iThreadRetVal) ? LOG_VERBOSE : LOG_ERROR), LOG_HDR; LOG_STRING("["); LOG_UINT32(i); LOG_STRING("] pthread_join()"); LOG_STRING((0 == iThreadRetVal) ? "ok" : "failed"));

        }
    }
#endif

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
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UTILS.LIST_UART_PORTS"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("WAIT_UART_INSERT : wait for UART port insertion"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : [timeout] (if 0 or absent then wait forever)"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UTILS.WAIT_UART_INSERT 5000"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       NEW_PORT ?= UTILS.WAIT_UART_INSERT"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       NEW_PORT ?= UTILS.WAIT_UART_INSERT 5000"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       UTILS.PRINT $NEW_PORT"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Return : the inserted port or empty if the timeout occurs before insertion"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Note   : the expected port must be absent at the call time"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("WAIT_UART_REMOVE : wait for UART port removal"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : [timeout] (if 0 or absent then wait forever)"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UTILS.WAIT_UART_REMOVE 5000"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       REMOVED_PORT ?= UTILS.WAIT_UART_REMOVE"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       REMOVED_PORT ?= UTILS.WAIT_UART_REMOVE 5000"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       UTILS.PRINT $REMOVED_PORT"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Return : the inserted port or empty if the timeout occurs before removal"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Note   : the expected port must be present at the call time"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("START_UART_MONITORING : start reporting UART port insertions and removals"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : none"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UTILS.START_UART_MONITORING"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Note : runs until the end of script; for experimental monitoring use as:"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       UTILS.START_UART_MONITORING"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       UTILS.DELAY 10000"));

        bRetVal = true;

    } while(false);

    return bRetVal;

}


/**
  * \brief WAIT_UART_INSERT wait for an USB UART port to be inserted
  *        with a specified timeout (if 0 or not provided then wait forever)
  *
  * \note Usage example: <br>
  *       UTILS.WAIT_UART_INSERT
  *       UTILS.WAIT_UART_INSERT 3000
  *
  * \param[in] none or timeout to wait for the UART insertion
  *
  * \return true on success, false otherwise
*/

bool UtilsPlugin::m_Utils_WAIT_INSERT (const std::string &args) const
{
    return m_GenericUartHandling (args, uart_wait_port_insert);

}


/**
  * \brief WAIT_UART_REMOVE wait for an USB UART port to be removed
  *        with a specified timeout (if 0 or not provided then wait forever)
  *
  * \note Usage example: <br>
  *       UTILS.WAIT_UART_REMOVE
  *       UTILS.WAIT_UART_REMOVE 3000
  *
  * \note If no port is available at the moment of call then the command returns immediatelly
  *
  * \param[in] none or timeout to wait for the UART removal
  *
  * \return true on success, false otherwise
*/

bool UtilsPlugin::m_Utils_WAIT_REMOVE (const std::string &args) const
{
    bool bRetVal = false;

    do {

        if (0 == uart_get_available_ports_number())
        {
            LOG_PRINT(LOG_WARN, LOG_HDR; LOG_STRING("No UART port(s) currently available"));
            bRetVal = true;
            break;
        }

        bRetVal = m_GenericUartHandling (args, uart_wait_port_remove);

    } while(false);

    return bRetVal;

}


/**
  * \brief Monitor UART ports for the specified action (insertion / removal)
  *
  * \note Usage example: <br>
  *      UTILS.START_UART_MONITORING
  *
  * \param none
  *
  * \return true if the execution succeeded, false otherwise
*/

bool UtilsPlugin::m_Utils_START (const std::string &args) const
{
    bool bRetVal = false;

    do {

        // arguments expected
        if (false == args.empty()) {
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("No argument expected"));
            break;
        }

        // only one monitoring per operation is allowed
        if (true == m_bUartMonitoring.load())
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("UART port monitoring already exists"));
            break;
        }

        // set the internal flags (used to avoid multiple monitorings per operation)
        m_bUartMonitoring.store(true);

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled)
        {
            bRetVal = true;
            break;
        }

        // declare the function to be passed to the monitoring thread
        THREADFUNCPTR pfctThreadCB = (THREADFUNCPTR)&UtilsPlugin::m_threadUartMonitoring;

        // create the thread and add it to the vector of threads for joining them later ..
#if defined(_MSC_VER)

        std::thread threadExec(pfctThreadCB, std::ref(m_bUartMonitoring));
        m_vThreadArray.push_back(std::move(threadExec));

#else // Linux & MINGW

        pthread_t threadExec;
        if (0 != pthread_create( &threadExec, nullptr, pfctThreadCB, nullptr))
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to create pthread for UART monitoring"));
            break;
        }
        m_vThreadArray.push_back(threadExec);

#endif // #if defined(_MSC_VER)

        bRetVal = true;

    } while(false);

    return bRetVal;

}


/**
  * \brief list UART ports reported by the system
  *
  * \note Usage example: <br>
  *       UTILS.LIST_UART_PORTS
  *
  * \param[in] none
  *
  * \return true on success, false otherwise
*/

bool UtilsPlugin::m_Utils_LIST_PORTS (const std::string &args) const
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

        uart_list_ports();
        bRetVal = true;

    } while(false);

    return bRetVal;

}


///////////////////////////////////////////////////////////////////
//                      PRIVATE IMPLEMENTATION                   //
///////////////////////////////////////////////////////////////////


/**
 * \brief Generic function for UART port handling (insert, remove)
 * \param[in] args argumen(s) as string, here timeout to wait for insert/removal
 * \param[in] pfUartHdl pointer to a function to be called for handling
 * \return true on success, false otherwise
 */

bool UtilsPlugin::m_GenericUartHandling (const char *args, PFUARTHDL pfUartHdl) const
{
    bool bRetVal = false;
    uint32_t uiTimeout = 0;

    do {

        // if arguments are provided
        if (false == args.empty()) {
        {
            // fail if more than one space separated arguments is provided ...
            if (true == string_contains_char(args, CHAR_SEPARATOR_SPACE))
            {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected: delay"));
                break;
            }

            // convert string to integer
            if (false == numeric::str2uint32( args, uiTimeout))
            {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Incorrect delay value:"); LOG_STRING(args));
                break;
            }
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled)
        {
            bRetVal = true;
            break;
        }

        // buffer to store device name, i.e. windows: COM0 ... COM255, linux: /dev/ttyUSB0 .. 255 /dev/ttyACM0 .. 255
        char vcItem[32] = { 0 };

        // execute the callback
        if (false == pfUartHdl(vcItem, sizeof(vcItem), uiTimeout, m_u32PollingInterval))
        {
            break;
        }

        // this value can be returned to a variable
        m_strResultData.assign(vcItem);

        bRetVal = true;

    } while(false);

    return bRetVal;

}


#if defined(_MSC_VER)

/**
  * \brief Thread's callback used to monitor the UART port insertion
  * \param[in] bRun flag used to control the thread execution
  * \return null pointer
*/

void UtilsPlugin::m_threadUartMonitoring( std::atomic<bool> & bRun)
{
    uart_list_ports("(T) UART monitoring started in background =>");
    uart_monitor( m_u32PollingInterval, bRun);

}

#else // Linux & MINGW

/**
  * \brief Thread's callback used to monitor the UART port insertion
  * \param[in] pvThreadArgs pointer to the thread parameters
  * \return null pointer
*/

void* UtilsPlugin::m_threadUartMonitoring (void *pvThreadArgs)
{
    const std::string strCaption = "(T) UART monitoring";
    int iThRetVal = 0;

    do {

        if (0 != (iThRetVal = pthread_setcancelstate( PTHREAD_CANCEL_ENABLE, NULL)))
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(pstrCaption); LOG_STRING(": pthread_setcancelstate() failed, error:"); LOG_INT(iThRetVal));
            break;
        }

        if (0 != (iThRetVal = pthread_setcanceltype( PTHREAD_CANCEL_ASYNCHRONOUS, NULL)))
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(pstrCaption); LOG_STRING(": pthread_setcanceltype() failed, error:"); LOG_INT(iThRetVal));
            break;
        }

        // permanently false as the thread will be canceled
        std::atomic<bool> bRun (true);

        uart_list_ports("(T) UART monitoring started in background =>");
        uart_monitor( m_u32PollingInterval, std::ref(bRun));

    } while(false);

    return nullptr;

}

#endif // #if defined(_MSC_VER)


bool UartmonPlugin::m_LocalSetParams( const PluginDataSet *psSetParams )
{
    // add here the specific handling
    (void)psSetParams;

    return true;
}
