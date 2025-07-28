
#ifndef SHLIBIFDECL_LINUX_HPP
#define SHLIBIFDECL_LINUX_HPP

#define BITMODE_SYNCBB  0x04  /**< synchronous bitbang mode, available on 2232x and R-type chips  */

/**
    \brief Declaration of the main context structure for all libftdi functions.
*/
struct ftdi_context;

#ifdef __cplusplus
extern "C"
{
#endif

/**
    \brief Prototypes of the interfaces to be accessed in libftdi1.so
*/

struct ftdi_context *ExtIF_FtdiNew            ( void);
const char          *ExtIF_FtdiGetErrorString ( struct ftdi_context *ftdi);
int                  ExtIF_FtdiUsbOpen        ( struct ftdi_context *ftdi, int vendor, int product);
int                  ExtIF_FtdiUsbOpenString  ( struct ftdi_context *ftdi, const char* description);
int                  ExtIF_FtdiSetBitmode     ( struct ftdi_context *ftdi, unsigned char bitmask, unsigned char mode);
int                  ExtIF_FtdiReadPins       ( struct ftdi_context *ftdi, unsigned char *pins);
int                  ExtIF_FtdiWriteData      ( struct ftdi_context *ftdi, const unsigned char *buf, int size);
int                  ExtIF_FtdiUsbClose       ( struct ftdi_context *ftdi);
void                 ExtIF_FtdiFree           ( struct ftdi_context *ftdi);

#ifdef __cplusplus
}
#endif

#endif // SHLIBIFDECL_LINUX_HPP
