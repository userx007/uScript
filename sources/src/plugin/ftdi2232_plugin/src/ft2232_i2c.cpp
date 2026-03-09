/*
 * FT2232 Plugin – I2C protocol handlers
 *
 * Same structure as FT4232H I2C handlers.
 * Key difference: open() accepts variant=H|D forwarded to FT2232I2C::open().
 * FT2232D caps at ~400 kHz I2C (6 MHz base / ((1+6)*2) ≈ 428 kHz).
 *
 * Subcommands:
 *   open   [variant=H|D] [addr=0xNN] [clock=N] [channel=A|B] [device=N]
 *   close
 *   cfg    [variant=H|D] [addr=0xNN] [clock=N]
 *   write  AABB..     (START + addr+W + data + STOP)
 *   read   N          (repeated-START + addr+R + N bytes + STOP)
 *   wrrd   [hexdata][:rdlen]
 *   wrrdf  filename[:wrchunk][:rdchunk]
 *   scan              (probe 0x08..0x77; opens a temp driver per address)
 *   help
 */

#include "ft2232_plugin.hpp"
#include "ft2232_generic.hpp"

#include "uString.hpp"
#include "uNumeric.hpp"
#include "uHexlify.hpp"
#include "uHexdump.hpp"
#include "uLogger.hpp"

#include <iomanip>
#include <sstream>
#include <array>
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
#define LT_HDR   "FT2_I2C    |"
#define LOG_HDR  LOG_STRING(LT_HDR)

#define PROTOCOL_NAME "I2C"

///////////////////////////////////////////////////////////////////
//                       HELP                                    //
///////////////////////////////////////////////////////////////////

bool FT2232Plugin::m_handle_i2c_help(const std::string&) const
{
    return generic_module_list_commands<FT2232Plugin>(this, PROTOCOL_NAME);
}

///////////////////////////////////////////////////////////////////
//                       OPEN                                    //
///////////////////////////////////////////////////////////////////

bool FT2232Plugin::m_handle_i2c_open(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Use: open [variant=H|D] [addr=0xNN] [clock=N] [channel=A|B] [device=N]"));
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("  FT2232D: channel A only, practical max clock ~400 kHz"));
        return true;
    }

    std::vector<std::string> pairs;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, pairs);

    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() != 2) continue;

        bool ok = true;
        if (kv[0] == "variant") {
            ok = parseVariant(kv[1], m_sI2cCfg.variant);
        } else if (kv[0] == "addr" || kv[0] == "address") {
            ok = numeric::str2uint8(kv[1], m_sI2cCfg.address);
        } else if (kv[0] == "clock") {
            ok = numeric::str2uint32(kv[1], m_sI2cCfg.clockHz);
        } else if (kv[0] == "channel") {
            ok = parseChannel(kv[1], m_sI2cCfg.channel);
        } else if (kv[0] == "device") {
            uint8_t v = 0;
            ok = numeric::str2uint8(kv[1], v);
            if (ok) const_cast<FT2232Plugin*>(this)->m_sIniValues.u8DeviceIndex = v;
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

    if (!checkVariantSpeedLimit(m_sI2cCfg.variant, "I2C", m_sI2cCfg.clockHz)) return false;

    if (m_sI2cCfg.variant == FT2232Base::Variant::FT2232D &&
        m_sI2cCfg.channel != FT2232Base::Channel::A) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("FT2232D has MPSSE on channel A only"));
        return false;
    }

    if (m_pI2C) { m_pI2C->close(); m_pI2C.reset(); }

    m_pI2C = std::make_unique<FT2232I2C>();
    auto s = m_pI2C->open(m_sI2cCfg.address,
                          m_sI2cCfg.clockHz,
                          m_sI2cCfg.variant,
                          m_sI2cCfg.channel,
                          m_sIniValues.u8DeviceIndex);
    if (s != FT2232I2C::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("I2C open failed"));
        m_pI2C.reset();
        return false;
    }

    const char* varStr = (m_sI2cCfg.variant == FT2232Base::Variant::FT2232H) ? "H" : "D";
    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("I2C opened: variant="); LOG_STRING(varStr);
              LOG_STRING("addr=0x"); LOG_HEX8(m_sI2cCfg.address);
              LOG_STRING("clock="); LOG_UINT32(m_sI2cCfg.clockHz);
              LOG_STRING("ch="); LOG_UINT32(static_cast<uint8_t>(m_sI2cCfg.channel)));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CLOSE                                   //
///////////////////////////////////////////////////////////////////

bool FT2232Plugin::m_handle_i2c_close(const std::string&) const
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

bool FT2232Plugin::m_handle_i2c_cfg(const std::string& args) const
{
    if (args == "help" || args == "?") {
        const char* varStr = (m_sI2cCfg.variant == FT2232Base::Variant::FT2232H) ? "H" : "D";
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("I2C pending config:"));
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("  variant="); LOG_STRING(varStr);
                  LOG_STRING("addr=0x");   LOG_HEX8(m_sI2cCfg.address);
                  LOG_STRING("clock=");    LOG_UINT32(m_sI2cCfg.clockHz));
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Use: cfg [variant=H|D] [addr=0xNN] [clock=N]"));
        return true;
    }

    std::vector<std::string> pairs;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, pairs);

    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() != 2) continue;

        bool ok = true;
        if (kv[0] == "variant") {
            ok = parseVariant(kv[1], m_sI2cCfg.variant);
        } else if (kv[0] == "addr" || kv[0] == "address") {
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

bool FT2232Plugin::m_handle_i2c_write(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR;
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
    if (result.status != FT2232I2C::Status::SUCCESS) {
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

bool FT2232Plugin::m_handle_i2c_read(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Use: read N  (repeated-START + addr+R + N bytes + STOP)"));
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
    if (result.status != FT2232I2C::Status::SUCCESS) {
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

bool FT2232Plugin::m_i2c_wrrd_cb(std::span<const uint8_t> req, size_t rdlen) const
{
    auto* p = m_i2c();
    if (!p) return false;

    if (!req.empty()) {
        auto wr = p->tout_write(0, req);
        if (wr.status != FT2232I2C::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("wrrd: write phase failed"));
            return false;
        }
    }

    if (rdlen > 0) {
        std::vector<uint8_t> rxBuf(rdlen);
        ICommDriver::ReadOptions opts;
        opts.mode = ICommDriver::ReadMode::Exact;

        auto rd = p->tout_read(0, rxBuf, opts);
        if (rd.status != FT2232I2C::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("wrrd: read phase failed"));
            return false;
        }

        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Read:"));
        hexutils::HexDump2(rxBuf.data(), rd.bytes_read);
    }

    return true;
}

bool FT2232Plugin::m_handle_i2c_wrrd(const std::string& args) const
{
    return generic_write_read_data<FT2232Plugin>(
        this, args, &FT2232Plugin::m_i2c_wrrd_cb);
}

bool FT2232Plugin::m_handle_i2c_wrrdf(const std::string& args) const
{
    return generic_write_read_file<FT2232Plugin>(
        this, args, &FT2232Plugin::m_i2c_wrrd_cb,
        m_sIniValues.strArtefactsPath);
}

///////////////////////////////////////////////////////////////////
//                       SCAN                                    //
///////////////////////////////////////////////////////////////////

bool FT2232Plugin::m_handle_i2c_scan(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Probe I2C addresses 0x08..0x77"));
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Uses current variant/channel/clock/device settings"));
        return true;
    }

    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Scanning I2C bus..."));

    const uint8_t probe_byte = 0x00u;
    std::vector<uint8_t> found;

    for (uint8_t addr = 0x08u; addr <= 0x77u; ++addr) {
        FT2232I2C probe;
        auto s = probe.open(addr,
                            m_sI2cCfg.clockHz,
                            m_sI2cCfg.variant,
                            m_sI2cCfg.channel,
                            m_sIniValues.u8DeviceIndex);
        if (s != FT2232I2C::Status::SUCCESS) continue;

        const std::array<uint8_t, 1> buf{probe_byte};
        auto wr = probe.tout_write(200, buf);
        probe.close();

        if (wr.status == FT2232I2C::Status::SUCCESS)
            found.push_back(addr);
    }

    if (found.empty()) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("No devices found"));
    } else {
        for (uint8_t a : found) {
            std::ostringstream oss;
            oss << "Found device at 0x"
                << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                << static_cast<int>(a);
            LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING(oss.str()));
        }
    }

    return true;
}
