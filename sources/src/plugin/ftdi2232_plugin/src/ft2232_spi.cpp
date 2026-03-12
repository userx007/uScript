/*
 * FT2232 Plugin – SPI protocol handlers
 *
 * Identical in structure to the FT4232H SPI handlers.
 * Key difference: open() and cfg() accept variant=H|D which is forwarded
 * to FT2232SPI::open().  FT2232D caps at 3 MHz — enforced via
 * checkVariantSpeedLimit() before each open / speed change.
 *
 * Subcommands:
 *   open   [variant=H|D] [clock=N] [mode=0-3] [bitorder=msb|lsb]
 *          [cspin=N] [cspol=low|high] [channel=A|B] [device=N]
 *   close
 *   cfg    [variant=H|D] [clock=N] [mode=0-3] [bitorder=msb|lsb]
 *          [cspin=N] [cspol=low|high]
 *   cs     [en|dis]  (informational — CS managed per-transfer)
 *   write  AABB..    (hex bytes, MOSI only)
 *   read   N         (read N bytes, clocks 0x00)
 *   wrrd   [hexdata][:rdlen]
 *   wrrdf  filename[:wrchunk][:rdchunk]
 *   xfer   AABB..    (full-duplex, prints MISO)
 *   help
 */

#include "ft2232_plugin.hpp"
#include "ft2232_generic.hpp"

#include "uString.hpp"
#include "uNumeric.hpp"
#include "uHexlify.hpp"
#include "uHexdump.hpp"
#include "uLogger.hpp"

#include <vector>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "FT2_SPI     |"
#define LOG_HDR    LOG_STRING(LT_HDR)

#define PROTOCOL_NAME "SPI"

///////////////////////////////////////////////////////////////////
//              Internal: parse SPI key=value pair               //
///////////////////////////////////////////////////////////////////

bool FT2232Plugin::parseSpiKV(const std::string& key, const std::string& val,
                               SpiPendingCfg& cfg)
{
    (void)cfg;
    (void)key;
    (void)val;
    return true;
}

///////////////////////////////////////////////////////////////////
//                       HELP                                    //
///////////////////////////////////////////////////////////////////

bool FT2232Plugin::m_handle_spi_help(const std::string&) const
{
    return generic_module_list_commands<FT2232Plugin>(this, PROTOCOL_NAME);
}

///////////////////////////////////////////////////////////////////
//             Internal: shared key=value parser for open/cfg    //
///////////////////////////////////////////////////////////////////

bool FT2232Plugin::parseSpiParams(const std::string& args,
                                   SpiPendingCfg& cfg,
                                   uint8_t* pDeviceIndexOut)
{
    std::vector<std::string> pairs;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, pairs);

    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() != 2) continue;

        bool ok = true;
        if (kv[0] == "variant") {
            ok = parseVariant(kv[1], cfg.variant);
        } else if (kv[0] == "clock") {
            ok = numeric::str2uint32(kv[1], cfg.clockHz);
        } else if (kv[0] == "mode") {
            uint8_t v = 0;
            ok = numeric::str2uint8(kv[1], v);
            if (ok && v <= 3) cfg.mode = static_cast<FT2232SPI::SpiMode>(v);
            else ok = false;
        } else if (kv[0] == "bitorder") {
            if      (kv[1] == "msb") cfg.bitOrder = FT2232SPI::BitOrder::MsbFirst;
            else if (kv[1] == "lsb") cfg.bitOrder = FT2232SPI::BitOrder::LsbFirst;
            else ok = false;
        } else if (kv[0] == "cspin") {
            ok = numeric::str2uint8(kv[1], cfg.csPin);
        } else if (kv[0] == "cspol") {
            if      (kv[1] == "low")  cfg.csPolarity = FT2232SPI::CsPolarity::ActiveLow;
            else if (kv[1] == "high") cfg.csPolarity = FT2232SPI::CsPolarity::ActiveHigh;
            else ok = false;
        } else if (kv[0] == "channel") {
            ok = parseChannel(kv[1], cfg.channel);
        } else if (kv[0] == "device" && pDeviceIndexOut) {
            ok = numeric::str2uint8(kv[1], *pDeviceIndexOut);
        } else {
            LOG_PRINT(LOG_ERROR, LOG_STRING("FT2_SPI    |");
                      LOG_STRING("Unknown key:"); LOG_STRING(kv[0]));
            return false;
        }

        if (!ok) {
            LOG_PRINT(LOG_ERROR, LOG_STRING("FT2_SPI    |");
                      LOG_STRING("Invalid value for:"); LOG_STRING(kv[0]));
            return false;
        }
    }
    return true;
}

///////////////////////////////////////////////////////////////////
//                       OPEN                                    //
///////////////////////////////////////////////////////////////////

bool FT2232Plugin::m_handle_spi_open(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: open [variant=H|D] [clock=N] [mode=0-3]"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("          [bitorder=msb|lsb] [cspin=N] [cspol=low|high]"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("          [channel=A|B] [device=N]"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("  FT2232D: channel A only, max clock 3 MHz"));
        return true;
    }

    uint8_t devIdx = m_sIniValues.u8DeviceIndex;
    if (!parseSpiParams(args, m_sSpiCfg, &devIdx)) return false;
    const_cast<FT2232Plugin*>(this)->m_sIniValues.u8DeviceIndex = devIdx;

    // Variant speed guard
    if (!checkVariantSpeedLimit(m_sSpiCfg.variant, "SPI", m_sSpiCfg.clockHz)) return false;

    // FT2232D only has channel A
    if (m_sSpiCfg.variant == FT2232Base::Variant::FT2232D &&
        m_sSpiCfg.channel != FT2232Base::Channel::A) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("FT2232D has MPSSE on channel A only"));
        return false;
    }

    if (m_pSPI) { m_pSPI->close(); m_pSPI.reset(); }

    FT2232SPI::SpiConfig cfg;
    cfg.clockHz    = m_sSpiCfg.clockHz;
    cfg.mode       = m_sSpiCfg.mode;
    cfg.bitOrder   = m_sSpiCfg.bitOrder;
    cfg.csPin      = m_sSpiCfg.csPin;
    cfg.csPolarity = m_sSpiCfg.csPolarity;
    cfg.variant    = m_sSpiCfg.variant;
    cfg.channel    = m_sSpiCfg.channel;

    m_pSPI = std::make_unique<FT2232SPI>();
    auto s = m_pSPI->open(cfg, m_sIniValues.u8DeviceIndex);
    if (s != FT2232SPI::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI open failed"));
        m_pSPI.reset();
        return false;
    }

    const char* varStr = (cfg.variant == FT2232Base::Variant::FT2232H) ? "H" : "D";
    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("SPI opened: variant="); LOG_STRING(varStr);
              LOG_STRING("clock="); LOG_UINT32(cfg.clockHz);
              LOG_STRING("mode="); LOG_UINT32(static_cast<uint8_t>(cfg.mode));
              LOG_STRING("ch="); LOG_UINT32(static_cast<uint8_t>(cfg.channel)));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CLOSE                                   //
///////////////////////////////////////////////////////////////////

bool FT2232Plugin::m_handle_spi_close(const std::string&) const
{
    if (m_pSPI) {
        m_pSPI->close();
        m_pSPI.reset();
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("SPI closed"));
    } else {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("SPI was not open"));
    }
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CFG                                     //
///////////////////////////////////////////////////////////////////

bool FT2232Plugin::m_handle_spi_cfg(const std::string& args) const
{
    if (args == "help" || args == "?") {
        const char* varStr = (m_sSpiCfg.variant == FT2232Base::Variant::FT2232H) ? "H" : "D";
        LOG_PRINT(LOG_EMPTY, LOG_STRING("SPI pending config:"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("  variant=");  LOG_STRING(varStr);
                  LOG_STRING("clock=");      LOG_UINT32(m_sSpiCfg.clockHz);
                  LOG_STRING("mode=");       LOG_UINT32(static_cast<uint8_t>(m_sSpiCfg.mode));
                  LOG_STRING("bitorder=");   LOG_STRING(m_sSpiCfg.bitOrder == FT2232SPI::BitOrder::MsbFirst ? "msb" : "lsb");
                  LOG_STRING("cspin=0x");    LOG_HEX8(m_sSpiCfg.csPin);
                  LOG_STRING("cspol=");      LOG_STRING(m_sSpiCfg.csPolarity == FT2232SPI::CsPolarity::ActiveLow ? "low" : "high"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: cfg [variant=H|D] [clock=N] [mode=0-3] [bitorder=msb|lsb] [cspin=N] [cspol=low|high]"));
        return true;
    }

    if (!parseSpiParams(args, m_sSpiCfg)) return false;

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("SPI config updated (takes effect on next open)"));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CS (informational)                      //
///////////////////////////////////////////////////////////////////

bool FT2232Plugin::m_handle_spi_cs(const std::string& /*args*/) const
{
    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("CS is automatically asserted/deasserted per transfer."));
    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Use write/read/wrrd/xfer for CS-guarded transactions."));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       WRITE                                   //
///////////////////////////////////////////////////////////////////

bool FT2232Plugin::m_handle_spi_write(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: write AABB..  (hex bytes, MOSI only)"));
        return true;
    }
    auto* p = m_spi();
    if (!p) return false;

    std::vector<uint8_t> data;
    if (!hexutils::stringUnhexlify(args, data) || data.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected at least 1 hex byte"));
        return false;
    }

    auto result = p->tout_write(0, data);
    if (result.status != FT2232SPI::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Write failed, bytes written:"); LOG_SIZET(result.bytes_written));
        return false;
    }
    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Wrote"); LOG_SIZET(result.bytes_written); LOG_STRING("bytes"));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       READ                                    //
///////////////////////////////////////////////////////////////////

bool FT2232Plugin::m_handle_spi_read(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: read N  (read N bytes, clocks 0x00)"));
        return true;
    }
    auto* p = m_spi();
    if (!p) return false;

    size_t n = 0;
    if (!numeric::str2sizet(args, n) || n == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid byte count"));
        return false;
    }

    std::vector<uint8_t> buf(n);
    ICommDriver::ReadOptions opts;
    opts.mode = ICommDriver::ReadMode::Exact;

    auto result = p->tout_read(0, buf, opts);
    if (result.status != FT2232SPI::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Read failed, bytes read:"); LOG_SIZET(result.bytes_read));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("MISO:"));
    hexutils::HexDump2(buf.data(), result.bytes_read);
    return true;
}

///////////////////////////////////////////////////////////////////
//                       WRRD / WRRDF                            //
///////////////////////////////////////////////////////////////////

bool FT2232Plugin::m_spi_wrrd_cb(std::span<const uint8_t> req, size_t rdlen) const
{
    auto* p = m_spi();
    if (!p) return false;

    if (rdlen == 0 && req.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("wrrd: nothing to do"));
        return false;
    }

    if (rdlen == 0) {
        auto r = p->tout_write(0, req);
        return r.status == FT2232SPI::Status::SUCCESS;
    }

    if (req.empty()) {
        std::vector<uint8_t> rxBuf(rdlen);
        ICommDriver::ReadOptions opts;
        opts.mode = ICommDriver::ReadMode::Exact;
        auto r = p->tout_read(0, rxBuf, opts);
        if (r.status != FT2232SPI::Status::SUCCESS) return false;
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Read:"));
        hexutils::HexDump2(rxBuf.data(), r.bytes_read);
        return true;
    }

    // Write + Read: full-duplex spi_transfer()
    size_t totalLen = req.size() + rdlen;
    std::vector<uint8_t> txBuf(totalLen, 0x00u);
    std::copy(req.begin(), req.end(), txBuf.begin());
    std::vector<uint8_t> rxBuf(totalLen, 0x00u);

    auto r = p->spi_transfer(txBuf, rxBuf, 0u);
    if (r.status != FT2232SPI::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("spi_transfer failed, bytes exchanged:"); LOG_SIZET(r.bytes_xfered));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Read:"));
    hexutils::HexDump2(rxBuf.data() + req.size(), rdlen);
    return true;
}

bool FT2232Plugin::m_handle_spi_wrrd(const std::string& args) const
{
    return generic_write_read_data<FT2232Plugin>(
        this, args, &FT2232Plugin::m_spi_wrrd_cb);
}

bool FT2232Plugin::m_handle_spi_wrrdf(const std::string& args) const
{
    return generic_write_read_file<FT2232Plugin>(
        this, args, &FT2232Plugin::m_spi_wrrd_cb,
        m_sIniValues.strArtefactsPath);
}

///////////////////////////////////////////////////////////////////
//                       XFER (full-duplex)                      //
///////////////////////////////////////////////////////////////////

bool FT2232Plugin::m_handle_spi_xfer(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: xfer AABB..  (full-duplex: TX hex, MISO printed)"));
        return true;
    }
    auto* p = m_spi();
    if (!p) return false;

    std::vector<uint8_t> txBuf;
    if (!hexutils::stringUnhexlify(args, txBuf) || txBuf.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected at least 1 hex byte"));
        return false;
    }

    std::vector<uint8_t> rxBuf(txBuf.size(), 0x00u);
    auto result = p->spi_transfer(txBuf, rxBuf, 0u);

    if (result.status != FT2232SPI::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("xfer failed, bytes exchanged:"); LOG_SIZET(result.bytes_xfered));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("MISO:"));
    hexutils::HexDump2(rxBuf.data(), result.bytes_xfered);
    return true;
}

/* ============================================================
   m_handle_spi_script
   Execute a CommScriptClient script through the open SPI driver.
   The SPI port must be opened first ("FT2232.SPI open ...").

   Usage:  FT2232.SPI script <filename>
           FT2232.SPI script help
============================================================ */
bool FT2232Plugin::m_handle_spi_script(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: script <filename>"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  Executes script from ARTEFACTS_PATH/filename"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  SPI must be open first (FT2232.SPI open ...)"));
        return true;
    }

    auto* pSpi = m_spi();
    if (!pSpi) return false;

    const auto* ini = getAccessIniValues(*this);
    return generic_execute_script(
        pSpi,
        args,
        ini->strArtefactsPath,
        FT_BULK_MAX_BYTES,
        ini->u32ReadTimeout,
        ini->u32ScriptDelay);
}
