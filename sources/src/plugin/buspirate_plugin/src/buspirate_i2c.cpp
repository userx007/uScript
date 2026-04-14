/*
http://dangerousprototypes.com/docs/I2C_(binary)
*/

#include "buspirate_plugin.hpp"
#include "buspirate_generic.hpp"
#include "bithandling.h"

#include "uNumeric.hpp"
#include "uHexdump.hpp"
#include "uLogger.hpp"

#include <algorithm>

///////////////////////////////////////////////////////////////////
//                        LOG DEFINES                            //
///////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif 
#define LT_HDR     "BPIRATE_I2C |"
#define LOG_HDR    LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                          OPTIONS                              //
///////////////////////////////////////////////////////////////////

#define I2C_SCAN_REPORT_TABLE 0U

///////////////////////////////////////////////////////////////////
//                          DEFINES                              //
///////////////////////////////////////////////////////////////////

#define PROTOCOL_NAME           "I2C"

// return to bitbang mode
#define I2C_MODE_EXIT           0b0000'0000 // Exit to bitbang mode, responds "BBIOx"
#define I2C_MODE_EXIT_ANSWER    "BBIO1"

// get the mode
#define I2C_MODE_GET            0b0000'0001 // 0000'0001 – Display mode version string, responds "I2Cx"
#define I2C_MODE_ANSWER         "I2C1"

// fixed values
#define I2C_START               0b0000'0010 // 0000'0010 – I2C start bit
#define I2C_STOP                0b0000'0011 // 0000'0011 – I2C stop bit
#define I2C_READ                0b0000'0100 // 0000'0100 - I2C read byte
#define I2C_ACK                 0b0000'0110 // 0000'0110 - ACK bit
#define I2C_NACK                0b0000'0111 // 0000'0111 - NACK bit
#define I2C_SNIFF_START         0b0000'1111 // 0000'1111 - Start bus sniffer
#define I2C_SNIFF_STOP          0b1111'1111 // 1111'1111 - Send a single byte to exit, Bus Pirate responds 0x01 on exit
#define I2C_AUX                 0b0000'1001 // 0000'1001 - 0x09 Extended AUX command
#define I2C_WRITE_READ          0b0000'1000 // 0000'1000 - 0x08 Write then read

// base values
#define I2C_BULK_WR_BASE        0b0001'0000 // 0001'xxxx – Bulk I2C write, send 1-16 bytes (0=1byte!)
#define I2C_CFG_PERIF_BASE      0b0100'0000 // 0100'wxyz – Configure peripherals w=power, x=pullups, y=AUX, z=CS
#define I2C_PULL_UP_VOLT_BASE   0b0101'0000 // 0101'00xy - Pull up voltage select (BPV4 only)- x=5v y=3.3v
#define I2C_SET_SPEED_BASE      0b0110'0000 // 0110'00xx - Set I2C speed, 3=~400kHz, 2=~100kHz, 1=~50kHz, 0=~5kHz (updated in v4.2 firmware)

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
    uint8_t request = I2C_MODE_GET;
    constexpr const char expected[] = I2C_MODE_ANSWER;
    uint8_t response[sizeof(expected)] = {};

    return generic_uart_send_receive(numeric::byte2span(request), numeric::byte2span(response), numeric::cstr2span(expected));
}

/* ============================================================================================
 exit
============================================================================================ */
bool BuspiratePlugin::m_handle_i2c_exit(const std::string &args) const
{
    uint8_t request = I2C_MODE_EXIT;
    constexpr const char expected[] = I2C_MODE_EXIT_ANSWER;
    uint8_t response[sizeof(expected)] = {};

    return generic_uart_send_receive(numeric::byte2span(request), numeric::byte2span(response), numeric::cstr2span(expected));
}

/* ============================================================================================
Examples: i2c bit start / i2c bit stop / i2c bit ack / i2c bit nack
============================================================================================ */
bool BuspiratePlugin::m_handle_i2c_bit(const std::string &args) const
{
    uint8_t request = 0x00;

    if      ("start"== args) { return m_i2c_send_bit(I2C_START); }
    else if ("stop" == args) { return m_i2c_send_bit(I2C_STOP ); }
    else if ("ack"  == args) { return m_i2c_send_bit(I2C_ACK  ); }
    else if ("nack" == args) { return m_i2c_send_bit(I2C_NACK ); }
    else if ("help" == args) {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: start stop ack nack"));
        return true;
    } else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(pstrInvalidSubcommand); LOG_STRING(args));
        return false;
    }

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
    return generic_module_set_speed<BuspiratePlugin>( this, PROTOCOL_NAME, args);

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
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use | on | off"));
    } else {
        uint8_t request = 0;
        bool bStop = false;

        if      ("on"  == args) { request = I2C_SNIFF_START;              }
        else if ("off" == args) { request = I2C_SNIFF_STOP; bStop = true; }
        else {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(pstrInvalidSubcommand); LOG_STRING(args));
            bRetVal = false;
        }

        if (true == bRetVal) {
            if (true == bStop) {
                uint8_t response[sizeof(m_positive_response)] = {};
                bRetVal = generic_uart_send_receive(numeric::byte2span(request), numeric::byte2span(response), numeric::byte2span(m_positive_response));
            } else {
                bRetVal = generic_uart_send_receive(numeric::byte2span(request));
            }
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
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: N (nr. of bytes to read)"));
    } else {
        size_t szReadSize = 0;
        if (true == (bRetVal = numeric::str2sizet(args, szReadSize))) {
            if (szReadSize > 0) {
                std::vector<uint8_t> response(szReadSize);
                if (true == (bRetVal = m_i2c_read(response))) {
                    hexutils::logHexdump(LOG_VERBOSE, "I2C read:", "SAoC", response);
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
   7. Read starts immediately after the write completes. Bytes are read from I2C into a buffer at max I2C speed (no waiting for UART).
      All read bytes are ACKed, except the last byte which is NACKed, this process is handled internally between the Bus Pirate and the I2C device
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
    return generic_write_read_file( m_CMD_I2C_WRRD, args);

} /* m_handle_i2c_wrrdf() */


/* ============================================================================================
    BuspiratePlugin::m_handle_i2c_aux
============================================================================================ */
bool BuspiratePlugin::m_handle_i2c_aux(const std::string &args) const
{
    bool bRetVal = true;

    if ("help"== args) {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("acl - AUX/CS low" ));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("ach - AUX/CS high"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("acz - AUX/CS HiZ" ));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("ra  - read AUX"   ));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("ua  - use AUX"    ));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("uc  - use CS"     ));
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
        if (true == bRetVal) {
            uint8_t request[] = { 0x09, cAux };
            bRetVal = generic_uart_send_receive(std::span<uint8_t>(request, sizeof(request)));
        }
    }

    return bRetVal;

} /* m_handle_i2c_aux() */


/* ============================================================================================
    BuspiratePlugin::m_i2c_bulk_write

    Binary I2C bulk-write protocol (Bus Pirate firmware):
      → 0x10|(N-1)  data[0] ... data[N-1]   (command byte + N data bytes)
      ← 0x01                                 (command accepted)
      ← ack[0] ... ack[N-1]                  (one ACK/NACK per data byte)
                                             0x00 = ACK  (slave responded)
                                             0x01 = NACK (no response)

    The firmware always sends exactly N ACK/NACK bytes after the 0x01
    confirmation, one per data byte written.  Failing to consume them leaves
    them in the UART buffer and misaligns every subsequent read (the next
    command that expects 0x01 will read a leftover ACK/NACK byte instead).
============================================================================================ */
bool BuspiratePlugin::m_i2c_bulk_write(std::span<const uint8_t> request) const
{
    static constexpr size_t szBufflen = 17;

    if (request.size() + 1 >= szBufflen) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Length too big (max 16):"); LOG_SIZET(request.size()));
        return false;
    }

    std::array<uint8_t, szBufflen> internal_request = {};  // zero-initialized

    internal_request[0] = I2C_BULK_WR_BASE | static_cast<uint8_t>(request.size() - 1);
    std::copy(request.begin(), request.end(), internal_request.begin() + 1);

    /* Step 1: send the bulk-write command + data, expect 0x01 confirmation */
    uint8_t response[sizeof(m_positive_response)] = {};
    if (!generic_uart_send_receive(std::span<uint8_t>{internal_request.data(), request.size() + 1},
                                   numeric::byte2span(response),
                                   numeric::byte2span(m_positive_response))) {
        return false;
    }

    /* Step 2: consume one ACK/NACK byte per data byte sent.
       0x01 = ACK (device present), 0x00 = NACK (no response).
       We log each result but do not treat NACK as a hard failure here —
       the caller (scan loop) decides what a NACK means for its use-case. */
    bool bRetVal = true;
    for (size_t i = 0; i < request.size(); ++i) {
        uint8_t ackByte = 0xFF;
        if (!generic_uart_send_receive(std::span<uint8_t>{}, numeric::byte2span(ackByte))) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Failed to read ACK/NACK for byte"); LOG_SIZET(i));
            bRetVal = false;
            break;
        }
        const bool bAck = (ackByte == 0x01);
        LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                  LOG_STRING("Byte"); LOG_SIZET(i);
                  LOG_STRING("->"); LOG_STRING(bAck ? "ACK" : "NACK"));
    }

    return bRetVal;
}



/* ============================================================================================
    BuspiratePlugin::m_handle_i2c_read
============================================================================================ */
bool BuspiratePlugin::m_i2c_read (std::span<uint8_t> response) const
{
    bool bRetVal = true;

    const uint8_t request_read  = I2C_READ;
    const uint8_t request_ack   = I2C_ACK;
    const uint8_t request_nack  = I2C_NACK;
    const uint8_t request_stop  = I2C_STOP;

    const size_t szReadSize = response.size();

    if (szReadSize == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("No buffer was allocated for read ..."));
        return false;
    }

    uint8_t tempByte = 0;
    std::span<uint8_t> tempSpan(&tempByte, 1);

    for (size_t i = 0; i < szReadSize; ++i) {
        tempByte = 0;

        // Read one byte
        bRetVal = generic_uart_send_receive(numeric::byte2span(request_read), tempSpan);
        if (!bRetVal) {
            break;
        }

        response[i] = tempByte;

        // Send ACK or NACK
        uint8_t ackByte = (i == szReadSize - 1) ? request_nack : request_ack;
        uint8_t ack_response[sizeof(m_positive_response)] = {};
        bRetVal = generic_uart_send_receive(numeric::byte2span(ackByte), numeric::byte2span(ack_response), numeric::byte2span(m_positive_response));
        if (!bRetVal) {
            break;
        }
    }

    // Send STOP if all went well
    if (bRetVal) {
        uint8_t stop_response[sizeof(m_positive_response)] = {};
        bRetVal = generic_uart_send_receive(numeric::byte2span(request_stop), numeric::byte2span(stop_response), numeric::byte2span(m_positive_response));
    }

    return bRetVal;

} /* m_i2c_read() */


/* ============================================================================================
    BuspiratePlugin::m_handle_i2c_read
============================================================================================ */
bool BuspiratePlugin::m_handle_i2c_script(const std::string &args) const
{
    bool bRetVal = true;

    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: scriptname"));
        return true;
    } 
    
    return generic_execute_script<BuspiratePlugin>(this, args, &BuspiratePlugin::m_i2c_write_transaction, &BuspiratePlugin::m_i2c_read);

} /* m_handle_i2c_script() */


/* ============================================================================================
    BuspiratePlugin::m_i2c_flush_rx

    Drains any stale bytes sitting in the UART RX buffer by issuing receive-only
    reads until the read times out with nothing available.

    Called before the scan loop to remove firmware-emitted trailing bytes (e.g.
    0x07 NACK indicator) left over from preceding mode/peripheral setup commands.
============================================================================================ */
void BuspiratePlugin::m_i2c_flush_rx() const
{
    uint8_t drain = 0xFF;
    while (generic_uart_send_receive(std::span<uint8_t>{}, numeric::byte2span(drain))
           && drain != 0xFF)
    {
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Flush: discarded stale byte:"); LOG_UINT8(drain));
        drain = 0xFF;
    }
    
} /* m_i2c_flush_rx() */


/* ============================================================================================
    BuspiratePlugin::m_i2c_probe_address

    Performs a minimal write-probe transaction on a single 7-bit address to determine
    whether a device is present.

    Transaction sequence:
      1. START
      2. Bulk-write 1 byte  → addr7bit << 1  (R/W̄ = 0, write direction)
         - Bus Pirate responds 0x01 (command accepted)
         - Bus Pirate then sends 1 ACK/NACK byte: 0x00 = ACK, 0x01 = NACK
      3. STOP  (always, even on mid-transaction error, to release the bus)
      4. Post-STOP drain: some firmware versions emit an extra 0x07 byte after
         the 0x01 STOP confirmation when the address was NACKed — consume it.

    Parameters:
      addr7bit  [in]  – 7-bit I2C address to probe (0x00-0x7F)
      bAcked   [out]  – set to true if the device ACKed, false otherwise

    Returns true if all UART exchanges succeeded (even when the device NACKed).
    Returns false on any UART-level communication error.
============================================================================================ */
bool BuspiratePlugin::m_i2c_probe_address(const uint8_t addr7bit, bool &bAcked) const
{
    bAcked   = false;
    bool bOk = true;

    // 1. START
    {
        const uint8_t request = I2C_START;
        uint8_t response[sizeof(m_positive_response)] = {};
        bOk = generic_uart_send_receive(numeric::byte2span(request),
                                        numeric::byte2span(response),
                                        numeric::byte2span(m_positive_response));
        if (!bOk) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Probe"); LOG_HEX8(addr7bit);
                      LOG_STRING(": START failed"));
            return false;   // bus state unknown — cannot issue STOP safely
        }
    }

    // 2. Bulk-write address byte (addr << 1, R/W=0)
    //    → 0x10  addrByte
    //    ← 0x01  (command accepted)
    //    ← 0x00 / 0x01  (ACK / NACK per data byte)
    {
        const uint8_t addrByte = static_cast<uint8_t>(addr7bit << 1);
        uint8_t request[] = { static_cast<uint8_t>(I2C_BULK_WR_BASE | 0x00), addrByte };

        uint8_t cmdResponse[sizeof(m_positive_response)] = {};
        bOk = generic_uart_send_receive(std::span<uint8_t>(request, sizeof(request)),
                                        numeric::byte2span(cmdResponse),
                                        numeric::byte2span(m_positive_response));
        if (!bOk) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Probe"); LOG_HEX8(addr7bit);
                      LOG_STRING(": bulk-write failed"));
        } else {
            // Drain the single ACK/NACK byte the firmware always emits.
            // Skipping this would misalign every subsequent UART read.
            uint8_t ackByte = 0xFF;
            bOk = generic_uart_send_receive(std::span<uint8_t>{}, numeric::byte2span(ackByte));
            if (!bOk) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("Probe"); LOG_HEX8(addr7bit);
                          LOG_STRING(": ACK/NACK drain failed"));
            } else {
                bAcked = (ackByte == 0x01);
                LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                          LOG_STRING("Probe"); LOG_HEX8(addr7bit);
                          LOG_STRING(bAcked ? "-> ACK (found)" : "-> NACK"));
            }
        }
    }

    // 3. STOP — always release the bus, regardless of earlier errors
    {
        const uint8_t request = I2C_STOP;
        uint8_t response[sizeof(m_positive_response)] = {};
        const bool bStopOk = generic_uart_send_receive(numeric::byte2span(request),
                                                       numeric::byte2span(response),
                                                       numeric::byte2span(m_positive_response));
        if (!bStopOk) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Probe"); LOG_HEX8(addr7bit);
                      LOG_STRING(": STOP failed"));
        }
        bOk = bOk && bStopOk;
    }

#if 0
    // 4. Post-STOP drain: some firmware versions emit an extra 0x07 (NACK
    //    indicator) after the 0x01 STOP confirmation when the address NACKed.
    //    If nothing is there the read times out harmlessly.
    {
        uint8_t drain = 0xFF;
        generic_uart_send_receive(std::span<uint8_t>{}, numeric::byte2span(drain));
        if (drain != 0xFF) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                      LOG_STRING("Probe"); LOG_HEX8(addr7bit);
                      LOG_STRING(": drained extra byte after STOP:"); LOG_UINT8(drain));
        }
    }
#endif
    return bOk;

} /* m_i2c_probe_address() */


/* ============================================================================================
    BuspiratePlugin::m_handle_i2c_scan

    Scans every valid 7-bit I2C address and reports which ones respond with ACK.

    Skipped ranges (reserved by the I2C specification):
      0x00-0x07   general call, CBUS, reserved, Hs-mode master codes
      0x78-0x7F   10-bit addressing prefix, reserved

    Output format (i2cdetect-style grid):
         0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
      00:                         -- -- -- -- -- -- -- --
      10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
      ...
      60: -- -- -- -- 64 -- -- -- -- -- -- -- -- -- -- --
      70: -- -- -- -- -- -- -- --

    Legend:
      XX   device found at address 0xXX
      --   no device (NACK)
      EE   UART communication error during probe
      (blank)  reserved address, not probed

    Usage:
      i2c scan          – run the scan
      i2c scan help     – show this help text
============================================================================================ */
bool BuspiratePlugin::m_handle_i2c_scan(const std::string &args) const
{
    if ("help" == args) {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Probes every valid 7-bit I2C address (0x08-0x77)."));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Output: address hex = found  |  -- = absent  |  EE = UART error"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Blank cells are reserved addresses and are not probed."));
        return true;
    }

    // I2C-spec reserved ranges — do not probe
    static constexpr uint8_t SCAN_FIRST = 0x08;
    static constexpr uint8_t SCAN_LAST  = 0x77;

    if ("all" == args) {
        std::vector<uint8_t> vFound;
        size_t szErrors = 0;

        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("I2C scan  0x08 - 0x77  starting..."));

        // Flush any stale bytes left in the UART RX buffer by preceding
        // mode/peripheral setup commands (e.g. 0x07 NACK trailing byte).
        // m_i2c_flush_rx();

#if (1 == I2C_SCAN_REPORT_TABLE)

        // Header row
        LOG_PRINT(LOG_EMPTY, LOG_STRING("     0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F"));

        for (uint8_t row = 0x00; row <= 0x70; row = static_cast<uint8_t>(row + 0x10)) {
            char rowLabel[6];
            std::snprintf(rowLabel, sizeof(rowLabel), "%02X: ", row);
            std::string line(rowLabel);

            for (uint8_t col = 0; col < 0x10; ++col) {
                const uint8_t addr = static_cast<uint8_t>(row + col);

                if (addr < SCAN_FIRST || addr > SCAN_LAST) {
                    line += "   ";   // 3 chars: reserved — leave blank
                    continue;
                }

                bool bAcked = false;
                if (!m_i2c_probe_address(addr, bAcked)) {
                    line += "EE ";
                    ++szErrors;
                } else if (!bAcked) {
                    char cell[4];
                    std::snprintf(cell, sizeof(cell), "%02X ", addr);
                    line += cell;
                    vFound.push_back(addr);
                } else {
                    line += "-- ";
                }
            }

            LOG_PRINT(LOG_EMPTY, LOG_STRING(line));
        }

        // Summary
        LOG_PRINT(LOG_EMPTY, LOG_STRING(""));

#else
        for(uint8_t addr = SCAN_FIRST; addr <= SCAN_LAST; ++addr ) {
            bool bAcked = false;

            if (!m_i2c_probe_address(addr, bAcked)) {
                ++szErrors;
            } else if (!bAcked) {
                vFound.push_back(addr);
            }
        }

#endif /*(1 == I2C_SCAN_REPORT_TABLE)*/

        if (vFound.empty()) {
            LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("No devices found."));
        } else {
            LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Devices found:"); LOG_SIZET(vFound.size()));
            for (const uint8_t addr : vFound) {
                LOG_PRINT(LOG_INFO, LOG_HEX8(addr); LOG_STRING(":"); LOG_UINT8(addr));
            }
        }

        if (szErrors > 0) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("UART errors during scan:"); LOG_SIZET(szErrors));
        }

        return (szErrors == 0);
    }

    // expected a string convertible to a number (address)
    uint8_t addr = 0; 
    if (!numeric::str2uint8(args, addr)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Wrong argument:"); LOG_STRING(args));
        return false;
    }

    // correct as number but out of range
    if ((addr < SCAN_FIRST) || (addr > SCAN_LAST)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Address out of range:"); LOG_HEX8(addr));
        return false;
    }

    // correct address, try to see if a device is present
    bool bAcked = false;
    if (!m_i2c_probe_address(addr, bAcked)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to scan at the address:"); LOG_HEX8(addr));
    } else if (bAcked) {
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("I2C device found:"); LOG_HEX8(addr));
    } else {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("I2C device not found:"); LOG_HEX8(addr));
    }

    return true;

} /* m_handle_i2c_scan() */


/* ============================================================================================
    BuspiratePlugin::m_i2c_send_bit
============================================================================================ */
bool BuspiratePlugin::m_i2c_send_bit(uint8_t bit) const
{
    return generic_uart_send_receive(
        numeric::byte2span(bit),       
        numeric::byte2span(m_scratch_response),
        numeric::byte2span(m_positive_response)); // expect 0x01

} /* m_i2c_send_bit() */


/* ============================================================================================
    BuspiratePlugin::m_i2c_write_transaction
============================================================================================ */
bool BuspiratePlugin::m_i2c_write_transaction(std::span<const uint8_t> payload) const
{
    return m_i2c_send_bit(I2C_START)
        && m_i2c_bulk_write(payload)
        && m_i2c_send_bit(I2C_STOP);

} /* m_i2c_send_stop() */