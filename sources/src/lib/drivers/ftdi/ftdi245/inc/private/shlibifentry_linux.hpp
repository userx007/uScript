#ifndef SHLIBIFENTRY_LINUX_HPP
#define SHLIBIFENTRY_LINUX_HPP

#include "shlibifdecl_linux.hpp"
#include "uSharedLibLoader.hpp"

////////////////////////////////////////////////////////////////////////////////////////////
//
//  Class used to get entry points of some functions of libftdi1.so
//
////////////////////////////////////////////////////////////////////////////////////////////

class LibFtdiApi
{
    SharedLibLoader _dll{ "libftdi1.so" };

public:
    decltype(ExtIF_FtdiNew)            *pfFtdiNew          = _dll["ftdi_new"];
    decltype(ExtIF_FtdiUsbOpen)        *pfFtdiOpen         = _dll["ftdi_usb_open"];
    decltype(ExtIF_FtdiUsbOpenString)  *pfFtdiOpenString   = _dll["ftdi_usb_open_string"];
    decltype(ExtIF_FtdiSetBitmode)     *pfFtdiSetBitmode   = _dll["ftdi_set_bitmode"];
    decltype(ExtIF_FtdiReadPins)       *pfFtdiReadPins     = _dll["ftdi_read_pins"];
    decltype(ExtIF_FtdiWriteData)      *pfFtdiWriteData    = _dll["ftdi_write_data"];
    decltype(ExtIF_FtdiUsbClose)       *pfFtdiClose        = _dll["ftdi_usb_close"];
    decltype(ExtIF_FtdiFree)           *pfFtdiFree         = _dll["ftdi_free"];
    decltype(ExtIF_FtdiGetErrorString) *pfFtdiGetErrString = _dll["ftdi_get_error_string"];

};

#endif //SHLIBIFENTRY_LINUX_HPP