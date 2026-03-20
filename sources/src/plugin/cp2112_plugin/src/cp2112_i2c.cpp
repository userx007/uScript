/*
 * CP2112 Plugin – I2C protocol handlers
 *
 * The CP2112 exposes a single I2C/SMBus master port.  Each open() targets
 * one 7-bit slave address; re-open with a different address to address a
 * different device, or use scan to discover what is on the bus.
 *
 * Subcommands:
 *   open   [addr=0xNN] [clock=N] [device=N]
 *   close
 *   cfg    [addr=0xNN] [clock=N]
 *   write  AABB..
 *   read   N
 *   wrrd   [hexdata][:rdlen]
 *   wrrdf  filename[:wrchunk][:rdchunk]
 *   scan
 *   help
 */

#include "cp2112_plugin.hpp"
#include "cp2112_generic.hpp"

#include "uString.hpp"
#include "uNumeric.hpp"
#include "uHexlify.hpp"
#include "uHexdump.hpp"
#include "uLogger.hpp"

#include <iomanip>
#include <sstream>
#include <array>
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

#define LT_HDR     "CP2112_I2C  |"
#define LOG_HDR    LOG_STRING(LT_HDR)

#define PROTOCOL_NAME "I2C"

///////////////////////////////////////////////////////////////////
//                       HELP                                    //
///////////////////////////////////////////////////////////////////

bool CP2112Plugin::m_handle_i2c_help(const std::string&) const
{
    return generic_module_list_commands<CP2112Plugin>(this, PROTOCOL_NAME);
}

///////////////////////////////////////////////////////////////////
//                       OPEN                                    //
///////////////////////////////////////////////////////////////////

bool CP2112Plugin::m_handle_i2c_open(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: open [addr=0xNN] [clock=N] [device=N]"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("  addr   : 7-bit I2C slave address (default 0x50)"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("  clock  : I2C clock in Hz (default 100000)"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("  device : zero-based index if multiple CP2112 attached (default 0)"));
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
        } else if (kv[0] == "device") {
            ok = numeric::str2uint8(kv[1], const_cast<CP2112Plugin*>(this)->m_sIniValues.u8DeviceIndex);
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

    m_pI2C = std::make_unique<CP2112>();
    auto s = m_pI2C->open(m_sI2cCfg.address,
                          m_sI2cCfg.clockHz,
                          m_sIniValues.u8DeviceIndex);
    if (s != CP2112::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("I2C open failed"));
        m_pI2C.reset();
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("I2C opened: addr=0x"); LOG_HEX8(m_sI2cCfg.address);
              LOG_STRING("clock="); LOG_UINT32(m_sI2cCfg.clockHz);
              LOG_STRING("device="); LOG_UINT32(m_sIniValues.u8DeviceIndex));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CLOSE                                   //
///////////////////////////////////////////////////////////////////

bool CP2112Plugin::m_handle_i2c_close(const std::string&) const
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

bool CP2112Plugin::m_handle_i2c_cfg(const std::string& args) const
{
    if (args == "help" || args == "?") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("I2C pending config:"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("  addr=0x"); LOG_HEX8(m_sI2cCfg.address);
                  LOG_STRING("clock=");    LOG_UINT32(m_sI2cCfg.clockHz));
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
              LOG_STRING("I2C config updated (takes effect on next open):"));
    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("  addr=0x"); LOG_HEX8(m_sI2cCfg.address);
              LOG_STRING("clock=");    LOG_UINT32(m_sI2cCfg.clockHz));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       WRITE                                   //
///////////////////////////////////////////////////////////////////

bool CP2112Plugin::m_handle_i2c_write(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: write AABB..  (hex bytes; I2C START + addr+W + data + STOP)"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("  Max payload: 512 bytes, auto-chunked at 61-byte HID boundaries"));
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
    if (result.status != CP2112::Status::SUCCESS) {
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

bool CP2112Plugin::m_handle_i2c_read(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: read N  (reads N bytes from the current slave address; max 512)"));
        return true;
    }

    auto* p = m_i2c();
    if (!p) return false;

    size_t n = 0;
    if (!numeric::str2sizet(args, n) || n == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid byte count (must be 1..512)"));
        return false;
    }
    if (n > CP2112::MAX_I2C_READ_LEN) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Byte count exceeds maximum (512):"); LOG_SIZET(n));
        return false;
    }

    std::vector<uint8_t> buf(n);
    ICommDriver::ReadOptions opts;
    opts.mode = ICommDriver::ReadMode::Exact;

    auto result = p->tout_read(0, buf, opts);
    if (result.status != CP2112::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Read failed, bytes read:"); LOG_SIZET(result.bytes_read));
        return false;
    }

    hexutils::HexDump2(buf.data(), result.bytes_read);
    return true;
}

///////////////////////////////////////////////////////////////////
//                       WRRD / WRRDF                           //
///////////////////////////////////////////////////////////////////

bool CP2112Plugin::m_i2c_wrrd_cb(std::span<const uint8_t> req, size_t rdlen) const
{
    auto* p = m_i2c();
    if (!p) return false;

    // Write phase
    if (!req.empty()) {
        auto wr = p->tout_write(0, req);
        if (wr.status != CP2112::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("wrrd: write phase failed"));
            return false;
        }
    }

    // Read phase
    if (rdlen > 0) {
        if (rdlen > CP2112::MAX_I2C_READ_LEN) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("wrrd: read length exceeds 512:"); LOG_SIZET(rdlen));
            return false;
        }
        std::vector<uint8_t> rxBuf(rdlen);
        ICommDriver::ReadOptions opts;
        opts.mode = ICommDriver::ReadMode::Exact;

        auto rd = p->tout_read(0, rxBuf, opts);
        if (rd.status != CP2112::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("wrrd: read phase failed"));
            return false;
        }

        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Read:"));
        hexutils::HexDump2(rxBuf.data(), rd.bytes_read);
    }

    return true;
}

bool CP2112Plugin::m_handle_i2c_wrrd(const std::string& args) const
{
    return generic_write_read_data<CP2112Plugin>(
        this, args, &CP2112Plugin::m_i2c_wrrd_cb);
}

bool CP2112Plugin::m_handle_i2c_wrrdf(const std::string& args) const
{
    return generic_write_read_file<CP2112Plugin>(
        this, args, &CP2112Plugin::m_i2c_wrrd_cb,
        m_sIniValues.strArtefactsPath);
}

///////////////////////////////////////////////////////////////////
//                       SCAN                                    //
///////////////////////////////////////////////////////////////////

bool CP2112Plugin::m_handle_i2c_scan(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Probe I2C addresses 0x08..0x77 using a zero-byte write"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Uses current clock and device index; no open required"));
        return true;
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Scanning I2C bus..."));

    std::vector<uint8_t> found;
    const std::array<uint8_t, 1> probe_byte{0x00u};

    for (uint8_t addr = 0x08u; addr <= 0x77u; ++addr) {
        CP2112 probe;
        auto s = probe.open(addr,
                            m_sI2cCfg.clockHz,
                            m_sIniValues.u8DeviceIndex);
        if (s != CP2112::Status::SUCCESS) {
            // Failed to open device at all — report and abort
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Failed to open CP2112 HID handle during scan"));
            return false;
        }

        auto wr = probe.tout_write(200, probe_byte);
        probe.close();

        if (wr.status == CP2112::Status::SUCCESS) {
            found.push_back(addr);
        }
    }

    if (found.empty()) {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("No devices found"));
    } else {
        for (uint8_t a : found) {
            std::ostringstream oss;
            oss << "Found device at 0x"
                << std::hex << std::uppercase
                << std::setw(2) << std::setfill('0')
                << static_cast<int>(a);
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(oss.str()));
        }
    }

    return true;
}

/* ============================================================
   m_handle_i2c_script
   Execute a CommScriptClient script through the open I2C driver.
   The I2C port must be opened first ("CP2112.I2C open ...").

   Usage:  CP2112.I2C script <filename>
           CP2112.I2C script help
============================================================ */
bool CP2112Plugin::m_handle_i2c_script(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: script <filename>"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  Executes script from ARTEFACTS_PATH/filename"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  I2C must be open first (CP2112.I2C open ...)"));
        return true;
    }

    auto* pI2c = m_i2c();
    if (!pI2c) return false;

    const auto* ini = getAccessIniValues(*this);
    return generic_execute_script(
            pI2c,
            args,
            ini->strArtefactsPath,
            CP2112_BULK_MAX_BYTES,
            ini->u32ReadTimeout,
            ini->u32ScriptDelay,
            m_bIsEnabled);
}
