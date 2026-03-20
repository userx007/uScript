/*
 * FT4232H Plugin – I2C protocol handlers
 *
 * Subcommands:
 *   open   [addr=0xNN] [clock=N] [channel=A|B] [device=N]
 *   close
 *   cfg    [addr=0xNN] [clock=N]  (stored, applied on next open)
 *   write  AABB..     (hex bytes — START + addr+W + data + STOP)
 *   read   N          (START + addr+R + N bytes + STOP)
 *   wrrd   [hexdata][:rdlen]
 *   wrrdf  filename[:wrchunk][:rdchunk]
 *   scan              (probe all 7-bit addresses — slow but thorough)
 *   help
 *
 * Notes on wrrd:
 *   FT4232I2C sends a STOP after tout_write and a repeated-START before
 *   tout_read.  This produces two separate I2C transactions:
 *     [S][addr+W][data][P]  [S][addr+R][data][P]
 *   Most I2C devices treat this identically to a single combined transaction.
 *
 * Notes on scan:
 *   For each candidate address, a temporary FT4232I2C instance is opened
 *   and a 1-byte write (0x00) is attempted.  An ACK indicates a device is
 *   present.  The scan iterates addresses 0x08..0x77.
 */

#include "ft4232_plugin.hpp"
#include "ft4232_generic.hpp"

#include "uString.hpp"
#include "uNumeric.hpp"
#include "uHexlify.hpp"
#include "uHexdump.hpp"
#include "uLogger.hpp"

#include <iomanip>
#include <sstream>
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
#define LT_HDR   "FT_I2C     |"
#define LOG_HDR  LOG_STRING(LT_HDR)

#define PROTOCOL_NAME "I2C"

///////////////////////////////////////////////////////////////////
//                       HELP                                    //
///////////////////////////////////////////////////////////////////

bool FT4232Plugin::m_handle_i2c_help(const std::string&) const
{
    return generic_module_list_commands<FT4232Plugin>(this, PROTOCOL_NAME);
}

///////////////////////////////////////////////////////////////////
//                       OPEN                                    //
///////////////////////////////////////////////////////////////////

bool FT4232Plugin::m_handle_i2c_open(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: open [addr=0xNN] [clock=N] [channel=A|B] [device=N]"));
        return true;
    }

    std::vector<std::string> pairs;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, pairs);

    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() != 2) continue;

        bool ok = true;
        if (kv[0] == "addr" || kv[0] == "address") {
            ok = numeric::str2uint8(kv[1], m_sI2cCfg.address);
        } else if (kv[0] == "clock") {
            ok = numeric::str2uint32(kv[1], m_sI2cCfg.clockHz);
        } else if (kv[0] == "channel") {
            ok = parseChannel(kv[1], m_sI2cCfg.channel);
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
                      LOG_STRING("Invalid value for:"); LOG_STRING(kv[0]));
            return false;
        }
    }

    if (m_pI2C) { m_pI2C->close(); m_pI2C.reset(); }

    m_pI2C = std::make_unique<FT4232I2C>();
    auto s = m_pI2C->open(m_sI2cCfg.address,
                          m_sI2cCfg.clockHz,
                          m_sI2cCfg.channel,
                          m_sIniValues.u8DeviceIndex);
    if (s != FT4232I2C::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("I2C open failed"));
        m_pI2C.reset();
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("I2C opened: addr=0x"); LOG_HEX8(m_sI2cCfg.address);
              LOG_STRING("clock="); LOG_UINT32(m_sI2cCfg.clockHz);
              LOG_STRING("ch="); LOG_UINT32(static_cast<uint8_t>(m_sI2cCfg.channel)));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CLOSE                                   //
///////////////////////////////////////////////////////////////////

bool FT4232Plugin::m_handle_i2c_close(const std::string&) const
{
    if (m_pI2C) {
        m_pI2C->close();
        m_pI2C.reset();
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("I2C closed"));
    } else {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("I2C was not open"));
    }
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CFG                                     //
///////////////////////////////////////////////////////////////////

bool FT4232Plugin::m_handle_i2c_cfg(const std::string& args) const
{
    if (args == "help" || args == "?") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("I2C pending config:"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("  addr=0x");  LOG_HEX8(m_sI2cCfg.address);
                  LOG_STRING("clock=");     LOG_UINT32(m_sI2cCfg.clockHz));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: cfg [addr=0xNN] [clock=N]"));
        return true;
    }

    std::vector<std::string> pairs;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, pairs);

    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() != 2) continue;

        bool ok = true;
        if (kv[0] == "addr" || kv[0] == "address") {
            ok = numeric::str2uint8(kv[1], m_sI2cCfg.address);
        } else if (kv[0] == "clock") {
            ok = numeric::str2uint32(kv[1], m_sI2cCfg.clockHz);
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
              LOG_STRING("I2C config updated (takes effect on next open)"));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       WRITE                                   //
///////////////////////////////////////////////////////////////////

bool FT4232Plugin::m_handle_i2c_write(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: write AABB..  (hex bytes; START + addr+W + data + STOP)"));
        return true;
    }

    auto* p = m_i2c();
    if (!p) return false;

    std::vector<uint8_t> data;
    if (!hexutils::stringUnhexlify(args, data) || data.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected at least 1 hex byte"));
        return false;
    }

    auto result = p->tout_write(0, data);
    if (result.status != FT4232I2C::Status::SUCCESS) {
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

bool FT4232Plugin::m_handle_i2c_read(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: read N  (reads N bytes; ACKs all but the last)"));
        return true;
    }

    auto* p = m_i2c();
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
    if (result.status != FT4232I2C::Status::SUCCESS) {
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

bool FT4232Plugin::m_i2c_wrrd_cb(std::span<const uint8_t> req, size_t rdlen) const
{
    auto* p = m_i2c();
    if (!p) return false;

    // Write phase (if any)
    if (!req.empty()) {
        auto wr = p->tout_write(0, req);
        if (wr.status != FT4232I2C::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("wrrd: write phase failed"));
            return false;
        }
    }

    // Read phase (if any)
    if (rdlen > 0) {
        std::vector<uint8_t> rxBuf(rdlen);
        ICommDriver::ReadOptions opts;
        opts.mode = ICommDriver::ReadMode::Exact;

        auto rd = p->tout_read(0, rxBuf, opts);
        if (rd.status != FT4232I2C::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("wrrd: read phase failed"));
            return false;
        }

        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Read:"));
        hexutils::HexDump2(rxBuf.data(), rd.bytes_read);
    }

    return true;
}

bool FT4232Plugin::m_handle_i2c_wrrd(const std::string& args) const
{
    return generic_write_read_data<FT4232Plugin>(
        this, args, &FT4232Plugin::m_i2c_wrrd_cb);
}

bool FT4232Plugin::m_handle_i2c_wrrdf(const std::string& args) const
{
    return generic_write_read_file<FT4232Plugin>(
        this, args, &FT4232Plugin::m_i2c_wrrd_cb,
        m_sIniValues.strArtefactsPath);
}

///////////////////////////////////////////////////////////////////
//                       SCAN                                    //
///////////////////////////////////////////////////////////////////

bool FT4232Plugin::m_handle_i2c_scan(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Probe I2C addresses 0x08..0x77 (uses current channel/device/clock)"));
        return true;
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Scanning I2C bus..."));

    std::vector<uint8_t> found;
    const uint8_t probe_byte = 0x00u;

    // Each probe: open a temporary FT4232I2C at the candidate address,
    // attempt a 1-byte write, check for ACK (SUCCESS), then close.
    for (uint8_t addr = 0x08u; addr <= 0x77u; ++addr) {
        FT4232I2C probe;
        auto s = probe.open(addr,
                            m_sI2cCfg.clockHz,
                            m_sI2cCfg.channel,
                            m_sIniValues.u8DeviceIndex);
        if (s != FT4232I2C::Status::SUCCESS) continue;

        const std::array<uint8_t, 1> buf{probe_byte};
        auto wr = probe.tout_write(200, buf);
        probe.close();

        if (wr.status == FT4232I2C::Status::SUCCESS) {
            found.push_back(addr);
        }
    }

    if (found.empty()) {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("No devices found"));
    } else {
        for (uint8_t a : found) {
            std::ostringstream oss;
            oss << "Found device at 0x"
                << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                << static_cast<int>(a);
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(oss.str()));
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////
//                       SCRIPT                                  //
///////////////////////////////////////////////////////////////////

bool FT4232Plugin::m_handle_i2c_script(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: <scriptname>"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  Executes script from ARTEFACTS_PATH/scriptname"));
        return true;
    }
    auto* pDrv = m_i2c(); if (!pDrv) return false;
    const auto* ini = getAccessIniValues(*this);
    return generic_execute_script(
            pDrv, 
            args, ini->strArtefactsPath,
            FT_BULK_MAX_BYTES,
            ini->u32ReadTimeout,
            ini->u32ScriptDelay,
            m_bIsEnabled);
}
