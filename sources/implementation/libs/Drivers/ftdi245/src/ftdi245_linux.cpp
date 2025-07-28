
#include "ftdi245_linux.hpp"
#include "uLogger.hpp"

#include <stdexcept>

///////////////////////////////////////////////////////////////////
//                 DLT DEFINES                                   //
///////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR     "FTDI245    :"
#define LOG_HDR    LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//            PUBLIC INTERFACES IMPLEMENTATION                   //
///////////////////////////////////////////////////////////////////

/*
 * \brief class constructor
 */


ftdi245hdl::ftdi245hdl (const std::string& strSerialNumber, const int iVendorID, const int iProdID, const int iMaxNrRelays)
                                                        : m_strSerialNumber( strSerialNumber )
                                                        , m_iVendorID      ( iVendorID )
                                                        , m_iProductID     ( iProdID )
                                                        , m_iMaxNrRelays   ( iMaxNrRelays )
                                                        , m_pFtdiCtx       ( nullptr )
                                                        , m_bReady         ( false )
{
    bool bRetVal = false;

    do {

        // try to load the DLL and get the entry points of the necessary symbols
        try
        {
            m_pUsbLibApi = new LibFtdiApi();
        }
        catch ( const std::system_error& ex )
        {
            LOG_PRINT(LOG_FATAL, LOG_HDR; LOG_STRING("Failed to load/get symbols of FTDI driver. Error:"); LOG_STRING(ex.what()); LOG_STRING("Abort!"));
            break;
        }

        // just in case, abort if no valid pointer was obtained
        if( nullptr == m_pUsbLibApi )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to load shared library(nullptr). Abort!"));
            break;
        }

        // try to initialize the FTDI driver
        if (nullptr == (m_pFtdiCtx = m_pUsbLibApi->pfFtdiNew()))
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to initialize the FTDI driver. Abort!"));
            break;
        }

        // try to open the FTDI driver with the provided vid/pid
        int iFtdiRetVal = m_pUsbLibApi->pfFtdiOpen( m_pFtdiCtx, m_iVendorID, m_iProductID );

        // check the opening status
        if( iFtdiRetVal < 0 )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to open the FTDI device: Vid ="); LOG_HEX32(m_iVendorID); LOG_STRING("Pid ="); LOG_HEX32(m_iProductID); LOG_STRING("Abort!"));
            m_pUsbLibApi->pfFtdiFree(m_pFtdiCtx);
            m_pFtdiCtx = NULL;
            break;
        }

        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("FTDI device: Vid ="); LOG_HEX32(m_iVendorID); LOG_STRING("Pid ="); LOG_HEX32(m_iProductID); LOG_STRING("opened OK"));
        bRetVal = true;

    } while(false);

    // initialization failed, throw an exception to be handled by the caller
    if( false == bRetVal )
    {
        throw std::runtime_error("Ftdi245 device setup failed!");
    }

    m_bReady = true;

}


/*
 * \brief class destructor
 */

ftdi245hdl::~ftdi245hdl()
{
    if( nullptr != m_pUsbLibApi )
    {
        m_pUsbLibApi->pfFtdiClose( m_pFtdiCtx );
        m_pUsbLibApi->pfFtdiFree( m_pFtdiCtx );

        delete m_pUsbLibApi;
    }

}


/*
 * \brief Interface used to change the state of a relay
 * \param[in] uiRelay relay index
 * \param[in] uiState relay state
 * \return true on success, false otherwise
 */

bool ftdi245hdl::SetRelayState( const unsigned int uiRelay, const unsigned int uiState) const
{
    bool bRetVal = false;

    do {

        if( false == m_bReady )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SetRelayState: Device not ready!"));
            break;
        }

        if ((uiRelay < 1) || (uiRelay > m_iMaxNrRelays) || (uiState > 1) )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SetRelayState: Invalid arguments!"));
            break;
        }

        // set bitbang mode
        int iFtdiRetVal = m_pUsbLibApi->pfFtdiSetBitmode(m_pFtdiCtx, 0xFF, BITMODE_SYNCBB);
        if ( iFtdiRetVal < 0 )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SetRelayState::SetBitMode.Error:"); LOG_STRING(m_pUsbLibApi->pfFtdiGetErrString(m_pFtdiCtx)));
            break;
        }

        unsigned char ucPins   = 0;

        // read the current status of the pins to preserve the already set ones
        iFtdiRetVal = m_pUsbLibApi->pfFtdiReadPins(m_pFtdiCtx, &ucPins);
        if ( iFtdiRetVal < 0 )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SetRelayState::ReadPins.Error:"); LOG_STRING(m_pUsbLibApi->pfFtdiGetErrString(m_pFtdiCtx)));
            break;
        }

        // modify the state of the requested pin
        if (uiState) { ucPins |=  (1 << (uiRelay - 1));}
        else         { ucPins &= ~(1 << (uiRelay - 1));}

        // write the new state to the device
        iFtdiRetVal = m_pUsbLibApi->pfFtdiWriteData(m_pFtdiCtx, &ucPins, 1);
        if ( iFtdiRetVal < 0 )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SetRelayState::WriteData.Error:"); LOG_STRING(m_pUsbLibApi->pfFtdiGetErrString(m_pFtdiCtx)));
            break;
        }

        bRetVal = true;

    } while(false);

    LOG_PRINT(((false == bRetVal) ? LOG_ERROR : LOG_VERBOSE), LOG_HDR; LOG_STRING("SetRelayState:"); LOG_UINT16(uiRelay); LOG_UINT16(uiState); LOG_STRING(((false == bRetVal) ? "FAILED":"OK")));

    // show the current state of all relays
    if ( true == bRetVal )
    {
        GetRelaysStates();
    }

    return bRetVal;

}


/*
 * \brief Interface used to change the state of all relays at once
 * \param[in] uiState relay state
 * \return true on success, false otherwise
 */

bool ftdi245hdl::SetAllState( const unsigned int uiState ) const
{
    bool bRetVal = false;

    do {

        if( false == m_bReady )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SetAllState: Device not ready!"));
            break;
        }

        if( uiState > 1 )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SetAllState: Invalid arguments!"));
            break;
        }

        // set bitbang mode
        int iFtdiRetVal = m_pUsbLibApi->pfFtdiSetBitmode(m_pFtdiCtx, 0xFF, BITMODE_SYNCBB);

        if ( iFtdiRetVal < 0 )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SetAllState::SetBitMode.Error:"); LOG_STRING(m_pUsbLibApi->pfFtdiGetErrString(m_pFtdiCtx)));
            break;
        }

        unsigned char ucPins = 0;

        // modify the state of all relays
        for( unsigned int uiRelayIdx = 0; uiRelayIdx < m_iMaxNrRelays; ++uiRelayIdx )
        {
            if (uiState) { ucPins |=  (1 << uiRelayIdx);}  // set pin
            else         { ucPins &= ~(1 << uiRelayIdx);}  // reset pin
        }

        // write the new state to the device
        iFtdiRetVal = m_pUsbLibApi->pfFtdiWriteData(m_pFtdiCtx, &ucPins, 1);

        if ( iFtdiRetVal < 0 )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SetAllState::WriteData.Error:"); LOG_STRING(m_pUsbLibApi->pfFtdiGetErrString(m_pFtdiCtx)));
            break;
        }

        bRetVal = true;

    } while(false);

    LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("SetAllState:"); LOG_INT(uiState); LOG_STRING((true == bRetVal) ? "OK" : "FAIL"));

    // show the current state of all relays
    if ( true == bRetVal )
    {
        GetRelaysStates();
    }

    return bRetVal;

}


/*
 * \brief Get the state of the relays
 * \param none
 * \return true on success, false otherwise
 */

bool ftdi245hdl::GetRelaysStates( void ) const
{
    bool bRetVal = false;

    do {

        if( false == m_bReady )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("GetRelaysStates: Device not ready!"));
            break;
        }

        // set bitbang mode
        int iFtdiRetVal = m_pUsbLibApi->pfFtdiSetBitmode(m_pFtdiCtx, 0xFF, BITMODE_SYNCBB);

        if ( iFtdiRetVal < 0 )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("GetRelaysStates::SetBitMode.Error:"); LOG_STRING(m_pUsbLibApi->pfFtdiGetErrString(m_pFtdiCtx)));
            break;
        }

        unsigned char ucPins = 0;

        // read the current status of the pins to preserve the already set ones
        iFtdiRetVal = m_pUsbLibApi->pfFtdiReadPins(m_pFtdiCtx, &ucPins);
        if ( iFtdiRetVal < 0 )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("GetRelaysStates::ReadPins.Error:"); LOG_STRING(m_pUsbLibApi->pfFtdiGetErrString(m_pFtdiCtx)));
            break;
        }

        std::string strStates;

        // scan the states and store them
        for ( unsigned int uiRelayIdx = 0; uiRelayIdx < m_iMaxNrRelays; ++uiRelayIdx )
        {
            unsigned int ucPinState = ucPins & (1 << uiRelayIdx);
            strStates.append(std::to_string((0 == ucPinState) ? 0 : 1) );
            strStates.append(" ");
        }

        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Relays [ 1 .."); LOG_UINT32(m_iMaxNrRelays); LOG_STRING("] :"); LOG_STRING(strStates));

        bRetVal = true;

    } while(false);

    return bRetVal;

}



