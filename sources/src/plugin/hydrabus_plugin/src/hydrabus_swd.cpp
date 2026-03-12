/*
 * HydraBus Plugin – SWD protocol handlers
 *
 * Subcommands:
 *   init              (JTAG-to-SWD sequence + sync clocks)
 *   multidrop [addr]  (ADIv6 dormant-to-active, default addr=0)
 *   read_dp   addr    (read DP register, hex byte)
 *   write_dp  addr value (write DP register)
 *   read_ap   ap bank (read AP register)
 *   write_ap  ap bank value
 *   scan              (scan all 256 AP slots)
 *   abort     [flags] (default flags = 0x1F)
 *   help
 */

#include "hydrabus_plugin.hpp"
#include "hydrabus_generic.hpp"

#include "uNumeric.hpp"
#include "uHexlify.hpp"
#include "uLogger.hpp"

#include <iomanip>
#include <sstream>
#include <stdexcept>

///////////////////////////////////////////////////////////////////
//                       LOG DEFINES                             //
///////////////////////////////////////////////////////////////////

#ifdef  LT_HDR
#undef  LT_HDR
#endif
#ifdef  LOG_HDR
#undef  LOG_HDR
#endif
#define LT_HDR   "HB_SWD     |"
#define LOG_HDR  LOG_STRING(LT_HDR)

#define PROTOCOL_NAME "SWD"

///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_swd_help(const std::string&) const
{
    return generic_module_list_commands<HydrabusPlugin>(this, PROTOCOL_NAME);
}

///////////////////////////////////////////////////////////////////
//                       INIT                                    //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_swd_init(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Send JTAG-to-SWD sequence and sync clocks"));
        return true;
    }
    auto* p = m_swd();
    if (!p) return false;

    try {
        p->bus_init();
    } catch (const std::runtime_error& e) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("bus_init failed:"); LOG_STRING(e.what()));
        return false;
    }
    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("SWD bus initialised"));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       MULTIDROP                               //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_swd_multidrop(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: multidrop [addr]  (hex 32-bit DP address, default 0)"));
        return true;
    }
    auto* p = m_swd();
    if (!p) return false;

    uint32_t addr = 0;
    if (!args.empty()) {
        std::vector<uint8_t> buf;
        if (!hexutils::stringUnhexlify(args, buf) || buf.size() != 4) {
            // Try decimal
            if (!numeric::str2uint32(args, addr)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid addr"));
                return false;
            }
        } else {
            addr = static_cast<uint32_t>(buf[0])
                 | (static_cast<uint32_t>(buf[1]) << 8)
                 | (static_cast<uint32_t>(buf[2]) << 16)
                 | (static_cast<uint32_t>(buf[3]) << 24);
        }
    }

    try {
        p->multidrop_init(addr);
    } catch (const std::runtime_error& e) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("multidrop_init failed:"); LOG_STRING(e.what()));
        return false;
    }
    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Multidrop init done, addr="); LOG_UINT32(addr));
    return true;
}

///////////////////////////////////////////////////////////////////
//              HELPER: parse hex/decimal u32                    //
///////////////////////////////////////////////////////////////////

static bool parseU32(const std::string& s, uint32_t& out)
{
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        // hex with prefix
        std::vector<uint8_t> buf;
        if (!hexutils::stringUnhexlify(s.substr(2), buf)) return false;
        out = 0;
        for (uint8_t b : buf) out = (out << 8) | b;
        return true;
    }
    // Try as plain hex bytes first, then decimal
    std::vector<uint8_t> buf;
    if (hexutils::stringUnhexlify(s, buf)) {
        out = 0;
        for (uint8_t b : buf) out = (out << 8) | b;
        return true;
    }
    return numeric::str2uint32(s, out);
}

static bool parseU8(const std::string& s, uint8_t& out)
{
    uint32_t v = 0;
    if (!parseU32(s, v)) return false;
    out = static_cast<uint8_t>(v);
    return true;
}

///////////////////////////////////////////////////////////////////
//                       READ_DP                                 //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_swd_read_dp(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: read_dp addr  (e.g. read_dp 00)"));
        return true;
    }
    auto* p = m_swd();
    if (!p) return false;

    uint8_t addr = 0;
    if (!parseU8(args, addr)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid DP address"));
        return false;
    }

    try {
        uint32_t val = p->read_dp(addr);
        std::ostringstream oss;
        oss << "DP[0x" << std::hex << std::uppercase << (int)addr << "] = 0x"
            << std::setw(8) << std::setfill('0') << val;
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(oss.str()));
    } catch (const std::runtime_error& e) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(e.what()));
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////
//                       WRITE_DP                                //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_swd_write_dp(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: write_dp addr value  (e.g. write_dp 04 50000000)"));
        return true;
    }
    auto* p = m_swd();
    if (!p) return false;

    std::vector<std::string> parts;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, parts);
    if (parts.size() != 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected: write_dp addr value"));
        return false;
    }

    uint8_t addr = 0;  uint32_t val = 0;
    if (!parseU8(parts[0], addr) || !parseU32(parts[1], val)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid arguments"));
        return false;
    }

    try {
        p->write_dp(addr, val);
    } catch (const std::runtime_error& e) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(e.what()));
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////
//                       READ_AP                                 //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_swd_read_ap(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: read_ap ap_addr bank  (e.g. read_ap 00 FC)"));
        return true;
    }
    auto* p = m_swd();
    if (!p) return false;

    std::vector<std::string> parts;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, parts);
    if (parts.size() != 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected: read_ap ap_addr bank"));
        return false;
    }

    uint8_t ap = 0, bank = 0;
    if (!parseU8(parts[0], ap) || !parseU8(parts[1], bank)) return false;

    try {
        uint32_t val = p->read_ap(ap, bank);
        std::ostringstream oss;
        oss << "AP[" << (int)ap << "][0x" << std::hex << std::uppercase << (int)bank << "] = 0x"
            << std::setw(8) << std::setfill('0') << val;
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(oss.str()));
    } catch (const std::runtime_error& e) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(e.what()));
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////
//                       WRITE_AP                                //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_swd_write_ap(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: write_ap ap_addr bank value"));
        return true;
    }
    auto* p = m_swd();
    if (!p) return false;

    std::vector<std::string> parts;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, parts);
    if (parts.size() != 3) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected: write_ap ap_addr bank value"));
        return false;
    }

    uint8_t ap = 0, bank = 0;  uint32_t val = 0;
    if (!parseU8(parts[0], ap) || !parseU8(parts[1], bank) || !parseU32(parts[2], val))
        return false;

    try {
        p->write_ap(ap, bank, val);
    } catch (const std::runtime_error& e) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING(e.what()));
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////
//                       SCAN                                    //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_swd_scan(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Scan all 256 AP slots for valid IDR"));
        return true;
    }
    auto* p = m_swd();
    if (!p) return false;

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Scanning AP bus..."));
    try {
        p->scan_bus();
    } catch (const std::runtime_error& e) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Scan aborted:"); LOG_STRING(e.what()));
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////
//                       ABORT                                   //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_swd_abort(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: abort [flags]  (hex byte, default 1F)"));
        return true;
    }
    auto* p = m_swd();
    if (!p) return false;

    uint8_t flags = 0x1F;
    if (!args.empty()) {
        if (!parseU8(args, flags)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid flags"));
            return false;
        }
    }

    try {
        p->abort(flags);
    } catch (const std::runtime_error& e) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("ABORT failed:"); LOG_STRING(e.what()));
        return false;
    }
    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("ABORT sent, flags="); LOG_UINT8(flags));
    return true;
}
