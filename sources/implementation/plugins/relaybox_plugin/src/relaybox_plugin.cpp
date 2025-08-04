#include "CommonSettings.hpp"
#include "PluginSpecOperations.hpp"

#include "relaybox_plugin.hpp"

#include "uNumeric.hpp"
#include "uTimer.hpp"

///////////////////////////////////////////////////////////////////
//                     LOG DEFINES                               //
///////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR     "RELAYBOX   :"
#define LOG_HDR    LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                 //
///////////////////////////////////////////////////////////////////

#define    SERIAL_NUMBER      "SERIAL_NUMBER"
#define    PRODUCT_ID         "PRODUCT_ID"
#define    VENDOR_ID          "VENDOR_ID"
#define    NR_CHANNELS        "NR_CHANNELS"

///////////////////////////////////////////////////////////////////
//            LOCAL DEFINES AND DATA TYPES                       //
///////////////////////////////////////////////////////////////////

// thread callback type declaration
using THREADFUNCPTR = void* (*)(void*);

// flag used for "all channels"
#define RELAY_ALL_CHANNELS  0xFFFF

///////////////////////////////////////////////////////////////////
//                          PLUGIN ENTRY POINT                   //
///////////////////////////////////////////////////////////////////


/**
  * \brief The plugin's entry points
*/
extern "C"
{
    EXPORTED RelayboxPlugin* pluginEntry()
    {
        return new RelayboxPlugin();
    }

    EXPORTED void pluginExit( RelayboxPlugin *ptrPlugin )
    {
        if (nullptr != ptrPlugin )
        {
            delete ptrPlugin;
        }
    }
}


///////////////////////////////////////////////////////////////////
//                          INIT / CLEANUP                       //
///////////////////////////////////////////////////////////////////

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Function where to execute initialization of sub-modules
*/
/*--------------------------------------------------------------------------------------------------------*/

bool RelayboxPlugin::doInit(void *pvUserData)
{
    m_bIsInitialized = true;

    do {

#if !defined(_MSC_VER)
        if (0 != pthread_mutex_init( &m_mutexLock, NULL ) )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Mutex initialization failed"));
            m_bIsInitialized = false;
            break;
        }
#endif

        try
        {
            m_hFtdi245hdl = new ftdi245hdl(m_strSerialNumber, m_iVendorID, m_iProductID, m_iMaxNrRelays);
        }
        catch ( const std::system_error& ex )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FTDI handler: init failed, system error:"); LOG_STRING(ex.what()));
            m_bIsInitialized = false;
        }
        catch ( const std::runtime_error& ex )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FTDI handler: init failed, runtime error:"); LOG_STRING(ex.what()));
            m_bIsInitialized = false;
        }

        if (false == m_bIsInitialized )
        {
            if (nullptr != m_hFtdi245hdl )
            {
                delete m_hFtdi245hdl;
                m_hFtdi245hdl = nullptr;
            }
        }

    } while(false);

    return m_bIsInitialized;

}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Function where to execute de-initialization of sub-modules
*/
/*--------------------------------------------------------------------------------------------------------*/

void RelayboxPlugin::doCleanup(void)
{
    if (nullptr != m_hFtdi245hdl )
    {
        if (false == m_hFtdi245hdl->GetRelaysStates() )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to get relays status"));
        }
    }

    if (false == m_vThreadArray.empty() )
    {
#if defined(_MSC_VER)
        for( unsigned int i = 0; i < m_vThreadArray.size(); ++i)
        {
            m_vThreadArray.at(i).join();
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("std::thread.join OK"));
        }
#else
        for( auto th : m_vThreadArray )
        {
            void *pvJoinRetVal = nullptr;

            if (0 != pthread_join(th, &pvJoinRetVal) )
            {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("pthread_join:failed"));
            }
            else
            {
                LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("pthread_join OK"));
            }
        }
#endif
    }

#if !defined(_MSC_VER)
    pthread_mutex_destroy(&m_mutexLock);
#endif

    if (nullptr != m_hFtdi245hdl )
    {
        delete m_hFtdi245hdl;
    }

    m_bIsInitialized = false;
    m_bIsEnabled     = false;

}

///////////////////////////////////////////////////////////////////
//                          COMMAND HANDLERS                     //
///////////////////////////////////////////////////////////////////

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief INFO command implementation; shows details about plugin and
  *        describe the supported functions with examples of usage.
  *        This command takes no arguments and is executed even if the plugin initialization fails
  *
  * \note Usage example: <br>
  *       RELAYBOX.INFO
  *
  * \param[in] pstrArgs NULL (NULL means that no arguments are provided to this function)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool RelayboxPlugin::m_Relaybox_INFO (const std::string &args) const
{
    bool bRetVal = false;

    do {

        // expected no arguments
        if (false == args.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected no argument(s)"));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled )
        {
            bRetVal = true;
            break;
        }

        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Version:"); LOG_STRING(m_strPluginVersion));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Description: switch relays on/off"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("SWITCH : switch [relay] to [state] with optional delay(ms)"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : relay_idx(1..N) state(0,1) [delay]"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: RELAYBOX.SWITCH 1 1 (1st relay ON"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       RELAYBOX.SWITCH 3 0 2000 (3rd relay OFF after 2sec"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("SWITCHALL : switch all relays to on/off with optional delay(ms)"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : state(0,1) [delay]"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: RELAYBOX.SWITCHALL 0 (all relays OFF"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       RELAYBOX.SWITCHALL 1 2000 (all relays ON after 2sec"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("STATUS : show the status of all relays: 0-off, 1-on"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : none"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: RELAYBOX.STATUS"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Note : if a delay is provided then the command returns"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       immediatelly but the action is executed when the delay expires"));

        bRetVal = true;

    } while(false);

    return bRetVal;

}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief SWITCH command implementation; used to switch on/off
  *        the relay at the given index
  *
  * \note Usage example: <br>
  *       RELAYBOX.SWITCH 1 1 (relay 1 to ON) <br>
  *       RELAYBOX.SWITCH 3 0 (relay 3 to OFF)
  *
  * \param[in] pstrArgs 2 space separated numbers: relay_index(1..8) relay_state(0/1)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool RelayboxPlugin::m_Relaybox_SWITCH (const std::string &args) const
{
    bool bRetVal = false;

    do {

        // expecting arguments, fail if not provided
        if (true == args.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expecting arguments: relay(1..8) state(0-1) [delay]"));
            break;
        }

        std::vector<std::string> vstrArgs;
        ustring::tokenizeSpaceQuotesAware(args, vstrArgs);
        size_t szNrArgs = vstrArgs.size();

        // 2 or 3 arguments are expected
        if ((szNrArgs < 2) || (szNrArgs > 3) )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expecting 2..3 arguments: relay(1..8) state(0-1) [delay]"));
            break;
        }

        uint32_t u32Channel = 0;
        uint32_t u32State   = 0;
        uint32_t u32Delay   = 0;

        // convert relay and state
        if ((false == numeric::str2uint32( vstrArgs[0], u32Channel)) || (false == numeric::str2uint32( vstrArgs[1], u32State)) )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid value(s) of channel or state:"); LOG_STRING(vstrArgs[0]); LOG_STRING(vstrArgs[1]));
            break;
        }

        // convert the optional delay
        if (3 == szNrArgs )
        {
            if (false == numeric::str2uint32(vstrArgs[2], u32Delay) )
            {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid value of delay:"); LOG_STRING(vstrArgs[2]));
                break;
            }
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled )
        {
            bRetVal = true;
            break;
        }

#if defined(_MSC_VER)
        std::lock_guard<std::mutex> guard(m_mutexLock);
#endif
        bRetVal = m_RelayHandling( u32Channel, u32State, u32Delay );

    } while(false);

    return bRetVal;

}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief SWITCHALL command implementation; used to switch on/off all relays at once;
  *        Useful to set all relays to OFF at startup, etc.
  *
  * \note Usage example:  <br>
  *        RELAYBOX.SWITCHALL 0 (all to OFF) <br>
  *        RELAYBOX.SWITCHALL 1 (all to ON)
  *
  * \param[in] pstrArgs relays_state(0/1)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool RelayboxPlugin::m_Relaybox_SWITCHALL (const std::string &args) const
{
    bool bRetVal = false;

    do {

        // expecting arguments, fail if not provided
        if (true == args.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expecting arguments: state(0-1)"));
            break;
        }

        std::vector<std::string> vstrArgs;
        ustring::tokenizeSpaceQuotesAware(args, vstrArgs);
        size_t szNrArgs = vstrArgs.size();

        // 1 or 2 arguments are expected
        if ((szNrArgs < 1) || (szNrArgs > 2) )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expecting 1..2 arguments: state(0-1) [delay]"));
            break;
        }

        uint32_t u32State = 0;
        uint32_t u32Delay = 0;

        // convert the state
        if (false == numeric::str2uint32( vstrArgs[0], u32State))
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid value of state"); );
            break;
        }

        // convert the optional delay
        if (2 == szNrArgs )
        {
            if (false == numeric::str2uint32( vstrArgs[1], u32Delay) )
            {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid value of delay:"); LOG_STRING(vstrArgs[1]));
                break;
            }
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled )
        {
            bRetVal = true;
            break;
        }

#if defined(_MSC_VER)
        std::lock_guard<std::mutex> guard(m_mutexLock);
#endif
        // trigger the command execution
        bRetVal = m_RelayHandling( RELAY_ALL_CHANNELS, u32State, u32Delay );

    } while(false);

    return bRetVal;

}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief STATUS command implementation;
  *        Show the status of the relays
  *
  * \note Usage example:  <br>
  *        RELAYBOX.STATUS
  *
  * \param[in] none
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool RelayboxPlugin::m_Relaybox_STATUS (const std::string &args) const
{
    bool bRetVal = false;

    do {

        // expected no arguments
        if (false == args.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected no argument(s)"));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled )
        {
            bRetVal = true;
            break;
        }

        if (nullptr == m_hFtdi245hdl )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Device not ready!"));
            break;
        }

        bRetVal = m_hFtdi245hdl->GetRelaysStates();

    } while(false);

    return bRetVal;

}



///////////////////////////////////////////////////////////////////
//            PRIVATE INTERFACES IMPLEMENTATION                  //
///////////////////////////////////////////////////////////////////

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief delayed execution thread's callback
  * \param[in] u32Delay delay of the command
  *            uiRelayState the state to be set to the relay
  * \return void
*/
/*--------------------------------------------------------------------------------------------------------*/

void* RelayboxPlugin::m_threadCB( void *pvThreadArgs )
{
    uint32_t u32Channel = 0;
    uint32_t u32State = 0;
    uint32_t u32Delay = 0;

    // get access to the thread arguments
    RelayboxPlugin *psArgs = static_cast<RelayboxPlugin*>(pvThreadArgs);
    psArgs->m_getRelayParams( &u32Channel, &u32State, &u32Delay );

    if (RELAY_ALL_CHANNELS == u32Channel )
    {
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("(T) Relay(all)"); LOG_STRING("| State:"); LOG_UINT32(u32State); LOG_STRING("| Delay:"); LOG_UINT32(u32Delay));
    }
    else
    {
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("(T) Relay:"); LOG_UINT32(u32Channel); LOG_STRING("| State:"); LOG_UINT32(u32State); LOG_STRING("| Delay:"); LOG_UINT32(u32Delay));
    }

    // now the thread has read the values, we can release the mutex
#if !defined(_MSC_VER)
    if (0 != pthread_mutex_unlock(&(psArgs->m_mutexLock)) )
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("(T) Failed to unlock mutex"));
    }
#endif

    // sleep the requested delay
    utime::delay_ms(u32Delay);

    // execute the command after delay
    bool bRetVal = (RELAY_ALL_CHANNELS == u32Channel) ? psArgs->m_hFtdi245hdl->SetAllState(u32State) : psArgs->m_hFtdi245hdl->SetRelayState(u32Channel, u32State);
    LOG_PRINT(((false == bRetVal) ? LOG_ERROR : LOG_VERBOSE), LOG_HDR; LOG_STRING("(T) Delayed execution ["); LOG_UINT32(u32Delay); LOG_STRING(((false == bRetVal) ? "] FAILED":"] OK")));

    return nullptr;

}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief common function shared by PowerOn/Off commands
  * \param[in] u32Delay delay of the command
  *            uiRelayState the state to be set to the relay
  * \return void
*/
/*--------------------------------------------------------------------------------------------------------*/

bool RelayboxPlugin::m_RelayHandling( const uint32_t u32Channel, const uint32_t u32State, const uint32_t u32Delay) const
{
    bool bRetVal = false;

    do {

        if (nullptr == m_hFtdi245hdl )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Device not ready!"));
            break;
        }

        // if no delay is provided, then execute the command immediatelly
        if (0 == u32Delay )
        {
            bRetVal = (RELAY_ALL_CHANNELS == u32Channel) ? m_hFtdi245hdl->SetAllState(u32State) : m_hFtdi245hdl->SetRelayState(u32Channel, u32State);
            break;
        }

        /* lock the access to the values until the thread read them */
#if !defined(_MSC_VER)
        if (0 != pthread_mutex_lock(&m_mutexLock) )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to lock mutex"));
            break;
        }
#endif

        // otherwise create a thread and call the command from it after a delay
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Relay switch delayed with:"); LOG_UINT32(u32Delay));

        m_u32Delay   = u32Delay;
        m_u32State   = u32State;
        m_u32Channel = u32Channel;
        THREADFUNCPTR pfctThreadCB = (THREADFUNCPTR) &RelayboxPlugin::m_threadCB;

        // create the thread and add it to the vector of threads for joining them later ..
#if defined(_MSC_VER)
        std::thread threadExec(pfctThreadCB, const_cast<RelayboxPlugin*>(this));
        m_vThreadArray.push_back(std::move(threadExec));
#else
        pthread_t threadExec;
        if (0 != pthread_create( &threadExec, NULL, pfctThreadCB, const_cast<RelayboxPlugin*>(this) ))
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to create thread ..."));
            break;
        }
        m_vThreadArray.push_back(threadExec);
#endif

        // regardless of what will happen in the thread, return true here
        bRetVal = true;

    } while (false);

    return bRetVal;

}


///////////////////////////////////////////////////////////////////
//            PRIVATE INTERFACES IMPLEMENTATION                  //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------------------------------------*/

bool RelayboxPlugin::m_LocalSetParams( const PluginDataSet *psSetParams)
{
    bool bRetVal = false;

    if (false == psSetParams->mapSettings.empty()) {
        do {
            if (psSetParams->mapSettings.count(SERIAL_NUMBER) > 0) {
                m_strSerialNumber = psSetParams->mapSettings.at(SERIAL_NUMBER);
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("SerialNr :"); LOG_STRING(m_strSerialNumber));
            }

            if (psSetParams->mapSettings.count(VENDOR_ID) > 0) {
                if (false == numeric::str2int(psSetParams->mapSettings.at(VENDOR_ID), m_iVendorID)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("VendorID :"); LOG_INT(m_iVendorID));
            }

            if (psSetParams->mapSettings.count(PRODUCT_ID) > 0) {
                if (false == numeric::str2int(psSetParams->mapSettings.at(PRODUCT_ID), m_iProductID)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("ProdID :"); LOG_INT(m_iProductID));
            }

            if (psSetParams->mapSettings.count(NR_CHANNELS) > 0) {
                if (false == numeric::str2int(psSetParams->mapSettings.at(NR_CHANNELS), m_iMaxNrRelays)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("NrChannels :"); LOG_INT(m_iMaxNrRelays));
            }

            bRetVal = true;

        } while(false);
    } else {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
        bRetVal = true;
    }

    return bRetVal;

} /* m_LocalSetParams() */
