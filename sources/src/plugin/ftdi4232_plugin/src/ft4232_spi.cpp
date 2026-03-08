/*
 * FT4232H Plugin – SPI protocol handlers
 *
 * Subcommands:
 *   open   [clock=N] [mode=0-3] [bitorder=msb|lsb] [cspin=N] [cspol=low|high] [channel=A|B] [device=N]
 *   close
 *   cfg    [clock=N] [mode=0-3] [bitorder=msb|lsb] [cspin=N] [cspol=low|high]  (stored, applied on next open)
 *   cs     [en|dis]
 *   write  AABB..     (hex bytes, MOSI only — no read-back)
 *   read   N          (read N bytes, clocks 0x00 on MOSI)
 *   wrrd   [hexdata][:rdlen]
 *   wrrdf  filename[:wrchunk][:rdchunk]
 *   xfer   AABB..     (full-duplex: TX hex bytes, print simultaneous MISO)
 *   help
 *
 * Notes on wrrd:
 *   If both write and read bytes are requested, the call uses spi_transfer()
 *   which pads the TX buffer with 0x00 for the read phase — CS remains
 *   asserted for the whole transaction.
 *   If only write bytes are requested, tout_write() is used.
 *   If only read bytes are requested, tout_read() is used.
 */

#include "ft4232_plugin.hpp"
#include "ft4232_generic.hpp"

#include "uString.hpp"
#include "uNumeric.hpp"
#include "uHexlify.hpp"
#include "uHexdump.hpp"
#include "uLogger.hpp"

#include <vector>

///////////////////////////////////////////////////////////////////
//                       LOG DEFINES                             //
///////////////////////////////////////////////////////////////////

#ifdef  LT_HDR
#undef  LT_HDR
#endif
#ifdef  LOG_HDR
#undef  LOG_HDR
#endif
#define LT_HDR   "FT_SPI     |"
#define LOG_HDR  LOG_STRING(LT_HDR)

#define PROTOCOL_NAME "SPI"

///////////////////////////////////////////////////////////////////
//                       HELP                                    //
///////////////////////////////////////////////////////////////////

bool FT4232Plugin::m_handle_spi_help(const std::string&) const
{
    return generic_module_list_commands<FT4232Plugin>(this, PROTOCOL_NAME);
}

///////////////////////////////////////////////////////////////////
//                       OPEN                                    //
///////////////////////////////////////////////////////////////////

bool FT4232Plugin::m_handle_spi_open(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Use: open [clock=Hz] [mode=0-3] [bitorder=msb|lsb]"));
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("          [cspin=8] [cspol=low|high] [channel=A|B] [device=N]"));
        return true;
    }

    // Parse optional key=value pairs
    std::vector<std::string> pairs;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, pairs);

    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() != 2) continue;

        bool ok = true;
        if (kv[0] == "clock") {
            ok = numeric::str2uint32(kv[1], m_sSpiCfg.clockHz);
        } else if (kv[0] == "mode") {
            uint8_t v = 0;
            ok = numeric::str2uint8(kv[1], v);
            if (ok && v <= 3) m_sSpiCfg.mode = static_cast<FT4232SPI::SpiMode>(v);
            else ok = false;
        } else if (kv[0] == "bitorder") {
            if      (kv[1] == "msb") m_sSpiCfg.bitOrder = FT4232SPI::BitOrder::MsbFirst;
            else if (kv[1] == "lsb") m_sSpiCfg.bitOrder = FT4232SPI::BitOrder::LsbFirst;
            else ok = false;
        } else if (kv[0] == "cspin") {
            ok = numeric::str2uint8(kv[1], m_sSpiCfg.csPin);
        } else if (kv[0] == "cspol") {
            if      (kv[1] == "low")  m_sSpiCfg.csPolarity = FT4232SPI::CsPolarity::ActiveLow;
            else if (kv[1] == "high") m_sSpiCfg.csPolarity = FT4232SPI::CsPolarity::ActiveHigh;
            else ok = false;
        } else if (kv[0] == "channel") {
            ok = parseChannel(kv[1], m_sSpiCfg.channel);
        } else if (kv[0] == "device") {
            uint8_t v = 0;
            ok = numeric::str2uint8(kv[1], v);
            if (ok) const_cast<FT4232Plugin*>(this)->m_sIniValues.u8DeviceIndex = v;
        } else {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown key:"); LOG_STRING(kv[0]));
            return false;
        }

        if (!ok) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Invalid value for:"); LOG_STRING(kv[0]);
                      LOG_STRING("="); LOG_STRING(kv[1]));
            return false;
        }
    }

    // Close any existing instance before reopening
    if (m_pSPI) { m_pSPI->close(); m_pSPI.reset(); }

    FT4232SPI::SpiConfig cfg;
    cfg.clockHz    = m_sSpiCfg.clockHz;
    cfg.mode       = m_sSpiCfg.mode;
    cfg.bitOrder   = m_sSpiCfg.bitOrder;
    cfg.csPin      = m_sSpiCfg.csPin;
    cfg.csPolarity = m_sSpiCfg.csPolarity;
    cfg.channel    = m_sSpiCfg.channel;

    m_pSPI = std::make_unique<FT4232SPI>();
    auto s = m_pSPI->open(cfg, m_sIniValues.u8DeviceIndex);
    if (s != FT4232SPI::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI open failed"));
        m_pSPI.reset();
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("SPI opened: clock="); LOG_UINT32(cfg.clockHz);
              LOG_STRING("mode="); LOG_UINT32(static_cast<uint8_t>(cfg.mode));
              LOG_STRING("ch="); LOG_UINT32(static_cast<uint8_t>(cfg.channel)));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CLOSE                                   //
///////////////////////////////////////////////////////////////////

bool FT4232Plugin::m_handle_spi_close(const std::string&) const
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

bool FT4232Plugin::m_handle_spi_cfg(const std::string& args) const
{
    if (args == "help" || args == "?") {
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("SPI pending config:"));
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("  clock=");    LOG_UINT32(m_sSpiCfg.clockHz);
                  LOG_STRING("mode=");       LOG_UINT32(static_cast<uint8_t>(m_sSpiCfg.mode));
                  LOG_STRING("bitorder=");   LOG_STRING(m_sSpiCfg.bitOrder == FT4232SPI::BitOrder::MsbFirst ? "msb" : "lsb");
                  LOG_STRING("cspin=0x");    LOG_HEX8(m_sSpiCfg.csPin);
                  LOG_STRING("cspol=");      LOG_STRING(m_sSpiCfg.csPolarity == FT4232SPI::CsPolarity::ActiveLow ? "low" : "high"));
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Use: cfg [clock=N] [mode=0-3] [bitorder=msb|lsb] [cspin=N] [cspol=low|high]"));
        return true;
    }

    // Delegate to open() with same key=value parsing — cfg stores values,
    // but does NOT open the device.  We temporarily detach m_pSPI so that
    // open() only updates m_sSpiCfg without touching the hardware if already open.
    // Simpler: just parse the pairs directly here.

    std::vector<std::string> pairs;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, pairs);

    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() != 2) continue;

        bool ok = true;
        if (kv[0] == "clock") {
            ok = numeric::str2uint32(kv[1], m_sSpiCfg.clockHz);
        } else if (kv[0] == "mode") {
            uint8_t v = 0;
            ok = numeric::str2uint8(kv[1], v);
            if (ok && v <= 3) m_sSpiCfg.mode = static_cast<FT4232SPI::SpiMode>(v);
            else ok = false;
        } else if (kv[0] == "bitorder") {
            if      (kv[1] == "msb") m_sSpiCfg.bitOrder = FT4232SPI::BitOrder::MsbFirst;
            else if (kv[1] == "lsb") m_sSpiCfg.bitOrder = FT4232SPI::BitOrder::LsbFirst;
            else ok = false;
        } else if (kv[0] == "cspin") {
            ok = numeric::str2uint8(kv[1], m_sSpiCfg.csPin);
        } else if (kv[0] == "cspol") {
            if      (kv[1] == "low")  m_sSpiCfg.csPolarity = FT4232SPI::CsPolarity::ActiveLow;
            else if (kv[1] == "high") m_sSpiCfg.csPolarity = FT4232SPI::CsPolarity::ActiveHigh;
            else ok = false;
        } else {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown key:"); LOG_STRING(kv[0]));
            return false;
        }

        if (!ok) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Invalid value for:"); LOG_STRING(kv[0]));
            return false;
        }
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("SPI config updated (takes effect on next open)"));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CS                                      //
///////////////////////////////////////////////////////////////////

bool FT4232Plugin::m_handle_spi_cs(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: cs [en|dis]"));
        return true;
    }
    // CS is managed inside FT4232SPI per-transfer. For manual CS control
    // outside of a transfer, we use the raw MPSSE by calling tout_write
    // with a zero-length buffer tricks — instead we just log a note.
    // Most users drive CS via the write/read/wrrd/xfer commands.
    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Note: CS is automatically managed per transfer."));
    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Use write/read/wrrd/xfer for CS-guarded transfers."));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       WRITE                                   //
///////////////////////////////////////////////////////////////////

bool FT4232Plugin::m_handle_spi_write(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: write AABB..  (hex bytes, MOSI only)"));
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
    if (result.status != FT4232SPI::Status::SUCCESS) {
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

bool FT4232Plugin::m_handle_spi_read(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Use: read N  (read N bytes, clocks 0x00 on MOSI)"));
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
    if (result.status != FT4232SPI::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Read failed, bytes read:"); LOG_SIZET(result.bytes_read));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("MISO:"));
    hexutils::HexDump2(buf.data(), result.bytes_read);
    return true;
}

///////////////////////////////////////////////////////////////////
//                       WRRD                                    //
///////////////////////////////////////////////////////////////////

bool FT4232Plugin::m_spi_wrrd_cb(std::span<const uint8_t> req, size_t rdlen) const
{
    auto* p = m_spi();
    if (!p) return false;

    if (rdlen == 0 && req.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("wrrd: nothing to do"));
        return false;
    }

    if (rdlen == 0) {
        // Write-only: use tout_write (CS asserted for whole write)
        auto r = p->tout_write(0, req);
        return r.status == FT4232SPI::Status::SUCCESS;
    }

    if (req.empty()) {
        // Read-only: use tout_read
        std::vector<uint8_t> rxBuf(rdlen);
        ICommDriver::ReadOptions opts;
        opts.mode = ICommDriver::ReadMode::Exact;
        auto r = p->tout_read(0, rxBuf, opts);
        if (r.status != FT4232SPI::Status::SUCCESS) return false;
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Read:"));
        hexutils::HexDump2(rxBuf.data(), r.bytes_read);
        return true;
    }

    // Write + Read: full-duplex spi_transfer()
    // TX = req bytes followed by 0x00 padding for the read phase.
    // RX bytes [req.size() .. end] contain the device response.
    size_t totalLen = req.size() + rdlen;
    std::vector<uint8_t> txBuf(totalLen, 0x00u);
    std::copy(req.begin(), req.end(), txBuf.begin());
    std::vector<uint8_t> rxBuf(totalLen, 0x00u);

    auto r = p->spi_transfer(txBuf, rxBuf, 0u);
    if (r.status != FT4232SPI::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("spi_transfer failed, bytes exchanged:"); LOG_SIZET(r.bytes_xfered));
        return false;
    }

    // Print only the response portion (after the write bytes)
    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Read:"));
    hexutils::HexDump2(rxBuf.data() + req.size(), rdlen);
    return true;
}

bool FT4232Plugin::m_handle_spi_wrrd(const std::string& args) const
{
    return generic_write_read_data<FT4232Plugin>(
        this, args, &FT4232Plugin::m_spi_wrrd_cb);
}

bool FT4232Plugin::m_handle_spi_wrrdf(const std::string& args) const
{
    return generic_write_read_file<FT4232Plugin>(
        this, args, &FT4232Plugin::m_spi_wrrd_cb,
        m_sIniValues.strArtefactsPath);
}

///////////////////////////////////////////////////////////////////
//                       XFER (full-duplex)                      //
///////////////////////////////////////////////////////////////////

bool FT4232Plugin::m_handle_spi_xfer(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR;
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

    if (result.status != FT4232SPI::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("xfer failed, bytes exchanged:"); LOG_SIZET(result.bytes_xfered));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("MISO:"));
    hexutils::HexDump2(rxBuf.data(), result.bytes_xfered);
    return true;
}
