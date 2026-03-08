/*
 * HydraBus Plugin – UART protocol handlers
 *
 * Subcommands:
 *   baud    N           (arbitrary baud rate, e.g. 115200)
 *   parity  [none|even|odd]
 *   echo    [on|off]
 *   bridge              (transparent bridge – exit with UBTN)
 *   write   AABB..      (hex, 1-16 bytes)
 *   read    N           (read N bytes)
 *   aux     N [in|out|pp] [0|1]
 *   help
 */

#include "hydrabus_plugin.hpp"
#include "hydrabus_generic.hpp"

#include "uNumeric.hpp"
#include "uHexlify.hpp"
#include "uHexdump.hpp"
#include "uLogger.hpp"

///////////////////////////////////////////////////////////////////
//                       LOG DEFINES                             //
///////////////////////////////////////////////////////////////////

#ifdef  LT_HDR
#undef  LT_HDR
#endif
#ifdef  LOG_HDR
#undef  LOG_HDR
#endif
#define LT_HDR   "HB_UART    |"
#define LOG_HDR  LOG_STRING(LT_HDR)

#define PROTOCOL_NAME "UART"

///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_uart_help(const std::string&) const
{
    return generic_module_list_commands<HydrabusPlugin>(this, PROTOCOL_NAME);
}

///////////////////////////////////////////////////////////////////
//                       BAUD                                    //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_uart_baud(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: baud N  (e.g. baud 115200)"));
        return true;
    }
    auto* p = m_uart();
    if (!p) return false;

    uint32_t baud = 0;
    if (!numeric::str2uint32(args, baud) || baud == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid baud rate:"); LOG_STRING(args));
        return false;
    }
    if (!p->set_baud(baud)) return false;

    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Baud rate set to"); LOG_UINT32(baud));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       PARITY                                  //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_uart_parity(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: parity [none|even|odd]"));
        return true;
    }
    auto* p = m_uart();
    if (!p) return false;

    HydraHAL::UART::Parity par;
    if      (args == "none") par = HydraHAL::UART::Parity::None;
    else if (args == "even") par = HydraHAL::UART::Parity::Even;
    else if (args == "odd")  par = HydraHAL::UART::Parity::Odd;
    else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown parity:"); LOG_STRING(args));
        return false;
    }

    return p->set_parity(par);
}

///////////////////////////////////////////////////////////////////
//                       ECHO                                    //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_uart_echo(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: echo [on|off]"));
        return true;
    }
    auto* p = m_uart();
    if (!p) return false;

    if      (args == "on")  return p->set_echo(true);
    else if (args == "off") return p->set_echo(false);
    else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected on or off"));
        return false;
    }
}

///////////////////////////////////////////////////////////////////
//                       BRIDGE                                  //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_uart_bridge(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Transparent bridge mode. Press UBTN on HydraBus to exit."));
        return true;
    }
    auto* p = m_uart();
    if (!p) return false;

    LOG_PRINT(LOG_FIXED, LOG_HDR;
              LOG_STRING("Entering bridge mode – press UBTN on HydraBus to return"));
    p->enter_bridge();
    // enter_bridge() is blocking until the user presses UBTN
    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Bridge mode exited"));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       WRITE                                   //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_uart_write(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: write AABB..  (hex, 1-16 bytes)"));
        return true;
    }
    auto* p = m_uart();
    if (!p) return false;

    std::vector<uint8_t> data;
    if (!hexutils::stringUnhexlify(args, data) || data.empty() || data.size() > 16) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected 1-16 hex bytes"));
        return false;
    }
    return p->bulk_write(data);
}

///////////////////////////////////////////////////////////////////
//                       READ                                    //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_uart_read(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: read N"));
        return true;
    }
    auto* p = m_uart();
    if (!p) return false;

    size_t n = 0;
    if (!numeric::str2sizet(args, n) || n == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid byte count"));
        return false;
    }

    auto data = p->read(n);
    hexutils::HexDump2(data.data(), data.size());
    return true;
}

///////////////////////////////////////////////////////////////////
//                       AUX                                     //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_uart_aux(const std::string& args) const
{
    return m_handle_aux_common(args, m_uart());
}
