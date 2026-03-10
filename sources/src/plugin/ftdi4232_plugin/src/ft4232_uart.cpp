/*
 * FT4232H Plugin – UART protocol handlers
 *
 * Uses FT4232UART driver on async UART channels C or D.
 * Channels A/B are MPSSE-only and cannot be used here.
 *
 * Subcommands:
 *   open   [baud=N] [data=8] [stop=1] [parity=none|odd|even|mark|space]
 *          [flow=none|hw] [channel=C|D] [device=N]
 *   close
 *   cfg    [baud=N] [data=8] [stop=1] [parity=none|odd|even|mark|space]
 *          [flow=none|hw] [channel=C|D]
 *   write  AABBCC…
 *   read   N
 *   script SCRIPTNAME
 *   help
 */

#include "ft4232_plugin.hpp"
#include "ft4232_generic.hpp"

#include "uString.hpp"
#include "uNumeric.hpp"
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
#define LT_HDR   "FT_UART    |"
#define LOG_HDR  LOG_STRING(LT_HDR)

#define PROTOCOL_NAME "UART"

///////////////////////////////////////////////////////////////////
//                    STATIC PARSE HELPERS                       //
///////////////////////////////////////////////////////////////////

static bool parseParity(const std::string& s, uint8_t& out)
{
    if (s=="none" ||s=="NONE" ) { out=0; return true; }
    if (s=="odd"  ||s=="ODD"  ) { out=1; return true; }
    if (s=="even" ||s=="EVEN" ) { out=2; return true; }
    if (s=="mark" ||s=="MARK" ) { out=3; return true; }
    if (s=="space"||s=="SPACE") { out=4; return true; }
    LOG_PRINT(LOG_ERROR, LOG_HDR;
              LOG_STRING("Invalid parity (none|odd|even|mark|space):"); LOG_STRING(s));
    return false;
}

bool FT4232Plugin::parseUartParams(const std::string& args, UartPendingCfg& cfg,
                                    uint8_t* pDeviceIndexOut)
{
    std::vector<std::string> pairs;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, pairs);
    bool ok = true;
    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() != 2) continue;
        const auto& k = kv[0];
        const auto& v = kv[1];
        if      (k == "baud"   ) ok &= numeric::str2uint32(v, cfg.baudRate);
        else if (k == "data"   ) ok &= numeric::str2uint8 (v, cfg.dataBits);
        else if (k == "stop"   ) ok &= numeric::str2uint8 (v, cfg.stopBits);
        else if (k == "parity" ) ok &= parseParity        (v, cfg.parity);
        else if (k == "flow"   ) cfg.hwFlowCtrl = (v == "hw" || v == "HW" || v == "rtscts");
        else if (k == "channel") ok &= parseChannel(v, cfg.channel);
        else if (k == "device" && pDeviceIndexOut) ok &= numeric::str2uint8(v, *pDeviceIndexOut);
        if (!ok) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid value for:"); LOG_STRING(k);
                      LOG_STRING("="); LOG_STRING(v));
            return false;
        }
    }
    return ok;
}

///////////////////////////////////////////////////////////////////
//                       HELP                                    //
///////////////////////////////////////////////////////////////////

bool FT4232Plugin::m_handle_uart_help(const std::string&) const
{
    return generic_module_list_commands<FT4232Plugin>(this, PROTOCOL_NAME);
}

///////////////////////////////////////////////////////////////////
//                       OPEN                                    //
///////////////////////////////////////////////////////////////////

bool FT4232Plugin::m_handle_uart_open(const std::string& args) const
{
    if (m_pUART && m_pUART->is_open()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("UART already open — close first"));
        return false;
    }

    uint8_t devIdx = m_sIniValues.u8DeviceIndex;
    if (!parseUartParams(args, m_sUartCfg, &devIdx)) return false;

    // Only channels C or D support async UART
    if (m_sUartCfg.channel != FT4232Base::Channel::C &&
        m_sUartCfg.channel != FT4232Base::Channel::D) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("UART is only supported on channels C and D"));
        return false;
    }

    m_pUART = std::make_unique<FT4232UART>();
    auto s = m_pUART->open(m_sUartCfg, devIdx);
    if (s != FT4232UART::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("UART open failed, status=");
                  LOG_UINT32(static_cast<uint32_t>(s)));
        m_pUART.reset();
        return false;
    }
    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("UART opened ch="); LOG_UINT8(static_cast<uint8_t>(m_sUartCfg.channel));
              LOG_STRING("baud="); LOG_UINT32(m_sUartCfg.baudRate));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CLOSE                                   //
///////////////////////////////////////////////////////////////////

bool FT4232Plugin::m_handle_uart_close(const std::string&) const
{
    if (!m_pUART) { LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("UART not open")); return true; }
    m_pUART->close();
    m_pUART.reset();
    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("UART closed"));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CFG                                     //
///////////////////////////////////////////////////////////////////

bool FT4232Plugin::m_handle_uart_cfg(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Use: [baud=N] [data=8] [stop=1] [parity=none|odd|even|mark|space]"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("     [flow=none|hw] [channel=C|D]"));
        return true;
    }
    if (!parseUartParams(args, m_sUartCfg, nullptr)) return false;
    if (m_pUART && m_pUART->is_open()) {
        auto s = m_pUART->configure(m_sUartCfg);
        if (s != FT4232UART::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("UART configure failed"));
            return false;
        }
    }
    return true;
}

///////////////////////////////////////////////////////////////////
//                       WRITE                                   //
///////////////////////////////////////////////////////////////////

bool FT4232Plugin::m_handle_uart_write(const std::string& args) const
{
    if (args == "help") { LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: write AABBCC…")); return true; }
    auto* pDrv = m_uart(); if (!pDrv) return false;
    std::vector<uint8_t> data;
    if (!hexutils::stringUnhexlify(args, data) || data.empty()) return false;
    auto r = pDrv->tout_write(0, data);
    return r.status == FT4232UART::Status::SUCCESS;
}

///////////////////////////////////////////////////////////////////
//                       READ                                    //
///////////////////////////////////////////////////////////////////

bool FT4232Plugin::m_handle_uart_read(const std::string& args) const
{
    if (args == "help") { LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: read N")); return true; }
    auto* pDrv = m_uart(); if (!pDrv) return false;
    size_t n = 0;
    if (!numeric::str2sizet(args, n) || n == 0) return false;
    std::vector<uint8_t> buf(n);
    auto r = pDrv->tout_read(m_sIniValues.u32ReadTimeout, buf, {});
    if (r.status != FT4232UART::Status::SUCCESS) return false;
    hexutils::HexDump2(buf.data(), r.bytes_read);
    return true;
}

///////////////////////////////////////////////////////////////////
//                       SCRIPT                                  //
///////////////////////////////////////////////////////////////////

bool FT4232Plugin::m_handle_uart_script(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: <scriptname>"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("  Executes script from ARTEFACTS_PATH/scriptname"));
        return true;
    }
    auto* pDrv = m_uart(); if (!pDrv) return false;
    const auto* ini = getAccessIniValues(*this);
    return generic_execute_script(pDrv, args, ini->strArtefactsPath,
                                   FT_BULK_MAX_BYTES,
                                   ini->u32ReadTimeout,
                                   ini->u32ScriptDelay);
}
