/*
http://dangerousprototypes.com/docs/I2C_(binary)
*/

#include "buspirate_plugin.hpp"
#include "buspirate_generic.hpp"
#include "bithandling.h"

#include "uNumeric.hpp"
#include "uLogger.hpp"

///////////////////////////////////////////////////////////////////
//                        LOG DEFINES                            //
///////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR     "BP_I2C     :"
#define LOG_HDR    LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                          DEFINES                              //
///////////////////////////////////////////////////////////////////

#define PROTOCOL_NAME           "I2C"

// return to bitbang mode
#define I2C_MODE_EXIT           0b0000'0000
#define I2C_MODE_EXIT_ANSWER    "BBIO1"

// get the mode
#define I2C_MODE_GET            0b0000'0001
#define I2C_MODE_ANSWER         "I2C1"

// fixed values
#define I2C_START               0b0000'0010
#define I2C_STOP                0b0000'0011
#define I2C_READ                0b0000'0100
#define I2C_ACK                 0b0000'0110
#define I2C_NACK                0b0000'0111
#define I2C_SNIFF_START         0b0000'1111
#define I2C_SNIFF_STOP          0b1111'1111 // Send a single byte to exit, Bus Pirate responds 0x01 on exit
#define I2C_AUX                 0b0000'1001
#define I2C_WRITE_READ          0b0000'1000

// base values
#define I2C_BULK_WR_BASE        0b0001'0000
#define I2C_CFG_PERIF_BASE      0b0100'0000
#define I2C_PULL_UP_VOLT_BASE   0b0101'0000
#define I2C_SET_SPEED_BASE      0b0110'0000

// answer
#define I2C_ANSWER              0b0000'0001

static const char *pstrInvalidSubcommand = "Invalid subcommand:";


///////////////////////////////////////////////////////////////////
//            PUBLIC INTERFACES IMPLEMENTATION                   //
///////////////////////////////////////////////////////////////////

/* ============================================================================================
 List the subcommands of the protocol
============================================================================================ */
bool BuspiratePlugin::m_handle_i2c_help(const std::string &args) const
{
   return generic_module_list_commands<BuspiratePlugin>(this, PROTOCOL_NAME);
}

/* ============================================================================================
 get mode
============================================================================================ */
bool BuspiratePlugin::m_handle_i2c_mode(const std::string &args) const
{
    uint8_t request = 0x01U;
    uint8_t answer  = 0x01U;
    return generic_uart_send_receive(numeric::byte2span(request), numeric::byte2span(answer));
}


/* ============================================================================================
Examples: i2c bit start / i2c bit stop / i2c bit ack / i2c bit nack
============================================================================================ */
bool BuspiratePlugin::m_handle_i2c_bit(const std::string &args) const
{
    bool bRetVal = true;
    uint8_t request = 0x00;
    if      ("start"== args) { request = I2C_START; }
    else if ("stop" == args) { request = I2C_STOP;  }
    else if ("ack"  == args) { request = I2C_ACK;   }
    else if ("nack" == args) { request = I2C_NACK;  }
    else if ("help" == args) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use | start | stop | ack | nack |"));
    } else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(pstrInvalidSubcommand); LOG_STRING(args));
        bRetVal = false;
    }

    if (true == bRetVal) {
        uint8_t answer = I2C_ANSWER;
        bRetVal = generic_uart_send_receive(numeric::byte2span(request), numeric::byte2span(answer));
    }

    return bRetVal;

}/* m_handle_i2c_bit()  */


/* ============================================================================================
    I2C peripheral command handler
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
bool BuspiratePlugin::m_handle_i2c_per(const std::string &args) const
{
    return generic_set_peripheral (args);

} /* m_handle_i2c_cfg() */


/* ============================================================================================
    BuspiratePlugin::m_handle_i2c_speed
============================================================================================ */
bool BuspiratePlugin::m_handle_i2c_speed(const std::string &args) const
{
    return generic_module_set_speed<BuspiratePlugin>( this, PROTOCOL_NAME, args );

} /* m_handle_i2c_speed() */


/* ============================================================================================
    00001111 - Start bus sniffer
    Sniff traffic on an I2C bus.

    [/] - Start/stop bit
    \ - escape character precedes a data byte value
    +/- - ACK/NACK

Sniffed traffic is encoded according to the table above.
Data bytes are escaped with the '\' character.
Send a single byte to exit, Bus Pirate responds 0x01 on exit.
============================================================================================ */
bool BuspiratePlugin::m_handle_i2c_sniff(const std::string &args) const
{
    bool bRetVal = true;

    if ("help"== args) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use | on | off"));
    } else {
        uint8_t request = 0;
        bool bStop = false;

        if      ("on"  == args) { request = I2C_SNIFF_START;              }
        else if ("off" == args) { request = I2C_SNIFF_STOP; bStop = true; }
        else {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(pstrInvalidSubcommand); LOG_STRING(args));
            bRetVal = false;
        }

        if (true == bRetVal ) {
            uint8_t answer = I2C_ANSWER;
            bRetVal = (true == bStop) ? generic_uart_send_receive(numeric::byte2span(request), numeric::byte2span(answer)) : generic_uart_send_receive(numeric::byte2span(request));
        }
    }

    return bRetVal;

} /* m_handle_i2c_sniff() */


/* ============================================================================================
    BuspiratePlugin::m_handle_i2c_read
============================================================================================ */
bool BuspiratePlugin::m_handle_i2c_read(const std::string &args) const
{
    bool bRetVal = true;

    if ("help" == args) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: N (nr. of bytes to read)"));
    } else {
        size_t szReadSize = 0;
        if (true == (bRetVal = numeric::str2sizet(args, szReadSize))) {
            if (szReadSize > 0 ) {
                uint8_t request_read = I2C_READ;
                uint8_t request_ack  = I2C_ACK;
                uint8_t request_nack = I2C_NACK;
                uint8_t request_stop = I2C_STOP;
                uint8_t answer       = I2C_ANSWER;

                // send ACK after every read excepting the last one when send NACK
                for (size_t i = 0; i < szReadSize; ++i) {
                    if (false == (bRetVal = generic_uart_send_receive(numeric::byte2span(request_read)))) {
                        break;
                    }

                    if (false == (bRetVal = generic_uart_send_receive(numeric::byte2span((i == (szReadSize - 1)) ? request_nack : request_ack), numeric::byte2span(answer)))) {
                        break;
                    }
                }
                // after NACK send stop bit
                if (true == bRetVal) {
                    bRetVal = generic_uart_send_receive (numeric::byte2span(request_stop), numeric::byte2span(answer));
                }
            }
        }
    }

    return bRetVal;

} /* m_handle_i2c_read() */


/* ============================================================================================
    BuspiratePlugin::m_handle_i2c_write
============================================================================================ */
bool BuspiratePlugin::m_handle_i2c_write(const std::string &args) const
{
    return generic_write_data(this, args, &BuspiratePlugin::m_i2c_bulk_write);

} /* m_handle_i2c_write() */


/* ============================================================================================
   1. First send the write then read command (0x08)
   2. The next two bytes (High8/Low8) set the number of bytes to write (0 to 4096)
   3. The next two bytes (h/l) set the number of bytes to read (0 to 4096)
   4. If the number of bytes to read or write are out of bounds, the Bus Pirate will return 0x00 now
   5. Next, send the bytes to write. Bytes are buffered in the Bus Pirate, there is no acknowledgment that a byte is received.
   6. The Bus Pirate sends an I2C start bit, then all write bytes are sent at once. If an I2C write is not ACKed by a slave device, then the operation will abort and the Bus Pirate will return 0x00 now
   7. Read starts immediately after the write completes. Bytes are read from I2C into a buffer at max I2C speed (no waiting for UART). All read bytes are ACKed, except the last byte which is NACKed, this process is handled internally between the Bus Pirate and the I2C device
   8. At the end of the read process, the Bus Pirate sends an I2C stop
   9. The Bus Pirate now returns 0x01 to the PC, indicating success
   10 Finally, the buffered read bytes are returned to the PC

Except as described above, there is no acknowledgment that a byte is received.
============================================================================================ */
bool BuspiratePlugin::m_handle_i2c_wrrd(const std::string &args) const
{
    return generic_write_read_data(m_CMD_I2C_WRRD, args);

} /* m_handle_i2c_wrrd() */


/* ============================================================================================
    BuspiratePlugin::m_handle_i2c_wrrdf
============================================================================================ */
bool BuspiratePlugin::m_handle_i2c_wrrdf(const std::string &args) const
{
    return generic_write_read_file( m_CMD_I2C_WRRD, args );

} /* m_handle_i2c_wrrdf() */


/* ============================================================================================
    BuspiratePlugin::m_handle_i2c_aux
============================================================================================ */
bool BuspiratePlugin::m_handle_i2c_aux(const std::string &args) const
{
    bool bRetVal = true;

    if ("help"== args) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("acl - AUX/CS low" ));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("ach - AUX/CS high"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("acz - AUX/CS HiZ" ));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("ra  - read AUX"   ));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("ua  - use AUX"    ));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("uc  - use CS"     ));
    } else {
        uint8_t cAux = 0x00;
        if      ("acl" == args) { cAux = 0x00;  }
        else if ("ach" == args) { cAux = 0x01;  }
        else if ("acz" == args) { cAux = 0x02;  }
        else if ("ra"  == args) { cAux = 0x03;  }
        else if ("ua"  == args) { cAux = 0x10;  }
        else if ("uc"  == args) { cAux = 0x20;  }
        else {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(pstrInvalidSubcommand); LOG_STRING(args));
            bRetVal = false;
        }
        if (true == bRetVal ) {
            uint8_t request[] = { 0x09, cAux };
            bRetVal = generic_uart_send_receive(std::span<uint8_t>(request, sizeof(request)));
        }
    }

    return bRetVal;

} /* m_handle_i2c_aux() */


/* ============================================================================================
    BuspiratePlugin::m_i2c_bulk_write
============================================================================================ */
bool BuspiratePlugin::m_i2c_bulk_write (const uint8_t *pu8Data, const size_t szLen) const
{
    static constexpr size_t szBufflen = 17;

    if (szLen + 1 >= szBufflen) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Length too big (max 16):"); LOG_SIZET(szLen));
        return false;
    }

    uint8_t vcBuf[szBufflen] = { 0 };

    vcBuf[0]= 0x10 | (uint8_t)(szLen - 1);
    memcpy(&vcBuf[1], pu8Data, szLen);

    return generic_uart_send_receive(std::span<uint8_t>{vcBuf, (szLen + 1)}, numeric::byte2span(m_positive_response));

} /* m_i2c_bulk_write() */
