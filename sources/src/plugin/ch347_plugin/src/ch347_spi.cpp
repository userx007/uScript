/*
 * CH347 Plugin – SPI protocol handlers
 *
 * Wraps the CH347SPI driver behind the plugin command interface.
 *
 * Subcommands:
 *   open   [clock=N] [mode=0-3] [order=msb|lsb] [cs=cs1|cs2|none] [device=/dev/... (Linux) or 0 (Windows)]
 *   close
 *   cfg    [clock=N] [mode=0-3] [order=msb|lsb] [cs=cs1|cs2|none]
 *   cs     [en|dis]
 *   write  AABB..    (hex bytes, MOSI only)
 *   read   N         (full-duplex, clocks 0x00 N times)
 *   wrrd   [hexdata][:rdlen]
 *   wrrdf  filename[:wrchunk][:rdchunk]
 *   xfer   AABB..    (full-duplex WriteRead, prints MISO)
 *   script filename
 *   help
 */

#include "ch347_plugin.hpp"
#include "ch347_generic.hpp"

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

#define LT_HDR     "CH347_SPI   |"
#define LOG_HDR    LOG_STRING(LT_HDR)

#define PROTOCOL_NAME "SPI"

///////////////////////////////////////////////////////////////////
//                       HELP                                    //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_spi_help(const std::string&) const
{
    return generic_module_list_commands<CH347Plugin>(this, PROTOCOL_NAME);
}

///////////////////////////////////////////////////////////////////
//                       OPEN                                    //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_spi_open(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: open [clock=N] [mode=0-3] [order=msb|lsb]"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("          [cs=cs1|cs2|none] [device=/dev/... (Linux) or 0 (Windows)]"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("  clock: 468750 – 60000000 Hz"));
        return true;
    }

    std::string devPath = m_sIniValues.strDevicePath;
    if (!parseSpiParams(args, m_sSpiCfg, &devPath)) return false;
    const_cast<CH347Plugin*>(this)->m_sIniValues.strDevicePath = devPath;

    if (m_pSPI) { m_pSPI->close(); m_pSPI.reset(); }

    m_pSPI = std::make_unique<CH347SPI>();
    auto s = m_pSPI->open(m_sIniValues.strDevicePath, m_sSpiCfg.cfg);
    if (s != CH347SPI::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI open failed"));
        m_pSPI.reset();
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("SPI opened: device="); LOG_STRING(m_sIniValues.strDevicePath);
              LOG_STRING("clock="); LOG_UINT32(spiClockIndexToHz(m_sSpiCfg.cfg.iClock));
              LOG_STRING("mode=");  LOG_UINT32(m_sSpiCfg.cfg.iMode));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CLOSE                                   //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_spi_close(const std::string&) const
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

bool CH347Plugin::m_handle_spi_cfg(const std::string& args) const
{
    if (args == "help" || args == "?") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("SPI pending config:"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("  clock="); LOG_UINT32(spiClockIndexToHz(m_sSpiCfg.cfg.iClock));
                  LOG_STRING("mode="); LOG_UINT32(m_sSpiCfg.cfg.iMode);
                  LOG_STRING("order="); LOG_STRING(m_sSpiCfg.cfg.iByteOrder ? "msb" : "lsb"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: cfg [clock=N] [mode=0-3] [order=msb|lsb] [cs=cs1|cs2|none]"));
        return true;
    }

    if (!parseSpiParams(args, m_sSpiCfg)) return false;

    // Apply to open driver if present
    if (m_pSPI && m_pSPI->is_open() && m_sSpiCfg.cfgDirty) {
        m_pSPI->set_frequency(spiClockIndexToHz(m_sSpiCfg.cfg.iClock));
    }
    m_sSpiCfg.cfgDirty = false;

    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("SPI config updated"));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CS                                      //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_spi_cs(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: cs [en|dis]"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  CS is asserted/deasserted automatically per transfer."));
        return true;
    }
    auto* p = m_spi();
    if (!p) return false;

    if (args == "en" || args == "1") {
        p->change_cs(1);
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("CS asserted"));
    } else if (args == "dis" || args == "0") {
        p->change_cs(0);
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("CS deasserted"));
    } else {
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("CS is managed automatically per-transfer."));
    }
    return true;
}

///////////////////////////////////////////////////////////////////
//                       WRITE                                   //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_spi_write(const std::string& args) const
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
    if (result.status != CH347SPI::Status::SUCCESS) {
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

bool CH347Plugin::m_handle_spi_read(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: read N  (full-duplex, clocks 0x00 N times, prints MISO)"));
        return true;
    }
    auto* p = m_spi();
    if (!p) return false;

    size_t n = 0;
    if (!numeric::str2sizet(args, n) || n == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid byte count"));
        return false;
    }

    std::vector<uint8_t> buf(n, 0x00u);
    ICommDriver::ReadOptions opts;
    opts.mode = ICommDriver::ReadMode::Exact;

    auto result = p->tout_read(0, buf, opts);
    if (result.status != CH347SPI::Status::SUCCESS) {
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

bool CH347Plugin::m_spi_wrrd_cb(std::span<const uint8_t> req, size_t rdlen) const
{
    auto* p = m_spi();
    if (!p) return false;

    if (rdlen == 0 && req.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("wrrd: nothing to do"));
        return false;
    }

    if (rdlen == 0) {
        auto r = p->tout_write(0, req);
        return r.status == CH347SPI::Status::SUCCESS;
    }

    if (req.empty()) {
        std::vector<uint8_t> rxBuf(rdlen, 0x00u);
        ICommDriver::ReadOptions opts;
        opts.mode = ICommDriver::ReadMode::Exact;
        auto r = p->tout_read(0, rxBuf, opts);
        if (r.status != CH347SPI::Status::SUCCESS) return false;
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Read:"));
        hexutils::HexDump2(rxBuf.data(), r.bytes_read);
        return true;
    }

    // Write + full-duplex read using tout_xfer
    size_t totalLen = req.size() + rdlen;
    std::vector<uint8_t> buf(totalLen, 0x00u);
    std::copy(req.begin(), req.end(), buf.begin());

    auto r = p->tout_xfer(buf, m_sSpiCfg.xferOpts);
    if (r.status != CH347SPI::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("xfer failed, bytes exchanged:"); LOG_SIZET(r.bytes_read));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Read:"));
    hexutils::HexDump2(buf.data() + req.size(), rdlen);
    return true;
}

bool CH347Plugin::m_handle_spi_wrrd(const std::string& args) const
{
    return generic_write_read_data<CH347Plugin>(
        this, args, &CH347Plugin::m_spi_wrrd_cb);
}

bool CH347Plugin::m_handle_spi_wrrdf(const std::string& args) const
{
    return generic_write_read_file<CH347Plugin>(
        this, args, &CH347Plugin::m_spi_wrrd_cb,
        m_sIniValues.strArtefactsPath);
}

///////////////////////////////////////////////////////////////////
//                       XFER (full-duplex)                      //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_spi_xfer(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: xfer AABB..  (full-duplex WriteRead, MISO printed)"));
        return true;
    }
    auto* p = m_spi();
    if (!p) return false;

    std::vector<uint8_t> buf;
    if (!hexutils::stringUnhexlify(args, buf) || buf.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected at least 1 hex byte"));
        return false;
    }

    auto result = p->tout_xfer(buf, m_sSpiCfg.xferOpts);
    if (result.status != CH347SPI::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("xfer failed, bytes exchanged:"); LOG_SIZET(result.bytes_read));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("MISO:"));
    hexutils::HexDump2(buf.data(), result.bytes_read);
    return true;
}

///////////////////////////////////////////////////////////////////
//                       SCRIPT                                  //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_spi_script(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: script <filename>"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  Executes script from ARTEFACTS_PATH/filename"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  SPI must be open first"));
        return true;
    }

    auto* pSpi = m_spi();
    if (!pSpi) return false;

    const auto* ini = getAccessIniValues(*this);
    return generic_execute_script(
        pSpi,
        args,
        ini->strArtefactsPath,
        CH347_BULK_MAX_BYTES,
        ini->u32ReadTimeout,
        ini->u32ScriptDelay);
}
