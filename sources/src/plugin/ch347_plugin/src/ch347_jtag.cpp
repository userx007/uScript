/*
 * CH347 Plugin – JTAG protocol handlers
 *
 * Wraps the CH347JTAG driver behind the plugin command interface.
 *
 * Subcommands:
 *   open   [rate=0-5] [device=/dev/...]
 *   close
 *   cfg    [rate=0-5]
 *   reset  [trst]                   — TAP logic reset (TRST asserts via pin)
 *   write  [ir|dr] AABB..           — shift bytes into IR or DR
 *   read   [ir|dr] N                — shift N bytes out of IR or DR
 *   wrrd   [ir|dr] HEXDATA:rdlen    — combined shift-in/out
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
#include <sstream>
#include <iomanip>

///////////////////////////////////////////////////////////////////
//                       LOG DEFINES                             //
///////////////////////////////////////////////////////////////////

#ifdef  LT_HDR
#undef  LT_HDR
#endif
#ifdef  LOG_HDR
#undef  LOG_HDR
#endif
#define LT_HDR   "CH347_JTAG |"
#define LOG_HDR  LOG_STRING(LT_HDR)

#define PROTOCOL_NAME "JTAG"

///////////////////////////////////////////////////////////////////
//             Internal helpers                                  //
///////////////////////////////////////////////////////////////////

static bool parseJtagReg(const std::string& s, JtagRegister& out)
{
    if (s == "ir" || s == "IR") { out = JtagRegister::IR; return true; }
    if (s == "dr" || s == "DR") { out = JtagRegister::DR; return true; }
    return false;
}

///////////////////////////////////////////////////////////////////
//                       HELP                                    //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_jtag_help(const std::string&) const
{
    return generic_module_list_commands<CH347Plugin>(this, PROTOCOL_NAME);
}

///////////////////////////////////////////////////////////////////
//                       OPEN                                    //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_jtag_open(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Use: open [rate=0-5] [device=/dev/...]"));
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("  rate: 0=slowest, 5=fastest (hardware-dependent frequency)"));
        return true;
    }

    std::string devPath = m_sIniValues.strDevicePath;
    std::vector<std::string> pairs;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, pairs);

    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() != 2) continue;
        bool ok = true;
        if      (kv[0] == "rate")   ok = numeric::str2uint8(kv[1], m_sJtagCfg.clockRate);
        else if (kv[0] == "device") devPath = kv[1];
        else {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown key:"); LOG_STRING(kv[0]));
            return false;
        }
        if (!ok) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid value for:"); LOG_STRING(kv[0]));
            return false;
        }
    }

    if (m_sJtagCfg.clockRate > CH347JTAG::JTAG_MAX_CLOCK_RATE) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Clock rate must be 0-5, got:"); LOG_UINT32(m_sJtagCfg.clockRate));
        return false;
    }

    const_cast<CH347Plugin*>(this)->m_sIniValues.strDevicePath = devPath;

    if (m_pJTAG) { m_pJTAG->close(); m_pJTAG.reset(); }

    m_pJTAG = std::make_unique<CH347JTAG>();
    auto s = m_pJTAG->open(devPath, m_sJtagCfg.clockRate);
    if (s != CH347JTAG::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("JTAG open failed"));
        m_pJTAG.reset();
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("JTAG opened: device="); LOG_STRING(devPath);
              LOG_STRING("rate="); LOG_UINT32(m_sJtagCfg.clockRate));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CLOSE                                   //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_jtag_close(const std::string&) const
{
    if (m_pJTAG) {
        m_pJTAG->close();
        m_pJTAG.reset();
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("JTAG closed"));
    } else {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("JTAG was not open"));
    }
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CFG                                     //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_jtag_cfg(const std::string& args) const
{
    if (args == "help" || args == "?") {
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("JTAG config: rate="); LOG_UINT32(m_sJtagCfg.clockRate));
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Use: cfg rate=0-5"));
        return true;
    }

    std::vector<std::string> pairs;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, pairs);
    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() != 2) continue;
        if (kv[0] == "rate") {
            if (!numeric::str2uint8(kv[1], m_sJtagCfg.clockRate)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid rate")); return false;
            }
        }
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("JTAG config updated: rate="); LOG_UINT32(m_sJtagCfg.clockRate));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       RESET                                   //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_jtag_reset(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: reset [trst]"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("  (no args) = TAP logic reset via TMS"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("  trst      = assert TRST pin"));
        return true;
    }

    auto* p = m_jtag();
    if (!p) return false;

    if (args == "trst") {
        auto s = p->tap_reset_trst(true);
        if (s != CH347JTAG::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("TRST reset failed"));
            return false;
        }
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("TRST asserted"));
    } else {
        auto s = p->tap_reset();
        if (s != CH347JTAG::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("TAP reset failed"));
            return false;
        }
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("TAP reset OK"));
    }
    return true;
}

///////////////////////////////////////////////////////////////////
//                       WRITE                                   //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_jtag_write(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: write [ir|dr] AABB..  (hex bytes)"));
        return true;
    }

    auto* p = m_jtag();
    if (!p) return false;

    std::vector<std::string> parts;
    ustring::splitAtFirst(args, CHAR_SEPARATOR_SPACE, parts);
    if (parts.size() < 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Use: write [ir|dr] HEXDATA"));
        return false;
    }

    JtagRegister reg = m_sJtagCfg.lastReg;
    if (parseJtagReg(parts[0], reg)) {
        m_sJtagCfg.lastReg = reg;
    } else {
        // First token is hex data, not ir/dr — reassemble
        parts[1] = args;
    }

    std::vector<uint8_t> data;
    if (!hexutils::stringUnhexlify(parts[1], data) || data.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected at least 1 hex byte"));
        return false;
    }

    auto s = p->write_register(reg, data);
    if (s != CH347JTAG::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("JTAG write failed"));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Wrote"); LOG_SIZET(data.size());
              LOG_STRING("bytes to"); LOG_STRING(reg == JtagRegister::IR ? "IR" : "DR"));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       READ                                    //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_jtag_read(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: read [ir|dr] N"));
        return true;
    }

    auto* p = m_jtag();
    if (!p) return false;

    std::vector<std::string> parts;
    ustring::splitAtFirst(args, CHAR_SEPARATOR_SPACE, parts);
    if (parts.size() < 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Use: read [ir|dr] N"));
        return false;
    }

    JtagRegister reg = m_sJtagCfg.lastReg;
    size_t n = 0;

    if (parseJtagReg(parts[0], reg)) {
        m_sJtagCfg.lastReg = reg;
        if (!numeric::str2sizet(parts[1], n) || n == 0) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid byte count")); return false;
        }
    } else {
        if (!numeric::str2sizet(parts[0], n) || n == 0) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid byte count")); return false;
        }
    }

    std::vector<uint8_t> buf(n);
    auto s = p->read_register(reg, buf);
    if (s != CH347JTAG::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("JTAG read failed"));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Read from"); LOG_STRING(reg == JtagRegister::IR ? "IR" : "DR"));
    hexutils::HexDump2(buf.data(), n);
    return true;
}

///////////////////////////////////////////////////////////////////
//                       WRRD                                    //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_jtag_wrrd(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Use: wrrd [ir|dr] HEXDATA:rdlen"));
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("  e.g.  wrrd dr DEADBEEF:4"));
        return true;
    }

    auto* p = m_jtag();
    if (!p) return false;

    std::vector<std::string> parts;
    ustring::splitAtFirst(args, CHAR_SEPARATOR_SPACE, parts);

    JtagRegister reg = m_sJtagCfg.lastReg;
    std::string transferSpec;

    if (parts.size() >= 2 && parseJtagReg(parts[0], reg)) {
        m_sJtagCfg.lastReg = reg;
        transferSpec = parts[1];
    } else {
        transferSpec = args;
    }

    // Parse HEXDATA:rdlen
    std::vector<std::string> txRx;
    ustring::tokenize(transferSpec, CHAR_SEPARATOR_COLON, txRx);
    if (txRx.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Use: wrrd [ir|dr] HEXDATA:rdlen"));
        return false;
    }

    std::vector<uint8_t> writeBuf;
    if (!hexutils::stringUnhexlify(txRx[0], writeBuf)) return false;

    size_t rdlen = writeBuf.size();
    if (txRx.size() >= 2 && !numeric::str2sizet(txRx[1], rdlen)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid rdlen")); return false;
    }

    std::vector<uint8_t> readBuf(rdlen);
    auto result = p->write_read(reg, writeBuf, readBuf);
    if (result.status != CH347JTAG::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("wrrd failed"));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Read from"); LOG_STRING(reg == JtagRegister::IR ? "IR" : "DR"));
    hexutils::HexDump2(readBuf.data(), result.bytes_read);
    return true;
}

///////////////////////////////////////////////////////////////////
//                       SCRIPT                                  //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_jtag_script(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: script <filename>"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("  Executes script from ARTEFACTS_PATH/filename"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("  JTAG must be open first"));
        return true;
    }

    auto* pJtag = m_jtag();
    if (!pJtag) return false;

    const auto* ini = getAccessIniValues(*this);
    return generic_execute_script(
        pJtag,
        args,
        ini->strArtefactsPath,
        CH347_BULK_MAX_BYTES,
        ini->u32ReadTimeout,
        ini->u32ScriptDelay);
}
