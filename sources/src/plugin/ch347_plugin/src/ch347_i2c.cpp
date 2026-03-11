/*
 * CH347 Plugin – I2C protocol handlers
 *
 * Wraps the CH347I2C driver behind the plugin command interface.
 * The CH347 I2C host uses CH347StreamI2C which performs a combined
 * START / Write / Repeated-START / Read / STOP in one call.
 *
 * Subcommands:
 *   open   [speed=100kHz|400kHz|...] [addr=0xNN] [device=/dev/... (Linux) or 0 (Windows)]
 *   close
 *   cfg    [speed=...] [addr=0xNN]
 *   write  AABB..     (START + addr+W + data + STOP)
 *   read   N          (START + addr+R + N bytes + STOP)
 *   wrrd   [hexdata][:rdlen]
 *   wrrdf  filename[:wrchunk][:rdchunk]
 *   scan              (probe 0x08..0x77 for ACK)
 *   eeprom read|write TYPE ADDR HEXDATA|N
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
#define LT_HDR   "CH347_I2C  |"
#define LOG_HDR  LOG_STRING(LT_HDR)

#define PROTOCOL_NAME "I2C"

///////////////////////////////////////////////////////////////////
//                       HELP                                    //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_i2c_help(const std::string&) const
{
    return generic_module_list_commands<CH347Plugin>(this, PROTOCOL_NAME);
}

///////////////////////////////////////////////////////////////////
//                       OPEN                                    //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_i2c_open(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Use: open [speed=20kHz|50kHz|100kHz|200kHz|400kHz|750kHz|1MHz]"));
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("          [addr=0xNN] [device=/dev/... (Linux) or 0 (Windows)]"));
        return true;
    }

    std::string devPath = m_sIniValues.strDevicePath;
    if (!parseI2cParams(args, m_sI2cCfg, &devPath)) return false;
    const_cast<CH347Plugin*>(this)->m_sIniValues.strDevicePath = devPath;

    if (m_pI2C) { m_pI2C->close(); m_pI2C.reset(); }

    m_pI2C = std::make_unique<CH347I2C>();
    auto s = m_pI2C->open(m_sIniValues.strDevicePath, m_sI2cCfg.speed);
    if (s != CH347I2C::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("I2C open failed"));
        m_pI2C.reset();
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("I2C opened: device="); LOG_STRING(m_sIniValues.strDevicePath);
              LOG_STRING("addr=0x"); LOG_HEX8(m_sI2cCfg.address));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CLOSE                                   //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_i2c_close(const std::string&) const
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

bool CH347Plugin::m_handle_i2c_cfg(const std::string& args) const
{
    if (args == "help" || args == "?") {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("I2C pending config:"));
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("  speed="); LOG_UINT32(static_cast<int>(m_sI2cCfg.speed));
                  LOG_STRING("addr=0x"); LOG_HEX8(m_sI2cCfg.address));
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Use: cfg [speed=20kHz|100kHz|400kHz|750kHz|1MHz] [addr=0xNN]"));
        return true;
    }

    if (!parseI2cParams(args, m_sI2cCfg)) return false;

    // Apply to live driver if open
    if (m_pI2C && m_pI2C->is_open()) {
        m_pI2C->set_speed(m_sI2cCfg.speed);
    }

    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("I2C config updated"));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       WRITE                                   //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_i2c_write(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Use: write AABB.."));
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("  buffer[0] = (devAddr << 1) | 0  (WRITE bit)"));
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("  buffer[1..] = register + payload"));
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
    if (result.status != CH347I2C::Status::SUCCESS) {
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

bool CH347Plugin::m_handle_i2c_read(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Use: read N  (reads N bytes from addr configured in open/cfg)"));
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
    I2cReadOptions opts;
    opts.devAddr  = m_sI2cCfg.address;
    opts.writeLen = 0;

    auto result = p->tout_read_i2c(buf, opts);
    if (result.status != CH347I2C::Status::SUCCESS) {
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

bool CH347Plugin::m_i2c_wrrd_cb(std::span<const uint8_t> req, size_t rdlen) const
{
    auto* p = m_i2c();
    if (!p) return false;

    if (!req.empty()) {
        auto wr = p->tout_write(0, req);
        if (wr.status != CH347I2C::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("wrrd: write phase failed"));
            return false;
        }
    }

    if (rdlen > 0) {
        std::vector<uint8_t> rxBuf(rdlen);
        I2cReadOptions opts;
        opts.devAddr  = m_sI2cCfg.address;
        opts.writeLen = 0;

        auto rd = p->tout_read_i2c(rxBuf, opts);
        if (rd.status != CH347I2C::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("wrrd: read phase failed"));
            return false;
        }

        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Read:"));
        hexutils::HexDump2(rxBuf.data(), rd.bytes_read);
    }

    return true;
}

bool CH347Plugin::m_handle_i2c_wrrd(const std::string& args) const
{
    return generic_write_read_data<CH347Plugin>(
        this, args, &CH347Plugin::m_i2c_wrrd_cb);
}

bool CH347Plugin::m_handle_i2c_wrrdf(const std::string& args) const
{
    return generic_write_read_file<CH347Plugin>(
        this, args, &CH347Plugin::m_i2c_wrrd_cb,
        m_sIniValues.strArtefactsPath);
}

///////////////////////////////////////////////////////////////////
//                       SCAN                                    //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_i2c_scan(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Probe I2C addresses 0x08..0x77 for ACK"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Device must be open first"));
        return true;
    }

    auto* p = m_i2c();
    if (!p) {
        // Open a temporary driver for the scan
        CH347I2C probe;
        auto s = probe.open(m_sIniValues.strDevicePath, m_sI2cCfg.speed);
        if (s != CH347I2C::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Cannot open device for scan"));
            return false;
        }
        p = &probe;
    }

    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Scanning I2C bus 0x08..0x77 ..."));
    std::vector<uint8_t> found;

    for (uint8_t addr = 0x08u; addr <= 0x77u; ++addr) {
        // A zero-length write to (addr << 1)|0 probes for ACK
        const uint8_t probe_buf[1] = { static_cast<uint8_t>(addr << 1) };
        auto wr = p->tout_write(200, std::span<const uint8_t>(probe_buf, 1));
        if (wr.status == CH347I2C::Status::SUCCESS)
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

///////////////////////////////////////////////////////////////////
//                       EEPROM                                  //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_i2c_eeprom(const std::string& args) const
{
    if (args == "help" || args.empty()) {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: eeprom read  TYPE ADDR N"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("     eeprom write TYPE ADDR HEXDATA"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("  TYPE: 0=24C01 1=24C02 2=24C04 3=24C08 4=24C16"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("        5=24C32 6=24C64 7=24C128 8=24C256"));
        return true;
    }

    auto* p = m_i2c();
    if (!p) return false;

    std::vector<std::string> parts;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, parts);
    if (parts.size() < 3) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Use: eeprom [read|write] TYPE ADDR [N|HEXDATA]"));
        return false;
    }

    const std::string& op = parts[0];
    uint8_t  typeIdx = 0;
    uint32_t addr    = 0;

    if (!numeric::str2uint8(parts[1], typeIdx) || typeIdx > 12) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid EEPROM type (0-12):"); LOG_STRING(parts[1]));
        return false;
    }
    if (!numeric::str2uint32(parts[2], addr)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid address:"); LOG_STRING(parts[2]));
        return false;
    }

    auto eeType = static_cast<EEPROM_TYPE>(typeIdx);

    if (op == "read") {
        if (parts.size() < 4) { LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing read count")); return false; }
        size_t n = 0;
        if (!numeric::str2sizet(parts[3], n) || n == 0) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid byte count")); return false;
        }
        std::vector<uint8_t> buf(n);
        auto s = p->read_eeprom(eeType, static_cast<int>(addr), buf);
        if (s != CH347I2C::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("EEPROM read failed"));
            return false;
        }
        hexutils::HexDump2(buf.data(), n);
        return true;
    }

    if (op == "write") {
        if (parts.size() < 4) { LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing write data")); return false; }
        std::vector<uint8_t> data;
        if (!hexutils::stringUnhexlify(parts[3], data) || data.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid hex data")); return false;
        }
        auto s = p->write_eeprom(eeType, static_cast<int>(addr), data);
        if (s != CH347I2C::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("EEPROM write failed"));
            return false;
        }
        LOG_PRINT(LOG_INFO, LOG_HDR;
                  LOG_STRING("EEPROM wrote"); LOG_SIZET(data.size()); LOG_STRING("bytes OK"));
        return true;
    }

    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown op (use read|write):"); LOG_STRING(op));
    return false;
}

///////////////////////////////////////////////////////////////////
//                       SCRIPT                                  //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_i2c_script(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: script <filename>"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("  Executes script from ARTEFACTS_PATH/filename"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("  I2C must be open first"));
        return true;
    }

    auto* pI2c = m_i2c();
    if (!pI2c) return false;

    const auto* ini = getAccessIniValues(*this);
    return generic_execute_script(
        pI2c,
        args,
        ini->strArtefactsPath,
        CH347_BULK_MAX_BYTES,
        ini->u32ReadTimeout,
        ini->u32ScriptDelay);
}
