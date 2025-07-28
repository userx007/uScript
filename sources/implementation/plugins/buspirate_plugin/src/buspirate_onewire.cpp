/*
http://dangerousprototypes.com/docs/1-Wire_(binary)
*/

#include "buspirate_plugin.hpp"
#include "buspirate_generic.hpp"
#include "string_handling.hpp"
#include "bithandling.h"

#include "uString.hpp"

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
#define LT_HDR     "BP_ONEWIRE :"
#define LOG_HDR    LOG_STRING(LT_HDR)


///////////////////////////////////////////////////////////////////
//            PUBLIC INTERFACES IMPLEMENTATION                   //
///////////////////////////////////////////////////////////////////

/* ============================================================================================
00000010 – 1-Wire reset
Send a 1-Wire reset. Responds 0×01.
Use a dummy char /string for the second parameter (will be ignored)
============================================================================================ */
bool BuspiratePlugin::m_handle_onewire_reset(const std::string &args) const
{
    char request = 0x02;
    char answer  = 0x01;
    return generic_uart_send_receive(&request, sizeof(request), &answer, sizeof(answer));

} /* m_handle_onewire_reset() */

/* ============================================================================================
00001000 - ROM search macro (0xf0)
00001001 - ALARM search macro (0xec)

Search macros are special 1-Wire procedures that determine device addresses.
The command returns 0x01, and then each 8-byte 1-Wire address located.
Data ends with 8 bytes of 0xff.
============================================================================================ */
bool BuspiratePlugin::m_handle_onewire_search(const std::string &args) const
{
    bool bRetVal = true;
    unsigned char request = 0U;

    if      ("rom"  == args) { request = (unsigned char)0xF0U; }
    else if ("alarm" == args) { request = (unsigned char)0xECU; }
    else if ("help" == args) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: rom alarm"));
    } else {
        const unsigned char answer = 0x01U;
        bRetVal = generic_uart_send_receive(reinterpret_cast<const char*>(&request), sizeof(request), reinterpret_cast<const char*>(&answer), sizeof(answer));
    }

    return bRetVal;

} /* m_handle_onewire_search() */

/* ============================================================================================
00000100 – Read byte(s)
Reads a byte from the bus, returns the byte.
============================================================================================ */
bool BuspiratePlugin::m_handle_onewire_read(const std::string &args) const
{
    bool bRetVal = true;

    if ("help" == args) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: 1 .. N"));
    } else {
        uint8_t u8reads = atoi(args);
        for(int i = 0; i < u8reads; ++i) {
            char request = 0x40;
            if( false == (bRetVal = generic_uart_send_receive(&request, sizeof(request))) ) {
                break;
            }
        }
    }

    return bRetVal;

} /* m_handle_onewire_read() */

/* ============================================================================================
 0001xxxx – Bulk 1-Wire write, send 1-16 bytes (0=1byte!)

Bulk write transfers a packet of xxxx+1 bytes to the 1-Wire bus.
Up to 16 data bytes can be sent at once. Note that 0000 indicates 1 byte because there’s no
reason to send 0. BP replies 0×01 to each byte.
============================================================================================ */
bool BuspiratePlugin::m_handle_onewire_write(const std::string &args) const
{
    return generic_write_data(this, args, &BuspiratePlugin::generic_wire_write_data);

} /* m_handle_onewire_write() */

/* ============================================================================================
 0100wxyz – Configure peripherals w=power, x=pullups, y=AUX, z=CS

Enable (1) and disable (0) Bus Pirate peripherals and pins.
- bit w enables the power supplies,
- bit x toggles the on-board pull-up resistors,
- y sets the state of the auxiliary pin,
- z sets the chip select pin.
Features not present in a specific hardware version are ignored.
Bus Pirate responds 0×01 on success.

Note:
CS pin always follows the current HiZ pin configuration.
AUX is always a normal pin output (0=GND, 1=3.3volts).
============================================================================================ */
bool BuspiratePlugin::m_handle_onewire_cfg(const std::string &args) const
{
    bool bRetVal = true;
    char request = 0x40;

    if ("help" == args) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("w/W - disable/enable power "));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("p/P - toggle pull-up resistors"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("a/A - toggle AUX pin"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("c/C - toggle CS pin"));
    } else if ("?" == args) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("onewire::cfg:"); LOG_UINT8(request));
    } else {
        // pin output
        if (ustring::containsChar(args, 'w') ) { BIT_CLEAR(request, 3); }
        if (ustring::containsChar(args, 'W') ) { BIT_SET(request,   3); }
        // clock idle phase
        if (ustring::containsChar(args, 'p') ) { BIT_CLEAR(request, 2); }
        if (ustring::containsChar(args, 'P') ) { BIT_SET(request,   2); }
        // clock edge
        if (ustring::containsChar(args, 'a') ) { BIT_CLEAR(request, 1); }
        if (ustring::containsChar(args, 'A') ) { BIT_SET(request,   1); }
        // sample time
        if (ustring::containsChar(args, 'c') ) { BIT_CLEAR(request, 0); }
        if (ustring::containsChar(args, 'C') ) { BIT_SET(request,   0); }

        char answer = 0x01;
        bRetVal = generic_uart_send_receive(&request, sizeof(request), &answer, sizeof(answer));
    }

    return bRetVal;

} /* m_handle_onewire_cfg() */
