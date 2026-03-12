/*
 * HydraBus Plugin – OneWire protocol handlers
 *
 * Subcommands:
 *   cfg     pullup=[0|1]
 *   reset               (1-Wire reset pulse)
 *   write   AABB..      (hex, 1-16 bytes)
 *   read    N           (read N bytes)
 *   swio    init|read addr|write addr value
 *   aux     N [in|out|pp] [0|1]
 *   help
 */

#include "hydrabus_plugin.hpp"
#include "hydrabus_generic.hpp"

#include "uString.hpp"
#include "uNumeric.hpp"
#include "uHexlify.hpp"
#include "uHexdump.hpp"
#include "uLogger.hpp"

#include <iomanip>
#include <sstream>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef  LT_HDR
#undef  LT_HDR
#endif
#ifdef  LOG_HDR
#undef  LOG_HDR
#endif
#define LT_HDR   "HB_ONEWIRE |"
#define LOG_HDR  LOG_STRING(LT_HDR)

#define PROTOCOL_NAME "ONEWIRE"

///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_onewire_help(const std::string&) const
{
    return generic_module_list_commands<HydrabusPlugin>(this, PROTOCOL_NAME);
}

///////////////////////////////////////////////////////////////////
//                       CFG                                     //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_onewire_cfg(const std::string& args) const
{
    auto* p = m_onewire();

    if (args == "help" || args == "?") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: cfg pullup=[0|1]"));
        return true;
    }
    if (!p) return false;

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("pullup="); LOG_UINT8(p->get_pullup() ? 1 : 0));

    std::vector<std::string> pairs;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, pairs);

    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() != 2) continue;

        if (kv[0] == "pullup") {
            uint8_t v = 0;
            if (!numeric::str2uint8(kv[1], v)) return false;
            if (!p->set_pullup(v != 0)) return false;
        } else {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown key:"); LOG_STRING(kv[0]));
            return false;
        }
    }
    return true;
}

///////////////////////////////////////////////////////////////////
//                       RESET                                   //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_onewire_reset(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Send 1-Wire reset pulse"));
        return true;
    }
    auto* p = m_onewire();
    if (!p) return false;

    bool ok = p->reset();
    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Reset sent, result:"); LOG_UINT8(ok ? 1 : 0));
    return ok;
}

///////////////////////////////////////////////////////////////////
//                       WRITE                                   //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_onewire_write(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: write AABB..  (hex, 1-16 bytes)"));
        return true;
    }
    auto* p = m_onewire();
    if (!p) return false;

    std::vector<uint8_t> data;
    if (!hexutils::stringUnhexlify(args, data) || data.empty() || data.size() > 16) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected 1-16 hex bytes"));
        return false;
    }
    return p->bulk_write(data);
}

///////////////////////////////////////////////////////////////////
//                       READ                                    //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_onewire_read(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: read N"));
        return true;
    }
    auto* p = m_onewire();
    if (!p) return false;

    size_t n = 0;
    if (!numeric::str2sizet(args, n) || n == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid byte count"));
        return false;
    }
    auto data = p->read(n);
    hexutils::HexDump2(data.data(), data.size());
    return true;
}

///////////////////////////////////////////////////////////////////
//                       SWIO                                    //
//  swio init
//  swio read  ADDR         (hex byte)
//  swio write ADDR VALUE   (hex byte, hex 32-bit LE)
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_onewire_swio(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use:"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  swio init"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  swio read  ADDR         (e.g. swio read 00)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  swio write ADDR VALUE   (e.g. swio write 04 50000000)"));
        return true;
    }
    auto* p = m_onewire();
    if (!p) return false;

    std::vector<std::string> parts;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, parts);

    if (parts.empty()) return false;

    if (parts[0] == "init") {
        return p->swio_init();
    }
    else if (parts[0] == "read" && parts.size() == 2) {
        std::vector<uint8_t> addrBuf;
        if (!hexutils::stringUnhexlify(parts[1], addrBuf) || addrBuf.size() != 1) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("addr must be 1 hex byte"));
            return false;
        }
        uint32_t val = p->swio_read_reg(addrBuf[0]);
        std::ostringstream oss;
        oss << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << val;
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("SWIO reg"); LOG_STRING(parts[1]);
                  LOG_STRING("="); LOG_STRING(oss.str()));
        return true;
    }
    else if (parts[0] == "write" && parts.size() == 3) {
        std::vector<uint8_t> addrBuf, valBuf;
        if (!hexutils::stringUnhexlify(parts[1], addrBuf) || addrBuf.size() != 1) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("addr must be 1 hex byte"));
            return false;
        }
        if (!hexutils::stringUnhexlify(parts[2], valBuf) || valBuf.size() != 4) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("value must be 4 hex bytes (LE)"));
            return false;
        }
        uint32_t v = static_cast<uint32_t>(valBuf[0])
                   | (static_cast<uint32_t>(valBuf[1]) << 8)
                   | (static_cast<uint32_t>(valBuf[2]) << 16)
                   | (static_cast<uint32_t>(valBuf[3]) << 24);
        return p->swio_write_reg(addrBuf[0], v);
    }

    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid swio syntax – use: swio help"));
    return false;
}

///////////////////////////////////////////////////////////////////
//                       AUX                                     //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_onewire_aux(const std::string& args) const
{
    return m_handle_aux_common(args, m_onewire());
}
