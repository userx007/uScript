/*
http://dangerousprototypes.com/docs/UART_(binary)
*/

#include "buspirate_plugin.hpp"
#include "buspirate_generic.hpp"
#include "bithandling.h"

#include "uLogger.hpp"

///////////////////////////////////////////////////////////////////
//                 LOG DEFINES                                   //
///////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR     "BP_UART    :"
#define LOG_HDR    LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                          DEFINES                              //
///////////////////////////////////////////////////////////////////

#define PROTOCOL_NAME    "UART"

///////////////////////////////////////////////////////////////////
//            PUBLIC INTERFACES IMPLEMENTATION                   //
///////////////////////////////////////////////////////////////////

/* ============================================================================================
 List the subcommands of the protocol
============================================================================================ */

bool BuspiratePlugin::m_handle_uart_help(const std::string &args) const
{
   return generic_module_list_commands<BuspiratePlugin>(this, PROTOCOL_NAME);
}

/* ============================================================================================
 00000111 – Manual baud rate configuration, send 2 bytes

Configures the UART using custom baud rate generator settings.
This command is followed by two data bytes that represent the BRG register value.
Send the high 8 bits first, then the low 8 bits.

Use the UART manual [PDF] or an online calculator to find the correct value
(key values: fosc 32mHz, clock divider = 2, BRGH=1) .
Bus Pirate responds 0x01 to each byte. Settings take effect immediately.
============================================================================================ */

bool BuspiratePlugin::m_handle_uart_bdr(const std::string &args) const
{
    return true;

} /* m_handle_uart_bdr() */

/* ============================================================================================
 100wxxyz – Configure UART settings

    w= pin output HiZ(0)/3.3v(1)
    xx=databits and parity 8/N(0), 8/E(1), 8/O(2), 9/N(3)
    y=stop bits 1(0)/2(1)
    z=RX polarity idle 1 (0), idle 0 (1)

Startup default is 00000. Bus Pirate responds 0x01 on success.
Note: that this command code is three bits because the databits and parity setting consists of two bits.
It is not quite the same as the binary SPI mode configuration command code.
============================================================================================ */

bool BuspiratePlugin::m_handle_uart_cfg(const std::string &args) const
{
    return true;

} /* m_handle_uart_cfg() */

/* ============================================================================================
0000001x – Start (0)/stop(1) echo UART RX
In binary UART mode the UART is always active and receiving.
Incoming data is only copied to the USB side if UART RX echo is enabled.
This allows you to configure and control the UART mode settings without random data colliding
with response codes. UART mode starts with echo disabled.
This mode has no impact on data transmissions.
Responds 0x01. Clears buffer overrun bit.
============================================================================================ */

bool BuspiratePlugin::m_handle_uart_echo(const std::string &args) const
{
    bool bRetVal = true;
    uint8_t request = 0;

    if      ("start"== args) { request = 0x02; }
    else if ("stop" == args) { request = 0x03; }
    else if ("help" == args) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: start stop"));
    } else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid subcommand:"); LOG_STRING(args));
        bRetVal = false;
    }

    if (true == bRetVal) {
        bRetVal = generic_uart_send_receive(numeric::byte2span(request), numeric::byte2span(m_positive_response));
    }

    return bRetVal;

} /* m_handle_uart_echo() */

/* ============================================================================================
00001111 - UART bridge mode (reset to exit)

Starts a transparent UART bridge using the current configuration.
Unplug the Bus Pirate to exit.
============================================================================================ */

bool BuspiratePlugin::m_handle_uart_mode(const std::string &args) const
{
    bool bRetVal = true;
    uint8_t request = 0;

    if      ("bridge" == args) { request = 0x0F; }
    else if ("help"   == args) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: bridge (unplug to exit)"));
    } else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid subcommand:"); LOG_STRING(args));
        bRetVal = false;
    }

    if (true == bRetVal) {
        bRetVal = generic_uart_send_receive(numeric::byte2span(request), numeric::byte2span(m_positive_response));
    }

    return bRetVal;

} /* m_handle_uart_mode() */

/* ============================================================================================
 0100wxyz – Configure peripherals w=power, x=pullups, y=AUX, z=CS

Enable (1) and disable (0) Bus Pirate peripherals and pins.
w enables the power supplies,
x toggles the on-board pull-up resistors,
y sets the state of the auxiliary pin,
z sets the chip select pin.

Features not present in a specific hardware version are ignored. Bus Pirate responds 0×01 on success.
Note: CS pin always follows the current HiZ pin configuration. AUX is always a normal pin output (0=GND, 1=3.3volts).
============================================================================================ */

bool BuspiratePlugin::m_handle_uart_per(const std::string &args) const
{
    return generic_set_peripheral (args);

} /* m_handle_uart_per() */

/* ============================================================================================
 0110xxxx - Set UART speed

Set the UART at a preconfigured speed value:
0000=300,  0001=1200,  0010=2400,         0011=4800,
0100=9600, 0101=19200, 0110=31250 (MIDI), 0111=38400,
1000=57600,1010=115200

Start default is 300 baud. Bus Pirate responds 0×01 on success.
A read command is planned but not implemented in this version.
============================================================================================ */

bool BuspiratePlugin::m_handle_uart_speed(const std::string &args) const
{
    return generic_module_set_speed<BuspiratePlugin>( this, PROTOCOL_NAME, args);

} /* m_handle_uart_speed() */

/* ============================================================================================
 0001xxxx – Bulk UART write, send 1-16 bytes (0=1byte!)

Bulk write transfers a packet of xxxx+1 bytes to the UART.
Up to 16 data bytes can be sent at once.
Note that 0000 indicates 1 byte because there’s no reason to send 0. BP replies 0×01 to each byte.
============================================================================================ */

bool BuspiratePlugin::m_handle_uart_write(const std::string &args) const
{
    return true;

} /* m_handle_uart_write() */
