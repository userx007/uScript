/*
 * FT245 Plugin – FIFO protocol handlers
 *
 * Implements all FIFO subcommands via the FT245Sync driver.
 * The FT245 exposes a simple bulk byte-stream interface — there is no
 * serial framing, no clock divisor, and no chip-select concept.
 *
 * Subcommands:
 *   open   [variant=BM|R] [mode=async|sync] [device=N]
 *   close
 *   cfg    [variant=BM|R] [mode=async|sync]
 *   write  AABB..     (hex bytes — written into TX FIFO)
 *   read   N          (read N bytes from RX FIFO)
 *   wrrd   [hexdata][:rdlen]
 *   wrrdf  filename[:wrchunk][:rdchunk]
 *   flush             (purge RX + TX FIFOs without closing)
 *   script SCRIPTNAME (CommScriptClient — FIFO must be open)
 *   help
 */

#include "ft245_plugin.hpp"
#include "ft245_generic.hpp"

#include "uString.hpp"
#include "uNumeric.hpp"
#include "uHexlify.hpp"
#include "uHexdump.hpp"
#include "uLogger.hpp"

#include <vector>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef  LT_HDR
#undef  LT_HDR
#endif
#ifdef  LOG_HDR
#undef  LOG_HDR
#endif
#define LT_HDR   "FT245_FIFO |"
#define LOG_HDR  LOG_STRING(LT_HDR)

#define PROTOCOL_NAME "FIFO"

///////////////////////////////////////////////////////////////////
//                       HELP                                    //
///////////////////////////////////////////////////////////////////

bool FT245Plugin::m_handle_fifo_help(const std::string&) const
{
    return generic_module_list_commands<FT245Plugin>(this, PROTOCOL_NAME);
}

///////////////////////////////////////////////////////////////////
//                       OPEN                                    //
///////////////////////////////////////////////////////////////////

bool FT245Plugin::m_handle_fifo_open(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: open [variant=BM|R] [mode=async|sync] [device=N]"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("  variant : BM=FT245BM/RL (default) | R=FT245R"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("  mode    : async (default, both variants) | sync (FT245BM only)"));
        return true;
    }

    uint8_t devIdx = m_sIniValues.u8DeviceIndex;
    if (!parseFifoParams(args, m_sFifoCfg, &devIdx)) return false;
    const_cast<FT245Plugin*>(this)->m_sIniValues.u8DeviceIndex = devIdx;

    // FT245R does not support sync mode
    if (m_sFifoCfg.variant == FT245Base::Variant::FT245R &&
        m_sFifoCfg.fifoMode == FT245Base::FifoMode::Sync) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("FT245R does not support sync FIFO mode — use mode=async"));
        return false;
    }

    if (m_pFIFO) { m_pFIFO->close(); m_pFIFO.reset(); }

    FT245Sync::SyncConfig cfg;
    cfg.variant  = m_sFifoCfg.variant;
    cfg.fifoMode = m_sFifoCfg.fifoMode;

    m_pFIFO = std::make_unique<FT245Sync>();
    auto s = m_pFIFO->open(cfg, m_sIniValues.u8DeviceIndex);
    if (s != FT245Sync::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FIFO open failed"));
        m_pFIFO.reset();
        return false;
    }

    const char* varStr  = (cfg.variant  == FT245Base::Variant::FT245BM) ? "BM" : "R";
    const char* modeStr = (cfg.fifoMode == FT245Base::FifoMode::Async)  ? "async" : "sync";
    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("FIFO opened: variant="); LOG_STRING(varStr);
              LOG_STRING("mode="); LOG_STRING(modeStr);
              LOG_STRING("device="); LOG_UINT32(m_sIniValues.u8DeviceIndex));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CLOSE                                   //
///////////////////////////////////////////////////////////////////

bool FT245Plugin::m_handle_fifo_close(const std::string&) const
{
    if (m_pFIFO) {
        m_pFIFO->close();
        m_pFIFO.reset();
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("FIFO closed"));
    } else {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("FIFO was not open"));
    }
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CFG                                     //
///////////////////////////////////////////////////////////////////

bool FT245Plugin::m_handle_fifo_cfg(const std::string& args) const
{
    if (args == "help" || args == "?") {
        const char* varStr  = (m_sFifoCfg.variant  == FT245Base::Variant::FT245BM) ? "BM" : "R";
        const char* modeStr = (m_sFifoCfg.fifoMode == FT245Base::FifoMode::Async)  ? "async" : "sync";
        LOG_PRINT(LOG_EMPTY, LOG_STRING("FIFO pending config:"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("  variant="); LOG_STRING(varStr);
                  LOG_STRING("mode=");      LOG_STRING(modeStr));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: cfg [variant=BM|R] [mode=async|sync]"));
        return true;
    }

    if (!parseFifoParams(args, m_sFifoCfg)) return false;

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("FIFO config updated (takes effect on next open)"));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       WRITE                                   //
///////////////////////////////////////////////////////////////////

bool FT245Plugin::m_handle_fifo_write(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: write AABB..  (hex bytes written into TX FIFO)"));
        return true;
    }
    auto* p = m_fifo();
    if (!p) return false;

    std::vector<uint8_t> data;
    if (!hexutils::stringUnhexlify(args, data) || data.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected at least 1 hex byte"));
        return false;
    }

    auto result = p->tout_write(0, data);
    if (result.status != FT245Sync::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Write failed, bytes written:"); LOG_SIZET(result.bytes_written));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Wrote"); LOG_SIZET(result.bytes_written); LOG_STRING("bytes OK"));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       READ                                    //
///////////////////////////////////////////////////////////////////

bool FT245Plugin::m_handle_fifo_read(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: read N  (read N bytes from RX FIFO)"));
        return true;
    }
    auto* p = m_fifo();
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
    if (result.status != FT245Sync::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Read failed, bytes read:"); LOG_SIZET(result.bytes_read));
        return false;
    }

    hexutils::HexDump2(buf.data(), result.bytes_read);
    return true;
}

///////////////////////////////////////////////////////////////////
//                       WRRD / WRRDF                            //
///////////////////////////////////////////////////////////////////

bool FT245Plugin::m_fifo_wrrd_cb(std::span<const uint8_t> req, size_t rdlen) const
{
    auto* p = m_fifo();
    if (!p) return false;

    if (!req.empty()) {
        auto wr = p->tout_write(0, req);
        if (wr.status != FT245Sync::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("wrrd: write phase failed"));
            return false;
        }
    }

    if (rdlen > 0) {
        std::vector<uint8_t> rxBuf(rdlen);
        ICommDriver::ReadOptions opts;
        opts.mode = ICommDriver::ReadMode::Exact;

        auto rd = p->tout_read(0, rxBuf, opts);
        if (rd.status != FT245Sync::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("wrrd: read phase failed"));
            return false;
        }

        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Read:"));
        hexutils::HexDump2(rxBuf.data(), rd.bytes_read);
    }

    return true;
}

bool FT245Plugin::m_handle_fifo_wrrd(const std::string& args) const
{
    return generic_write_read_data<FT245Plugin>(
        this, args, &FT245Plugin::m_fifo_wrrd_cb);
}

bool FT245Plugin::m_handle_fifo_wrrdf(const std::string& args) const
{
    return generic_write_read_file<FT245Plugin>(
        this, args, &FT245Plugin::m_fifo_wrrd_cb,
        m_sIniValues.strArtefactsPath);
}

///////////////////////////////////////////////////////////////////
//                       FLUSH                                   //
///////////////////////////////////////////////////////////////////

bool FT245Plugin::m_handle_fifo_flush(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: flush  (purge RX + TX FIFOs without closing)"));
        return true;
    }

    auto* p = m_fifo();
    if (!p) return false;

    auto s = p->flush();
    if (s != FT245Sync::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FIFO flush failed"));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("FIFO flushed"));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       SCRIPT                                  //
///////////////////////////////////////////////////////////////////

bool FT245Plugin::m_handle_fifo_script(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: script <filename>"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  Executes script from ARTEFACTS_PATH/filename"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  FIFO must be open first (FT245.FIFO open ...)"));
        return true;
    }

    auto* pFifo = m_fifo();
    if (!pFifo) return false;

    const auto* ini = getAccessIniValues(*this);
    return generic_execute_script(
        pFifo,
        args,
        ini->strArtefactsPath,
        FT_BULK_MAX_BYTES,
        ini->u32ReadTimeout,
        ini->u32ScriptDelay);
}
