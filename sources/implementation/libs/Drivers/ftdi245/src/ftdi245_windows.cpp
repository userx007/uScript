
#include "ftdi245_windows.hpp"
#include "uError.hpp"
#include "uLogger.hpp"

#include <winusb.h>

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
//            LOCAL DEFINES AND DATA TYPES                       //
///////////////////////////////////////////////////////////////////

#define EP_ADDRESS                                           (0x02)
#define FTDI_VENDOR_ID                              ((int)(0x0403))
#define FTDI_PROD_ID                                ((int)(0x6001))

///////////////////////////////////////////////////////////////////
//            PUBLIC INTERFACES IMPLEMENTATION                   //
///////////////////////////////////////////////////////////////////


/*
 * \brief class constructor
 */

ftdi245hdl::ftdi245hdl( const std::string& strSerialNumber, const INT iVendorID, const INT iProdID, const INT iMaxNrRelays)
                                                        : m_strSerialNumber( strSerialNumber )
                                                        , m_iProductID     ( iProdID )
                                                        , m_iVendorID      ( iVendorID )
                                                        , m_iMaxNrRelays   ( iMaxNrRelays )
                                                        , m_pUsbLibApi     ( nullptr )
                                                        , m_psDeviceInfo   ( nullptr )
                                                        , m_pvUsbHandle    ( nullptr )
                                                        , m_pvDeviceList   ( nullptr )
                                                        , m_bReady         ( FALSE )
{
    BOOL bRetVal = FALSE;

    do {

        // try to unload the library if exist in order to ensure a clean usage
        m_ForceUnloadLibrary( LIBUSBK_DLL_NAME );

        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Loading"); LOG_STRING(LIBUSBK_DLL_NAME); LOG_STRING("..."));
        // try to load the DLL and get the entry points of the necessary symbols
        try
        {
            m_pUsbLibApi = new LibUsbKApi();
        }
        catch ( const std::system_error& ex )
        {
            LOG_PRINT(LOG_FATAL, LOG_HDR; LOG_STRING("Failed to get FTDI driver symbols. Error:"); LOG_STRING(ex.what()));
            break;
        }

        // just in case, abort if no valid pointer was obtained
        if( nullptr == m_pUsbLibApi )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("m_pUsbLibApi: nullptr after loading FTDI library"));
            break;
        }

        // try to get the uniqued device (abort if none or more than one devices are found )
        if( FALSE == m_GetUniqueDevice() )
        {
            break;
        }

        if( nullptr == m_psDeviceInfo )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("m_psDeviceInfo nullptr after getting device"));
            break;
        }

        // get the driver name
        const char *pstrDriverName = nullptr;
        switch(m_psDeviceInfo->DriverID)
        {
            case DRVID_LIBUSBK:        { pstrDriverName = "libusbK"; break; }
            case DRVID_LIBUSB0:        { pstrDriverName = "libusb0"; break; }
            case DRVID_WINUSB:         { pstrDriverName = "WinUSB";  break; }
            case DRVID_LIBUSB0_FILTER: { pstrDriverName = "libusb0/filter"; break; }
            default:                   { pstrDriverName = "unknown"; break; }
        }

        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Loading driver API:"); LOG_STRING(pstrDriverName));

        // load the dynamic driver API
        if( FALSE == m_pUsbLibApi->pfLoadDrvApi(&m_sUsb, m_psDeviceInfo->DriverID) )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Loading driver API failed:"); LOG_STRING(uerror::getLastError()));
            break;
        }

        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Driver API loaded, initializing.."));

        // try to initialize the device; this creates the physical usb handle.
        if ( FALSE == m_sUsb.Init(&m_pvUsbHandle, m_psDeviceInfo))
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("USB device init failed."); LOG_STRING(uerror::getLastError()));
            break;
        }

        if( nullptr == m_pvUsbHandle )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("USB device handler invalid"));
            break;
        }

        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("USB device opened and ready"));

        // get the device descriptor
        if ( FALSE == m_GetDescriptor () )
        {
            break;
        }

        // set the flow control
        if ( FALSE == m_SetFlowControl( SIO_DISABLE_FLOW_CTRL ) )
        {
            break;
        }

        // if no break occured so far then the initialization is supposed to be OK
        bRetVal = TRUE;

    } while(FALSE);

    // initialization failed, throw an exception to be handled by the caller
    if( FALSE == bRetVal )
    {
        throw std::runtime_error("USB device setup failed");
    }

    m_bReady = true;

}


/*
 * \brief class destructor
 */

ftdi245hdl::~ftdi245hdl()
{

    if( nullptr != m_pvDeviceList )
    {
        m_pUsbLibApi->pfLstFree(m_pvDeviceList);
        m_pvDeviceList = nullptr;
    }

    if( nullptr != m_pvUsbHandle )
    {
        m_sUsb.Free(m_pvUsbHandle);
        m_pvUsbHandle = nullptr;
    }

    if( nullptr != m_pUsbLibApi )
    {
        delete m_pUsbLibApi;
        m_pUsbLibApi = nullptr;
    }

}


/*
 * \brief Interface used to change the state of a relay
 * \param[in] uiRelay relay index
 * \param[in] uiState relay state
 * \return TRUE on success, FALSE otherwise
 */

BOOL ftdi245hdl::SetRelayState( const UINT uiRelay, const UINT uiState ) const
{
    BOOL bRetVal = FALSE;

    do {

        if( FALSE == m_bReady )
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
        if( FALSE == m_SetBitMode(0xFF, BITMODE_BITBANG) )
        {
            break;
        }

        UCHAR ucPins   = 0;
        UINT uiWritten = 0;

        // read the current status of the pins to preserve the already set ones
        if( FALSE == m_ReadPins(&ucPins) )
        {
            break;
        };

        // modify the state of the requested pin
        if (uiState) { ucPins |=  (1 << (uiRelay - 1));}
        else         { ucPins &= ~(1 << (uiRelay - 1));}

        // write the new state to the device
        if( FALSE == m_WriteData(&ucPins, 1, &uiWritten) )
        {
            break;
        }

        // verify the writting status
        if( 1 != uiWritten )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SetRelayState: write incomplete:"); LOG_UINT16(uiWritten));
            break;
        }

        bRetVal = TRUE;

    } while(FALSE);

    LOG_PRINT(((FALSE == bRetVal) ? LOG_ERROR : LOG_VERBOSE), LOG_HDR; LOG_STRING("SetRelayState:"); LOG_UINT16(uiRelay); LOG_UINT16(uiState); LOG_STRING(((FALSE == bRetVal) ? "FAILED":"OK")));

    // show the current state of all relays
    if( TRUE == bRetVal )
    {
        GetRelaysStates();
    }

    return bRetVal;

}


/*
 * \brief Interface used to change the state of all relays at once
 * \param[in] uiState relay state
 * \return TRUE on success, FALSE otherwise
 */

BOOL ftdi245hdl::SetAllState( const UINT uiState ) const
{
    BOOL bRetVal = FALSE;

    do {

        if( FALSE == m_bReady )
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
        if( FALSE == m_SetBitMode(0xFF, BITMODE_BITBANG) )
        {
            break;
        };

        UCHAR ucPins   = 0;
        UINT uiWritten = 0;

        // modify the state of all relays
        for( UINT uiRelayIdx = 0; uiRelayIdx < m_iMaxNrRelays; ++uiRelayIdx )
        {
            if (uiState) { ucPins |=  (1 << uiRelayIdx);}  // set pin
            else         { ucPins &= ~(1 << uiRelayIdx);}  // reset pin
        }

        // write the new state to the device
        if( FALSE == m_WriteData(&ucPins, 1, &uiWritten) )
        {
            break;
        }

        // verify the writting status
        if( 1 != uiWritten )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SetAllState: write incomplete:"); LOG_UINT16(uiWritten));
            break;
        }

        bRetVal = TRUE;

    } while(FALSE);

    LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("SetAllState:"); LOG_UINT16(uiState); LOG_STRING((TRUE == bRetVal) ? "OK" : "FAIL"));

    // show the current state of all relays
    if(TRUE == bRetVal)
    {
        GetRelaysStates();
    }

    return bRetVal;

}


/*
 * \brief Get the state of the relays
 * \param none
 * \return TRUE on success, FALSE otherwise
 */

BOOL ftdi245hdl::GetRelaysStates( VOID ) const
{
    BOOL bRetVal = FALSE;

    do {

        if( FALSE == m_bReady )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("GetRelaysStates: Device not ready!"));
            break;
        }

        UCHAR ucPins = 0;

        // read the current status of the pins
        if( FALSE == m_ReadPins( &ucPins ) )
        {
            break;
        };

        std::string strStates;
        // scan the states and store them
        for( UINT uiRelayIdx = 0; uiRelayIdx < m_iMaxNrRelays; ++uiRelayIdx )
        {
            UCHAR ucPinState = ucPins & (1 << uiRelayIdx);
            strStates.append(std::to_string((0 == ucPinState) ? 0 : 1) );
            strStates.append(" ");
        }

        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Relays [ 1 .."); LOG_INT(m_iMaxNrRelays); LOG_STRING("] :"); LOG_STRING(strStates));

        bRetVal = TRUE;

    } while(FALSE);

    return bRetVal;

}


/**
 * \brief Interface used to get the device descriptor
 * \param none
 * \return TRUE on success, FALSE otherwise
 */

BOOL ftdi245hdl::m_GetDescriptor( VOID )
{
    BOOL bRetVal = TRUE;
    WINUSB_SETUP_PACKET sSetupPacket;
    USB_DEVICE_DESCRIPTOR sDeviceDescriptor;

    ZeroMemory(&sSetupPacket, sizeof(WINUSB_SETUP_PACKET));
    ZeroMemory(&sDeviceDescriptor, sizeof(USB_DEVICE_DESCRIPTOR));

    sSetupPacket.RequestType = 0x80; // DIR_DEVICE_TO_HOST, TYPE_STANDARD, RCPT_DEVICE
    sSetupPacket.Value       = USB_DESCRIPTOR_MAKE_TYPE_AND_INDEX(USB_DESCRIPTOR_TYPE_DEVICE, 0);
    sSetupPacket.Request     = USB_REQUEST_GET_DESCRIPTOR;
    sSetupPacket.Length      = sizeof(sDeviceDescriptor);

    if (FALSE == (bRetVal = m_sUsb.ControlTransfer(m_pvUsbHandle, sSetupPacket, (PUCHAR)&sDeviceDescriptor, sizeof(sDeviceDescriptor), NULL, NULL)))
    {
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("GetDescriptor failed:"); LOG_STRING(uerror::getLastError()));
    }

    return bRetVal;

}


/*
 * \brief Interface used to set the flow control
 * \param uiFlowCtrl
 * \return TRUE on success, FALSE otherwise
 */

BOOL ftdi245hdl::m_SetFlowControl( UINT uiFlowCtrl )
{
    BOOL bRetVal = TRUE;
    WINUSB_SETUP_PACKET sSetupPacket;

    ZeroMemory(&sSetupPacket, sizeof(WINUSB_SETUP_PACKET));
    sSetupPacket.RequestType = 0x40; // DIR_HOST_TO_DEVICE, TYPE_VENDOR, RCPT_DEVICE
    sSetupPacket.Request     = SIO_SET_FLOW_CTRL_REQUEST;
    sSetupPacket.Value       = 0;
    sSetupPacket.Index       = uiFlowCtrl;
    sSetupPacket.Length      = 0;

    if (FALSE == (bRetVal = m_sUsb.ControlTransfer(m_pvUsbHandle, sSetupPacket, NULL, 0, NULL, NULL)))
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SetFlowControl failed:"); LOG_STRING(uerror::getLastError()));
    }

    return bRetVal;

}


/*
 * \brief Interface used to set the bitbang mode
 * \param[in] ucBitmask
 * \param[in] ucMode
 * \return TRUE on success, FALSE otherwise
 */

BOOL ftdi245hdl::m_SetBitMode( UCHAR ucBitmask, UCHAR ucMode ) const
{
    BOOL bRetVal = TRUE;
    WINUSB_SETUP_PACKET sSetupPacket = { 0 };

    USHORT usValue = ucBitmask; // low byte: ucBitmask
    usValue |= (ucMode << 8);

    ZeroMemory(&sSetupPacket, sizeof(WINUSB_SETUP_PACKET));
    sSetupPacket.RequestType = 0x40; // DIR_HOST_TO_DEVICE, TYPE_VENDOR, RCPT_DEVICE
    sSetupPacket.Request     = SIO_SET_BITMODE_REQUEST;
    sSetupPacket.Value       = usValue;
    sSetupPacket.Index       = 0;
    sSetupPacket.Length      = 0;

    if (FALSE == (bRetVal = bRetVal = m_sUsb.ControlTransfer(m_pvUsbHandle, sSetupPacket, NULL, 0, NULL, NULL)))
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SetBitMode failed"); LOG_STRING(uerror::getLastError()));
    }

    return bRetVal;

}


/*
 * \brief Interface used to disable the bitbang mode
 * \param none
 * \return TRUE on success, FALSE otherwise
 */

BOOL ftdi245hdl::m_DisableBitMode( VOID )
{
    BOOL bRetVal = TRUE;
    WINUSB_SETUP_PACKET sSetupPacket = { 0 };

    ZeroMemory(&sSetupPacket, sizeof(WINUSB_SETUP_PACKET));
    sSetupPacket.RequestType = 0x40; // DIR_HOST_TO_DEVICE, TYPE_VENDOR, RCPT_DEVICE
    sSetupPacket.Request     = SIO_SET_BITMODE_REQUEST;
    sSetupPacket.Value       = 0;
    sSetupPacket.Index       = 0;
    sSetupPacket.Length      = 0;

    if (FALSE == (bRetVal = m_sUsb.ControlTransfer(m_pvUsbHandle, sSetupPacket, NULL, 0, NULL, NULL)))
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("DisableBitMode failed"); LOG_STRING(uerror::getLastError()));
    }

    return bRetVal;

}


/*
 * \brief Interface used to read the current state of the IO pins
 * \param[out] pucPins pointer to an UCHAR variable where to read the status of the pins
 * \return TRUE on success, FALSE otherwise
 */

BOOL ftdi245hdl::m_ReadPins ( UCHAR *pucPins ) const
{
    BOOL bRetVal = TRUE;
    WINUSB_SETUP_PACKET sSetupPacket = { 0 };

    ZeroMemory(&sSetupPacket, sizeof(WINUSB_SETUP_PACKET));
    sSetupPacket.RequestType = 0xC0; // DIR_DEVICE_TO_HOST, TYPE_VENDOR, RCPT_DEVICE
    sSetupPacket.Request     = SIO_READ_PINS_REQUEST;
    sSetupPacket.Value       = 0;
    sSetupPacket.Index       = 0;
    sSetupPacket.Length      = 1;

    if (FALSE == (bRetVal = m_sUsb.ControlTransfer(m_pvUsbHandle, sSetupPacket, pucPins, 1, NULL, NULL)))
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("ReadPins failed:"); LOG_STRING(uerror::getLastError()));
    }

    return bRetVal;

}


/*
 * \brief Interface used to write data to the device
 * \param[in] pucBuffer buffer with data to be written
 * \param[in] uiBufferSize size of the data in the buffer
 * \param[out] puiTransferredLength size of data reported as transferred
 * \return TRUE on success, FALSE otherwise
 */

BOOL ftdi245hdl::m_WriteData( UCHAR *pucBuffer, UINT uiBufferSize, UINT *puiTransferredLength ) const
{
    BOOL bRetVal = FALSE;

    if (USB_ENDPOINT_DIRECTION_OUT(EP_ADDRESS))
    {
        bRetVal = m_sUsb.WritePipe(m_pvUsbHandle, EP_ADDRESS, pucBuffer, uiBufferSize, puiTransferredLength, NULL);

        if (FALSE == bRetVal)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("WriteData failed:"); LOG_STRING(uerror::getLastError()));
        }
    }
    else
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("WriteData wrong address for output:"); LOG_UINT32((uint32_t)EP_ADDRESS));
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("WriteData"); LOG_STRING((TRUE == bRetVal) ? "OK" : "FAIL"));

    return bRetVal;

}


/*
 * \brief Interface used to get the device to be actually used
 * \param none
 * \return TRUE on success, FALSE otherwise
 */

BOOL ftdi245hdl::m_GetUniqueDevice( VOID )
{
    UINT uiDeviceCount = 0;
    BOOL bRetVal = FALSE;

    do {

        // get the list with all usb devices libusbK can access.
        if ((nullptr == m_pUsbLibApi) || (FALSE == m_pUsbLibApi->pfLstInit(&m_pvDeviceList, LSTFLAG_NONE)) )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to get device list:"); LOG_STRING(uerror::getLastError()));
            break;
        }

        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Getting devices count.."));

        // get the number of devices contained in the device list.
        if ((nullptr == m_pUsbLibApi) || (FALSE == m_pUsbLibApi->pfLstCount(m_pvDeviceList, &uiDeviceCount)) )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to get devices count:"); LOG_STRING(uerror::getLastError()));
            break;
        }

        // no device connected, NOK
        if( 0 == uiDeviceCount )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("No devices connected"));
            break;
        }

        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Found devices:"); LOG_UINT32(uiDeviceCount));

        // enumerate all the devices found
        if ((nullptr == m_pUsbLibApi) || (FALSE == m_pUsbLibApi->pfLstEnumerate(m_pvDeviceList, m_ShowDevicesCB, NULL)) )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to enumerate devices:"); LOG_STRING(uerror::getLastError()));
            break;
        }

        // one device connected; if a serial number is provided then check if they match
        if( 1 == uiDeviceCount )
        {
            // this will move the position before the first element in the list
            m_pUsbLibApi->pfLstMoveReset(m_pvDeviceList);

            // try to advance to the first position in the list
            if( TRUE == m_pUsbLibApi->pfLstMoveNext(m_pvDeviceList, &m_psDeviceInfo) )
            {
                // device serial number provided, check if matches with the detected one
                if( FALSE == m_strSerialNumber.empty() )
                {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Expecting device with serial number:"); LOG_STRING(m_strSerialNumber));

                    // mismatch of the serial numbers
                    if( 0 != m_strSerialNumber.compare(m_psDeviceInfo->SerialNumber) )
                    {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Device serial numbers mismatch! Found:"); LOG_STRING(m_psDeviceInfo->SerialNumber); LOG_STRING("Expected:"); LOG_STRING(m_strSerialNumber));
                        break;
                    }
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Using device with serial number:"); LOG_STRING(m_psDeviceInfo->SerialNumber));

                // product ID provided, check if it matches with the provided one
                if( 0 != m_iProductID )
                {
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("ProdID provided, used for filtering:"); LOG_HEX32(m_iProductID));

                    if( m_iProductID != m_psDeviceInfo->Common.Pid )
                    {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Device ProdID mismatch! Found:"); LOG_HEX32(m_psDeviceInfo->Common.Pid); LOG_STRING("Expected:"); LOG_HEX32(m_iProductID));
                        break;
                    }
                }
            }
            else
            {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("pfLstMoveNext failed:"); LOG_STRING(uerror::getLastError()));
                break;
            }

            bRetVal = TRUE;
            break;
        }

        // more devices are present but neither serial number / product id was given for filtering purpose
        if ((TRUE == m_strSerialNumber.empty()) && (0 == m_iProductID) )
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_UINT32(uiDeviceCount); LOG_STRING("devices connected, serial number and/or product ID needed!"));
            m_psDeviceInfo = nullptr;
            break;
        }

        // shows the filtering criteria
        if ((FALSE == m_strSerialNumber.empty()) && (0 != m_iProductID) )
        {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_UINT32(uiDeviceCount); LOG_STRING("devices connected, filtering by SN/PID:"); LOG_STRING(m_strSerialNumber); LOG_HEX32(m_iProductID));
        }
        else if( FALSE == m_strSerialNumber.empty() )
        {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_UINT32(uiDeviceCount); LOG_STRING("devices connected, filtering by SN:"); LOG_STRING(m_strSerialNumber));
        }
        else
        {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_UINT32(uiDeviceCount); LOG_STRING("devices connected, filtering by PID:"); LOG_HEX32(m_iProductID));
        }

        if( FALSE == m_FindDevice( ))
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_UINT32(uiDeviceCount); LOG_STRING("Found no device with serial number:"); LOG_STRING(m_strSerialNumber));
            break;
        }

        // everything OK so far, m_psDeviceInfo shall contain now the device information
        bRetVal = TRUE;

    } while(FALSE);

    return bRetVal;

}


/*
 * \brief Callback used to show details about the discovered devices
 * \param pvDeviceList pointer to the device list structure, actually unused
 * \param psDeviceInfo pointer to the device info structure
 * \param pvContext pointer to the context structure, actually unused
 * \return TRUE on success, FALSE otherwise
 */

BOOL ftdi245hdl::m_ShowDevicesCB( VOID *pvDeviceList, DeviceInfo_s *psDeviceInfo, VOID *pvContext )
{
    // print some information about the device.
    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("---- Begin device info -----"));
    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("VendorID  :"); LOG_HEX32(psDeviceInfo->Common.Vid));
    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("ProdID    :"); LOG_HEX32(psDeviceInfo->Common.Pid));
    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("InstID    :"); LOG_STRING(psDeviceInfo->Common.InstanceID));
    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Device    :"); LOG_STRING(psDeviceInfo->DeviceDesc));
    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Manufact  :"); LOG_STRING(psDeviceInfo->Mfg));
    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("SerialNo  :"); LOG_STRING(psDeviceInfo->SerialNumber));
    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("DriverID  :"); LOG_HEX32(psDeviceInfo->DriverID));
    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("DeviceAddr:"); LOG_HEX32(psDeviceInfo->DeviceAddress));
    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Connected :"); LOG_STRING(((TRUE == psDeviceInfo->Connected) ? "yes" : "no")));
    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("---- End device info -----"));

    // must return always TRUE because with FALSE the enumeration is aborted.
    return TRUE;

}


/*
 * \brief Find the device with the given serial number
 * \return TRUE if the device was found, FALSE otherwise
 */

BOOL ftdi245hdl::m_FindDevice( VOID )
{
    BOOL bRetVal = FALSE;
    BOOL bSerialNrProvided = (FALSE == m_strSerialNumber.empty());
    BOOL bProductIdProvided = (0 != m_iProductID);

    // reset the search list
    m_pUsbLibApi->pfLstMoveReset(m_pvDeviceList);

    // loop through the available devices
    while( TRUE == m_pUsbLibApi->pfLstMoveNext(m_pvDeviceList, &m_psDeviceInfo) )
    {
        // both, serial number and product ID were provided
        if ((TRUE == bSerialNrProvided) && (TRUE == bProductIdProvided) )
        {
            if ((0 == m_strSerialNumber.compare(m_psDeviceInfo->SerialNumber)) && (m_iProductID == m_psDeviceInfo->Common.Pid) )
            {
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Using device PID/SN:"); LOG_HEX32(m_psDeviceInfo->Common.Pid); LOG_STRING(m_psDeviceInfo->SerialNumber));
                bRetVal = TRUE;
                break;
            }
        }

        // only serial number was provided
        else if( TRUE == bSerialNrProvided )
        {
            if( 0 == m_strSerialNumber.compare(m_psDeviceInfo->SerialNumber) )
            {
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Using device SN:"); LOG_STRING(m_psDeviceInfo->SerialNumber); LOG_STRING("[ PID:");LOG_HEX32(m_psDeviceInfo->Common.Pid);LOG_STRING("]"));
                bRetVal = TRUE;
                break;
            }
        }

        // only product ID was provided
        else // if( TRUE == bProductIdProvided )
        {
            if( m_iProductID == m_psDeviceInfo->Common.Pid )
            {
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Using device PID:"); LOG_HEX32(m_psDeviceInfo->Common.Pid); LOG_STRING("[ SN:"); LOG_STRING(m_psDeviceInfo->SerialNumber);LOG_STRING("]"));
                bRetVal = TRUE;
                break;
            }
        }
    }

    return bRetVal;

}


/*
 * \brief Force unloading a dll library
 * \param pstrModuleName the name of the module to be unloaded
 * \return none
 */

VOID ftdi245hdl::m_ForceUnloadLibrary ( const char * pstrModuleName ) const
{
    HMODULE hMod = NULL;

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(pstrModuleName); LOG_STRING(": Check/force unloading"));

    if( NULL == (hMod = GetModuleHandle(pstrModuleName)) )
    {
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(pstrModuleName); LOG_STRING(":"); LOG_STRING(uerror::getLastError()));
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(pstrModuleName); LOG_STRING(": Unloading not necessary, skipped."));
    }
    else
    {
        uint32_t uiCounter = 0;
        BOOL bRetVal = FALSE;

        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(pstrModuleName); LOG_STRING("found, unloading ..."));

        do {

            // try to unload the library
            bRetVal = FreeLibrary( hMod );

            // failed to unload, maybe the last instance was released already
            if( FALSE == bRetVal )
            {
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(pstrModuleName); LOG_STRING("unloading failed :"); LOG_STRING(uerror::getLastError()));
                break;
            }
            ++uiCounter;
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(pstrModuleName); LOG_STRING("unloaded successfuly"); LOG_UINT32(uiCounter); LOG_STRING("time(s)"));

        } while(TRUE);
    }
}

