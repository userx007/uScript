/*
 * FT232H Plugin – UART protocol handlers
 *
 * Uses FT232HUART driver which opens the FT232H single interface in
 * async serial (VCP) mode rather than MPSSE mode.
 *
 * Note: UART and MPSSE modes are mutually exclusive on one physical
 * chip.  If MPSSE modules (SPI / I2C / GPIO) are needed simultaneously,
 * use a separate FT232H chip and select it with device=N.
 *
 * Subcommands:
 *   open   [baud=N] [data=8] [stop=0] [parity=none|odd|even|mark|space]
 *          [flow=none|hw] [device=N]
 *   close
 *   cfg    [baud=N] [data=8] [stop=0] [parity=none|odd|even|mark|space]
 *          [flow=none|hw]
 *   write  AABBCC…
 *   read   N
 *   script SCRIPTNAME
 *   help
 */

#include "ft232h_plugin.hpp"
#include "ft232h_generic.hpp"

#include "uString.hpp"
#include "uNumeric.hpp"
#include "uHexdump.hpp"
#include "uLogger.hpp"

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "FT232H_UART |"
#define LOG_HDR    LOG_STRING(LT_HDR)

#define PROTOCOL_NAME "UART"

///////////////////////////////////////////////////////////////////
//                       HELP                                    //
///////////////////////////////////////////////////////////////////

bool FT232HPlugin::m_handle_uart_help(const std::string&) const
{
    return generic_module_list_commands<FT232HPlugin>(this, PROTOCOL_NAME);
}

///////////////////////////////////////////////////////////////////
//                       OPEN                                    //
///////////////////////////////////////////////////////////////////

bool FT232HPlugin::m_handle_uart_open(const std::string& args) const
{
    if (m_pUART && m_pUART->is_open()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("UART already open — close first"));
        return false;
    }

    uint8_t devIdx = m_sIniValues.u8DeviceIndex;
    if (!parseUartParams(args, m_sUartCfg, &devIdx)) return false;

    m_pUART = std::make_unique<FT232HUART>();
    auto s = m_pUART->open(m_sUartCfg, devIdx);
    if (s != FT232HUART::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("UART open failed, status=");
                  LOG_UINT32(static_cast<uint32_t>(s)));
        m_pUART.reset();
        return false;
    }
    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("UART opened baud="); LOG_UINT32(m_sUartCfg.baudRate));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CLOSE                                   //
///////////////////////////////////////////////////////////////////

bool FT232HPlugin::m_handle_uart_close(const std::string&) const
{
    if (!m_pUART) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("UART not open"));
        return true;
    }
    m_pUART->close();
    m_pUART.reset();
    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("UART closed"));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CFG                                     //
///////////////////////////////////////////////////////////////////

bool FT232HPlugin::m_handle_uart_cfg(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: [baud=N] [data=8] [stop=0] [parity=none|odd|even|mark|space]"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("     [flow=none|hw]"));
        return true;
    }
    if (!parseUartParams(args, m_sUartCfg, nullptr)) return false;
    if (m_pUART && m_pUART->is_open()) {
        auto s = m_pUART->configure(m_sUartCfg);
        if (s != FT232HUART::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("UART configure failed"));
            return false;
        }
    }
    return true;
}

///////////////////////////////////////////////////////////////////
//                       WRITE                                   //
///////////////////////////////////////////////////////////////////

bool FT232HPlugin::m_handle_uart_write(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: write AABBCC…"));
        return true;
    }
    auto* pDrv = m_uart(); if (!pDrv) return false;
    std::vector<uint8_t> data;
    if (!hexutils::stringUnhexlify(args, data) || data.empty()) return false;
    auto r = pDrv->tout_write(0, data);
    return r.status == FT232HUART::Status::SUCCESS;
}

///////////////////////////////////////////////////////////////////
//                       READ                                    //
///////////////////////////////////////////////////////////////////

bool FT232HPlugin::m_handle_uart_read(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: read N"));
        return true;
    }
    auto* pDrv = m_uart(); if (!pDrv) return false;
    size_t n = 0;
    if (!numeric::str2sizet(args, n) || n == 0) return false;
    std::vector<uint8_t> buf(n);
    auto r = pDrv->tout_read(m_sIniValues.u32ReadTimeout, buf, {});
    if (r.status != FT232HUART::Status::SUCCESS) return false;
    hexutils::HexDump2(buf.data(), r.bytes_read);
    return true;
}

///////////////////////////////////////////////////////////////////
//                       SCRIPT                                  //
///////////////////////////////////////////////////////////////////

bool FT232HPlugin::m_handle_uart_script(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: <scriptname>"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  Executes script from ARTEFACTS_PATH/scriptname"));
        return true;
    }
    auto* pDrv = m_uart(); if (!pDrv) return false;
    const auto* ini = getAccessIniValues(*this);
    return generic_execute_script(  
            pDrv, 
            args, 
            ini->strArtefactsPath,
            FT_BULK_MAX_BYTES,
            ini->u32ReadTimeout,
            ini->u32ScriptDelay,
            m_bIsEnabled);
}
