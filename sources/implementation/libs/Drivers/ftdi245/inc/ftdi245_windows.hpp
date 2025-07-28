#ifndef FTDI245_WINDOWS_HPP
#define FTDI245_WINDOWS_HPP

#include "shlibifentry_windows.hpp"

#include <string>

////////////////////////////////////////////////////////////////////////////////////////////
//  Class implementing the control the FTDI245 chip under Windows
////////////////////////////////////////////////////////////////////////////////////////////

class ftdi245hdl
{

    public:

        /*
         * \brief The class constructor
         */

        ftdi245hdl( const std::string& strSerialNumber, const INT uiProdID );


        /*
         * \brief The class destructor
         */

        ~ftdi245hdl( );


        /*
         * \brief Interface to set the state of a relay
         * \param uiRelay the index of the relay to be switched
         * \param uiState the state in which the relay has to be switched
         * \return TRUE on success, FALSE otherwise
         */

        BOOL SetRelayState ( const UINT uiRelay, const UINT uiState ) const;

        /*
         * \brief Interface to set the state of all relays at once
         * \param uiState the state in which the relay has to be switched
         * \return TRUE on success, FALSE otherwise
         */

        BOOL SetAllState ( const UINT uiState) const;


        /*
         * \brief Get the supported number of relays
         * \param none
         * \return The supported number of relays
         */

        UINT GetMaxRelays ( VOID ) const { return m_uiMaxNrRelays; }

        /*
         * \brief Get the state of all relays
         * \param none
         * \return TRUE on success, FALSE otherwise
         */

        BOOL GetRelaysStates( VOID ) const;

    private:


        /** \brief Driver API instance
         */

        DriverApi_s m_sUsb;


        /** \brief Pointer to the device info structure
         */

        DeviceInfo_s *m_psDeviceInfo;


        /** \brief Handle of the API library
          */

        LibUsbKApi *m_pUsbLibApi;


        /** \brief USB device handle
          */

        VOID *m_pvUsbHandle;


        /** \brief USB device list
          */
        VOID *m_pvDeviceList;


        /** \brief The maximum number of supported relays
         */

        UINT m_uiMaxNrRelays;


        /** \brief The device vendor identifier
         */

        INT m_iVendorID;


        /** \brief The device product identifier
         */

        INT m_iProductID;


        /** \brief The device serial number
         */

        std::string m_strSerialNumber;


        /** \brief The status of the driver
         */

        BOOL m_bReady;

        /**
         * \brief Interface used to get the device descriptor
         * \param none
         * \return TRUE on success, FALSE otherwise
         */

        BOOL m_GetDescriptor ( VOID );


        /*
         * \brief Interface used to disable the bitbang mode
         * \param none
         * \return TRUE on success, FALSE otherwise
         */

        BOOL m_DisableBitMode ( VOID );


        /*
         * \brief Interface used to set the flow control
         * \param uiFlowCtrl
         * \return TRUE on success, FALSE otherwise
         */

        BOOL m_SetFlowControl ( UINT uiFlowCtrl );


        /*
         * \brief Interface used to set the bitbang mode
         * \param[in] ucBitmask
         * \param[in] ucMode
         * \return TRUE on success, FALSE otherwise
         */

        BOOL m_SetBitMode ( UCHAR ucBitmask, UCHAR ucMode) const;


        /*
         * \brief Interface used to read the current state of the IO pins
         * \param[out] pucPins pointer to an UCHAR variable where to read the status of the pins
         * \return TRUE on success, FALSE otherwise
         */

        BOOL m_ReadPins ( UCHAR *pucPins ) const;


        /*
         * \brief Interface used to write data to the device
         * \param[in] pucBuffer buffer with data to be written
         * \param[in] uiBufferSize size of the data in the buffer
         * \param[out] puiTransferredLength size of data reported as transferred
         * \return TRUE on success, FALSE otherwise
         */

        BOOL m_WriteData ( UCHAR *pucBuffer, UINT uiBufferSize, UINT *puiTransferredLength ) const;


        /*
         * \brief Interface used to get the device to be actually used
         * \param none
         * \return TRUE on success, FALSE otherwise
         */

        BOOL m_GetUniqueDevice ( VOID );


        /*
         * \brief Callback used to show details about the discovered devices
         * \param pvDeviceList pointer to the device list structure, actually unused
         * \param psDeviceInfo pointer to the device info structure
         * \param pvContext pointer to the context structure, actually unused
         * \return TRUE on success, FALSE otherwise
         */

        static BOOL m_ShowDevicesCB ( VOID *pvDeviceList, DeviceInfo_s *psDeviceInfo, VOID *pvContext );


        /*
         * \brief Find the device with the given serial number
         * \param none
         * \return TRUE if the device was found, FALSE otherwise
         */

        BOOL m_FindDevice( VOID );


        /*
         * \brief Force unloading the library
         * \param ModuleName the name of the module to be unloaded
         * \return none
         */

        VOID m_ForceUnloadLibrary ( const char * pstrModuleName ) const;

};

#endif //FTDI245_WINDOWS_HPP