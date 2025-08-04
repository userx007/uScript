/*
http://dangerousprototypes.com/docs/Raw-wire_(binary)
*/

#include "buspirate_plugin.hpp"
#include "buspirate_generic.hpp"
#include "bithandling.h"

#include "uString.hpp"
#include "uHexlify.hpp"
#include "uNumeric.hpp"
#include "uLogger.hpp"

#include <iostream>

///////////////////////////////////////////////////////////////////
//                 DLT DEFINES                                   //
///////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR     "BP_RAWWIRE :"
#define LOG_HDR    LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//            PUBLIC INTERFACES IMPLEMENTATION                   //
///////////////////////////////////////////////////////////////////


/* ============================================================================================
 0000010x- CS low (0) / high (1)

Toggle the Bus Pirate chip select pin, follows HiZ configuration setting.
CS high is pin output at 3.3volts, or HiZ.
CS low is pin output at ground. Bus Pirate responds 0×01.
============================================================================================ */
bool BuspiratePlugin::m_handle_rawwire_cs(const std::string &args) const
{
    bool bRetVal = true;
    uint8_t request = 0;

    if      ("low" ==  args) { request = 0x04; } //000000100
    else if ("high" ==  args) { request = 0x05; } //000000101
    else if ("help" ==  args) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: low high"));
    } else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid value:"); LOG_STRING(args));
        bRetVal = false;
    }

    if (true == bRetVal ) {
        uint8_t answer = 0x01;
        bRetVal = generic_uart_send_receive(numeric::byte2span(request), numeric::byte2span(answer));
    }

    return bRetVal;

} /* m_handle_rawwire_cs() */


/* ============================================================================================
0000001x - I2C-style start (0) / stop (1) bit
Send an I2C start or stop bit. Responds 0×01. Useful for I2C-like 2-wire protocols,
or building a custom implementation of I2C using the raw-wire library.

 0011xxxx - Bulk bits, send 1-8 bits of the next byte (0=1bit!) (added in v4.5)
Bulk bits sends xxxx+1 bits of the next byte to the bus.
Up to 8 data bytes can be sent at once.
Note that 0000 indicates 1 byte because there’s no reason to send 0.
BP replies 0×01 to each byte.
This is a PIC programming extension that only supports 2wire mode.
All writes are most significant bit first, regardless of the mode set with the configuration command.
============================================================================================ */
bool BuspiratePlugin::m_handle_rawwire_bit(const std::string &args) const
{
    bool bRetVal = true;
    bool bBulkBits = false;
    uint8_t answer = 0x01;
    uint8_t cBit = 0;

    if      ("start" == args) { cBit = 0x02; } //00000010
    else if ("stop"  == args) { cBit = 0x03; } //00000011
    else if ("help"  == args) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("start - send I2C start bit"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("stop  - send I2C stop bit"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("0kXY  - send k=[0..7] => 1..8 bits from byte XY"));
    } else {
        std::vector<uint8_t> data;
        if( true == (bRetVal = hexutils::stringUnhexlify(args, data)) ){
            if( 2 == data.size() ) {
                if( data[0] <= 7 ) {
                    uint8_t request[2];
                    request[0] = 0x30 + data[0];
                    request[1] = data[1];
                    bRetVal = generic_uart_send_receive(std::span<uint8_t>(request, sizeof(request)), numeric::byte2span(answer));
                    bBulkBits = true;
                } else {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Too many bits (>7)"));
                    bRetVal = false;
                }
            } else {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Too many bytes (>2)"));
                bRetVal = false;
            }
        } else {
            bRetVal = false;
        }
    }

    if ( (true == bRetVal) && (false == bBulkBits) ) {
        bRetVal = generic_uart_send_receive(numeric::byte2span(cBit), numeric::byte2span(answer));
    }

    return bRetVal;

} /* m_handle_rawwire_bit() */


/* ============================================================================================
 00000110 - Read byte
Reads a byte from the bus, returns the byte. Writes 0xff to bus in 3-wire mode.

00000111 - Read bit
Read a single bit from the bus, returns the bit value.
============================================================================================ */
bool BuspiratePlugin::m_handle_rawwire_read(const std::string &args) const
{
    bool bRetVal = true;
    uint8_t request = 0;

    if      ("bit" == args) { request = 0x07; } //00000111
    else if ("byte" == args) { request = 0x06; } //00000110
    else if ("dpin" == args) { request = 0x08; } //00001000
    else if ("help" == args) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("  bit -  read single bit from bus"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("  byte - read byte from bus"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("  dpin - read state of data input pin (no clock sent)"));
    } else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid value:"); LOG_STRING(args));
        bRetVal = false;
    }

    if (true == bRetVal ) {
        bRetVal = generic_uart_send_receive(numeric::byte2span(request));
    }

    return bRetVal;

} /* m_handle_rawwire_read() */


/* ============================================================================================

============================================================================================ */
bool BuspiratePlugin::m_handle_rawwire_write(const std::string &args) const
{
    return generic_write_data(this, args, &BuspiratePlugin::generic_wire_write_data);

} /* m_handle_rawwire_write() */


/* ============================================================================================
 00001001 - Clock tick
Sends one clock tick (low->high->low). Responds 0x01.

0000101x - Clock low (0) / high (1)
Set clock signal low or high. Responds 0x01.
============================================================================================ */
bool BuspiratePlugin::m_handle_rawwire_clock(const std::string &args) const
{
    bool bRetVal = true;
    bool bTicks  = false;
    uint8_t cClock  = 0;

    if      ("tick" == args) { cClock = 0x09; } //00001001
    else if ("lo"   == args) { cClock = 0x0A; } //00001010
    else if ("hi"   == args) { cClock = 0x0B; } //00001011
    else if ("help" == args) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("  tick - sends one clock tick (low->high->low)"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("  lo -   set clock low "));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("  hi -   set clock high"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("  k  -   [k in 1..16] bulk clock ticks)"));
    } else { // generate a number of ticks
        uint8_t u8ticks = 0;
        if (true == (bRetVal = numeric::str2uint8(args, u8ticks))) {
            if ( u8ticks < 16 ) {
                uint8_t request = 0x30 + u8ticks;
                bRetVal = generic_uart_send_receive(numeric::byte2span(request));
                bTicks = true;
            } else {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(": too many ticks (>15)"));
                bRetVal = false;
            }
        }
    }
    // or generate one tick / set clock line high or low
    if ( (true == bRetVal) && (false == bTicks) ) {
        uint8_t answer = 0x01;
        bRetVal = generic_uart_send_receive(numeric::byte2span(cClock), numeric::byte2span(answer));

    }

    return bRetVal;

} /* m_handle_rawwire_clock() */


/* ============================================================================================
 0000110x - Data low (0) / high (1)
Set data signal low or high. Responds 0x01.
============================================================================================ */
bool BuspiratePlugin::m_handle_rawwire_data(const std::string &args) const
{
    bool bRetVal = true;
    uint8_t request = 0;

    if      ("low"  == args) { request = 0x0C; } //000001100
    else if ("high" == args) { request = 0x0D; } //000001101
    else if ("help" == args) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: low high"));
    } else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid value:"); LOG_STRING(args));
        bRetVal = false;
    }

    if (true == bRetVal ) {
        uint8_t answer = 0x01;
        bRetVal = generic_uart_send_receive(numeric::byte2span(request), numeric::byte2span(answer));
    }

    return bRetVal;

} /* m_handle_rawwire_data() */


/* ============================================================================================
 0100wxyz – Configure peripherals w=power, x=pullups, y=AUX, z=CS

Enable (1) and disable (0) Bus Pirate peripherals and pins.
w - enables the power supplies,
x - toggles the on-board pull-up resistors,
y - sets the state of the auxiliary pin, and
z - sets the chip select pin.
Features not present in a specific hardware version are ignored. Bus Pirate responds 0×01 on success.
Note: CS pin always follows the current HiZ pin configuration.
AUX is always a normal pin output (0=GND, 1=3.3volts).
============================================================================================ */
bool BuspiratePlugin::m_handle_rawwire_per(const std::string &args) const
{
    return generic_set_peripheral( args );

} /* m_handle_rawwire_per() */


/* ============================================================================================
 011000xx – Set speed, 3=~400kHz, 2=~100kHz, 1=~50kHz, 0=~5kHz

The last bit of the speed command determines the bus speed.
Startup default is high-speed. Bus Pirate responds 0x01.
============================================================================================ */
bool BuspiratePlugin::m_handle_rawwire_speed(const std::string &args) const
{
    return generic_module_set_speed<BuspiratePlugin>( this, "RAWWIRE", args );

} /* m_handle_rawwire_speed() */


/* ============================================================================================
 1000wxyz – Config, w=HiZ/3.3v, x=2/3wire, y=msb/lsb, z=not used

Configure the raw-wire mode settings:.
w = pin output type HiZ(0)/3.3v(1).
x = protocol wires (0=2, 1=3), toggles between a shared input/output pin (raw2wire),
    and a separate input pin (raw3wire).
y= bit order (0=MSB, 1=LSB).
The Bus Pirate responds 0×01 on success.
Default raw startup condition is 000z. HiZ mode configuration applies to the data pins
and the CS pin, but not the AUX pin.
============================================================================================ */
bool BuspiratePlugin::m_handle_rawwire_cfg(const std::string &args) const
{
   bool bRetVal = true;
   uint8_t request = 0x80U;

    if ("help" == args) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Z/V - pin output: Z(HiZ/0) V(3.3V/1) "));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("2/3 - protocol wires: 2/0 3/1"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("M/L - bit order: MSB/0 LSB/1"));
    } else if ("?" == args) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("rawwire::cfg:"); LOG_UINT8(request));
    } else {
        // pin output
        if (ustring::containsChar(args, 'Z') ) { BIT_CLEAR(request, 3); }
        if (ustring::containsChar(args, 'V') ) { BIT_SET(request,   3); }
        // protocol wires
        if (ustring::containsChar(args, '2') ) { BIT_CLEAR(request, 2); }
        if (ustring::containsChar(args, '3') ) { BIT_SET(request,   2); }
        // bit order
        if (ustring::containsChar(args, 'M') ) { BIT_CLEAR(request, 1); }
        if (ustring::containsChar(args, 'L') ) { BIT_SET(request,   1); }

        uint8_t answer = 0x01;
        bRetVal = generic_uart_send_receive(numeric::byte2span(request), numeric::byte2span(answer));
    }

    return bRetVal;

} /* m_handle_rawwire_cfg() */


/* ============================================================================================
10100100 - PIC write. Send command + 2 bytes of data, read 1 byte (v5.1)
An extension for programming PIC microcontrollers. Writes 20bits to the 2wire interface.
Payload is three bytes.
The first byte is XXYYYYYY, where XX are the delay in MS to hold the PGC pin high on the last command bit,
this is a delay required at the end of a page write.
YYYYYY is a 4 or 6 bit ICSP programming command to send to the PIC (enter 4 bit commands as 00YYYY,
commands clocked in LSB first).
The second and third bytes are 16bit instructions to execute on the PIC.

10100101 - PIC read. Send command, read 1 byte of data (v5.1)
An extension for programming PIC microcontrollers. Writes 12bits, reads 8bits.
Payload is one byte 00YYYYYY, where YYYYYY is a 4 or 6 bit ICSP programming command to send to the PIC.
Enter 4 bit commands as 00YYYY, all commands are clocked in LSB first.
The Bus Pirate send the 4/6bit command, then 8 '0' bits, then reads one byte. The read byte is returned.
============================================================================================ */
bool BuspiratePlugin::m_handle_rawwire_pic(const std::string &args) const
{
    bool bRetVal = true;
    uint8_t u8pic = 0;
    std::vector<std::string> vectParams;
    ustring::tokenize(args, CHAR_SEPARATOR_COLON, vectParams);

    if ("help" == args) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("  read - TODO"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("  write - TODO"));
    } else {
        if (2 == vectParams.size()) {
            if      ("read" == vectParams[0]) { u8pic = 0xA4; } // 10100100
            else if ("write" == vectParams[0]) { u8pic = 0xA5; } // 10100101
            else    {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("pic unsupported operation"));
                bRetVal = false;
            }
        } else {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("pic read/write: wrong format"));
            bRetVal = false;
        }

        if (true == bRetVal) {
            std::vector<uint8_t> data;
            std::vector<uint8_t> cmd { u8pic };

            if (true == hexutils::stringUnhexlify(vectParams[1], data)) {
                if ( ((0xA4 == u8pic) && (1 == data.size())) ||   // read, payload 1 byte
                     ((0xA5 == u8pic) && (3 == data.size())) ) {  // write, payload 3 bytes
                        data.insert( data.begin(), cmd.begin(), cmd.end() );
                        bRetVal = generic_uart_send_receive( std::span<uint8_t>(data), g_positive_answer );
                } else {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("pic read/write: invalid parameters"));
                    bRetVal = false;
                }
            }
        }
    }

    return bRetVal;

} /* m_handle_rawwire_pic() */

