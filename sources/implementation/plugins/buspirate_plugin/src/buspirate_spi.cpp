/*
http://dangerousprototypes.com/docs/SPI_(binary)
*/

#include "buspirate_plugin.hpp"
#include "buspirate_generic.hpp"
#include "string_handling.hpp"
#include "bithandling.h"

#include "uString.hpp"

///////////////////////////////////////////////////////////////////
//                 DLT DEFINES                                   //
///////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR     "BP_SPI     :"
#define LOG_HDR    LOG_STRING(LT_HDR)


///////////////////////////////////////////////////////////////////
//            PUBLIC INTERFACES IMPLEMENTATION                   //
///////////////////////////////////////////////////////////////////

/* ============================================================================================
SPI cs command handler
--------------------------
 0000001x - CS high (1) or low (0)
Toggle the Bus Pirate chip select pin, follows HiZ configuration setting.
CS high is pin output at 3.3volts, or HiZ.
CS low is pin output at ground. Bus Pirate responds 0x01.
============================================================================================ */

bool BuspiratePlugin::m_handle_spi_cs(const std::string &args) const
{
    bool bRetVal = true;

    if      ("en"  == args) { m_spi_cs_enable(m_CS_ENABLE);   } //00000010
    else if ("dis" == args) { m_spi_cs_enable(m_CS_DISABLE);  } //00000011
    else if ("help" == args) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: en[GND] dis[3.3V/HiZ]"));
    } else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid subcommand:"); LOG_STRING(args));
        bRetVal = false;
    }

    return bRetVal;

} /* m_handle_spi_cs() */


/* ============================================================================================
SPI sniff command handler
-----------------------------
000011XX - Sniff SPI traffic when CS low(10)/all(01)
The SPI sniffer is implemented in hardware and should work up to 10MHz.
It follows the configuration settings you entered for SPI mode.
The sniffer can read all traffic, or filter by the state of the CS pin.

Send the SPI sniffer command to start the sniffer, the Bus Pirate responds 0x01 then sniffed data starts to flow.
Send any byte to exit. Bus Pirate responds 0x01 on exit. (0x01 reply location was changed in v5.8)

If the sniffer can't keep with the SPI data, the MODE LED turns off and the sniff is aborted. (new in v5.1)
The sniffer follows the output clock edge and output polarity settings of the SPI mode,
but not the input sample phase.
============================================================================================ */

bool BuspiratePlugin::m_handle_spi_sniff(const std::string &args) const
{
    bool bRetVal = true;
    bool bStop = false;
    unsigned char request = 0xFFU;

    if      ("all"  == args) { request = (unsigned char)0x0DU; } //00001101
    else if ("cslo" == args) { request = (unsigned char)0x0EU; } //00001110
    else if ("off"  == args) { bStop   = true; } //any byte to exit
    else if ("help" == args) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: all cslo off"));
    } else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid subcommand:"); LOG_STRING(args));
        bRetVal = false;
    }

    if (true == bRetVal ) {
        const unsigned char answer = 0x01U;
        // positive answer expected only immediatelly after start
        bRetVal = (false == bStop) ? generic_uart_send_receive(reinterpret_cast<const char*>(&request), sizeof(request), reinterpret_cast<const char*>(&answer), sizeof(answer)) : generic_uart_send_receive(reinterpret_cast<const char*>(&request), sizeof(request) );
    }

    return bRetVal;

} /* m_handle_spi_sniff() */


/* ============================================================================================
SPI speed command handler
-----------------------------
000=30kHz, 001=125kHz, 010=250kHz, 011=1MHz, 100=2MHz, 101=2.6MHz, 110=4MHz, 111=8MHz
This command sets the SPI bus speed according to the values shown. Default startup speed is 000 (30kHz).
============================================================================================ */

bool BuspiratePlugin::m_handle_spi_speed(const std::string &args) const
{
    return generic_module_set_speed<BuspiratePlugin>( this, "SPI", args );

} /* m_handle_spi_speed() */


/* ============================================================================================
SPI configuration command handler
----------------------------------
1000wxyz - SPI config, w=HiZ/3.3v, x=CKP idle, y=CKE edge, z=SMP sample

This command configures the SPI settings.
Options and start-up defaults are the same as the user terminal SPI mode.
w= pin output HiZ(0)/3.3v(1),
x=CKP clock idle phase (low=0),
y=CKE clock edge (active to idle=1),
z=SMP sample time (middle=0). The Bus Pirate responds 0x01 on success.

Default raw SPI startup condition is 0010. HiZ mode configuration applies to the SPI pins and the CS pin,
but not the AUX pin.
See the PIC24FJ64GA002 datasheet and the SPI section[PDF] of the PIC24 family manual
for more about the SPI configuration settings.
============================================================================================ */

bool BuspiratePlugin::m_handle_spi_cfg(const std::string &args) const
{
    bool bRetVal = true;
    static unsigned char request = 0x80U;

    if ("help" == args) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("z/V - pin output: z(HiZ/0)! V(3.3V/1)"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("l/H - CKP clock idle phase: l(low/0)! H(high/1)"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("i/A - CKE clock edge i(Idle2Active/0) A(Active2Idle/1)"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("m/E - SMP sample time m(middle/0)! E(end/1)"));
    } else if ("?" == args) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("spi::cfg:"); LOG_UINT8(request));
    } else {
        // pin output
        if (ustring::containsChar(args, 'z') ) { BIT_CLEAR(request, 3); }
        if (ustring::containsChar(args, 'V') ) { BIT_SET(request,   3); }
        // clock idle phase
        if (ustring::containsChar(args, 'l') ) { BIT_CLEAR(request, 2); }
        if (ustring::containsChar(args, 'H') ) { BIT_SET(request,   2); }
        // clock edge
        if (ustring::containsChar(args, 'i') ) { BIT_CLEAR(request, 1); }
        if (ustring::containsChar(args, 'A') ) { BIT_SET(request,   1); }
        // sample time
        if (ustring::containsChar(args, 'm') ) { BIT_CLEAR(request, 0); }
        if (ustring::containsChar(args, 'E') ) { BIT_SET(request,   0); }

        unsigned char answer = 0x01U;
        bRetVal = generic_uart_send_receive(reinterpret_cast<const char*>(&request), sizeof(request), reinterpret_cast<const char*>(&answer), sizeof(answer));
    }

    return bRetVal;

} /* m_handle_spi_cfg() */


/* ============================================================================================
    SPI peripheral command handler
-----------------------------------
    0100wxyz - Configure peripherals w=power, x=pull-ups, y=AUX, z=CS
     * Configure peripherals:
     *
     *    7     6     5     4     3     2     1     0
     * +-----+-----+-----+-----+-----+-----+-----+-----+
     * |  0  |  1  |  0  |  0  |  W  |  X  |  Y  |  Z  |
     * +-----+-----+-----+-----+-----+-----+-----+-----+
     *  \__________ __________/ \_ _/ \_ _/ \_ _/ \_ _/
     *             |              |     |     |     |
     *             |              |     |     |     +--> CS       : 1 - Enable, 0 - Disable.
     *             |              |     |     +--------> Aux      : 1 - Enable, 0 - Disable.
     *             |              |     +--------------> Pull-ups : 1 - Enable, 0 - Disable.
     *             |              +--------------------> Power    : 1 - Enable, 0 - Disable.
     *             +-----------------------------------> Command  : 4xh - Configure peripherals.
============================================================================================ */

bool BuspiratePlugin::m_handle_spi_per(const std::string &args) const
{
    return generic_set_peripheral( args );

} /* m_handle_spi_per() */


/* ============================================================================================
    SPI bulk transfer command handler (READ)
----------------------------------------------
     * 0001xxxx - Bulk SPI transfer, write 1-16 bytes (0=1byte!)
     * Bulk SPI transfer:
     *
     *    7     6     5     4     3     2     1     0
     * +-----+-----+-----+-----+-----+-----+-----+-----+--- -- -   - -- ---+
     * |  0  |  0  |  0  |  1  |                       |                   |
     * +-----+-----+-----+-----+-----+-----+-----+-----+--- -- -   - -- ---+
     *  \__________ __________/ \__________ __________/ \________ ________/
     *             |                       |                     |
     *             |                       |                     +---------> Payload      : From 1 to 16 bytes.
     *             |                       +-------------------------------> Payload Size : Number of bytes - 1.
     *             +-------------------------------------------------------> Command      : 1xh
============================================================================================ */

bool BuspiratePlugin::m_handle_spi_read(const std::string &args) const
{
    bool bRetVal = true;

    if ("help" == args) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: 1 .. 16"));
    } else {
        uint8_t u8ReadBytes = (uint8_t)atoi(args);
        if ((u8ReadBytes > 16) || (0 == u8ReadBytes) ) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Read: too much/less data:"); LOG_UINT8(u8ReadBytes); LOG_STRING("Expected 1 .. 16"));
            bRetVal = false;
        } else {
            if (true == (bRetVal = m_spi_cs_enable(m_CS_ENABLE)) ) {
                uint8_t buffer[17];
                memset(buffer, 0xFF, u8ReadBytes);
                if (true == (bRetVal = m_spi_bulk_write(buffer, u8ReadBytes)) ) {
                    bRetVal = m_spi_cs_enable(m_CS_DISABLE);
                }
            }
        }
    }

    return bRetVal;

} /* m_handle_spi_read() */


/* ============================================================================================
    SPI bulk transfer command handler (WRITE)
---------------------------------------------
     * 0001xxxx - Bulk SPI transfer, write 1-16 bytes (0=1byte!)
     * Bulk SPI transfer:
     *
     *    7     6     5     4     3     2     1     0
     * +-----+-----+-----+-----+-----+-----+-----+-----+--- -- -   - -- ---+
     * |  0  |  0  |  0  |  1  |                       |                   |
     * +-----+-----+-----+-----+-----+-----+-----+-----+--- -- -   - -- ---+
     *  \__________ __________/ \__________ __________/ \________ ________/
     *             |                       |                     |
     *             |                       |                     +---------> Payload      : From 1 to 16 bytes.
     *             |                       +-------------------------------> Payload Size : Number of bytes - 1.
     *             +-------------------------------------------------------> Command      : 1xh
============================================================================================ */

bool BuspiratePlugin::m_handle_spi_write(const std::string &args) const
{
    return generic_write_data(this, args, &BuspiratePlugin::m_spi_bulk_write);

} /* m_handle_spi_write() */


/* ============================================================================================
    SPI "write then read" command handler
-----------------------------------------
     00000100 - Write then read
     This command was developed to help speed ROM programming with Flashrom. It might be helpful for a lot of common SPI operations.
     It enables chip select, writes 0-4096 bytes, reads 0-4096 bytes, then disables chip select.
     All data for this command can be sent at once, and it will be buffered in the Bus Pirate.
     The write and read operations happen all at once, and the read data is buffered.
     At the end of the operation, the read data is returned from the buffer.
     The goal is to meet the stringent timing requirements of some ROM chips by buffering everything instead of letting the serial port delay things.
     Write then read command format
     - command (1byte) | number of write bytes (2bytes) | number of read bytes (2bytes) | bytes to write (0-4096bytes)

     Return data format success/0x01 (1byte) bytes read from SPI (0-4096bytes)

      1   First send the write then read command (00000100)
      2   The next two bytes (High8/Low8) set the number of bytes to write (0 to 4096)
      3   The next two bytes (h/l) set the number of bytes to read (0 to 4096)
      4   If the number of bytes to read or write are out of bounds, the Bus Pirate will return 0x00 now
      5   Now send the bytes to write. Bytes are buffered in the Bus Pirate, there is no acknowledgment that a byte is received.
      6   Now the Bus Pirate will write the bytes to SPI and read/return the requsted number of read bytes
      7   CS goes low, all write bytes are sent at once
      8   Read starts immediately, all bytes are put into a buffer at max SPI speed (no waiting for UART)
      9   At the end of the read, CS goes high
     10   The Bus Pirate now returns 0x01, success
     11   Finally, the buffered read bytes are returned via the serial port

     Except as described above, there is no acknowledgment that a byte is received.
============================================================================================ */

bool BuspiratePlugin::m_handle_spi_wrrd(const std::string &args) const
{
    return generic_write_read_data(m_CMD_SPI_WRRD, args);

} /* m_handle_spi_wrrd() */


/* ============================================================================================
    SPI "write then read" from file command handler
============================================================================================ */

bool BuspiratePlugin::m_handle_spi_wrrdf(const std::string &args) const
{
    return generic_write_read_file( m_CMD_SPI_WRRD, args );

} /* m_handle_spi_wrrdf */


/* ============================================================================================

============================================================================================ */
bool BuspiratePlugin::m_spi_cs_enable( const int iEnable  ) const
{
    char request = ((m_CS_ENABLE == iEnable) ? 0x02 : 0x03);
    char answer  = 0x01;
    return generic_uart_send_receive(&request, sizeof(request), &answer, sizeof(answer));

} /* m_spi_cs_enable() */


/* ============================================================================================
    BuspiratePlugin::m_spi_bulk_write
============================================================================================ */

bool BuspiratePlugin::m_spi_bulk_write(const uint8_t *pu8Data, const int iLen) const
{
    bool bRetVal = false;
    char request[17] = { 0 };
    char answer = 0x01;

    if (true == (bRetVal = m_spi_cs_enable(m_CS_ENABLE)) ) {
        unsigned int  iTmpLen = iLen;
        while (iTmpLen > 0) {
            uint8_t count = (iTmpLen < 6U) ? iTmpLen : 6U;
            request[0]= 0x10 | (count - 1);
            memcpy(&request[1], pu8Data, count);

            if( false == (bRetVal = generic_uart_send_receive(request, (count + 1), &answer, sizeof(answer))) ) {
                break;
            }

            pu8Data += count;
            iTmpLen -= count;
        }
        if(true == bRetVal){
            bRetVal = m_spi_cs_enable(m_CS_DISABLE);
        }
    }

    return bRetVal;

} /* m_spi_bulk_write() */
