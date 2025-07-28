#ifndef FTDI245_LINUX_HPP
#define FTDI245_LINUX_HPP


#include "shlibifentry_linux.hpp"

#include <string>
#include <stdbool.h>


////////////////////////////////////////////////////////////////////////////////////////////
//  Class implementing the control the FTDI245 chip under Windows
////////////////////////////////////////////////////////////////////////////////////////////

class ftdi245hdl
{

    public:

        /*
         * \brief The class constructor
         */

        ftdi245hdl(const std::string& strSerialNumber, const int iVendorID, const int iProdID, const int iMaxNrRelays);


        /*
         * \brief The class constructor
         */

        ~ftdi245hdl( );


        /*
         * \brief Interface to set the state of a relay
         * \param uiRelay the index of the relay to be switched
         * \param uiState the state in which the relay has to be switched
         * \return TRUE on success, FALSE otherwise
         */

        bool SetRelayState( const unsigned int relay, const unsigned int state ) const ;


        /*
         * \brief Interface to set the state of all relays at once
         * \param uiState the state in which the relay has to be switched
         * \return TRUE on success, FALSE otherwise
         */

        bool SetAllState  ( const unsigned int state ) const ;


        /*
         * \brief Get the supported number of relays
         * \param none
         * \return The supported number of relays
         */

        unsigned int GetMaxRelays(void) const { return m_iMaxNrRelays; }

        /*
         * \brief Get the state of the relays
         * \param none
         * \return TRUE on success, FALSE otherwise
         */

        bool GetRelaysStates( void ) const;


    private:

        /** \brief Handle of the API library
          */

        LibFtdiApi *m_pUsbLibApi;


        /** \brief Handle of the driver context
         */

        ftdi_context *m_pFtdiCtx;


        /** \brief The maximum number of supported relays
         */

        unsigned int m_iMaxNrRelays;


        /** \brief The device vendor identifier
         */

        int m_iVendorID;


        /** \brief The device product identifier
         */

        int m_iProductID;


        /** \brief The device serial number
         */

        std::string m_strSerialNumber;


        /** \brief The status of the driver
         */

        bool m_bReady;

};

#endif //FTDI245_LINUX_HPP