/*
 * HydraBus Plugin – NFC Reader protocol handlers
 *
 * Subcommands:
 *   mode      [14443a|15693]
 *   rf        [on|off]
 *   write     AABB.. [crc]        (send bytes, optional CRC append)
 *   write_bits HEXBYTE N          (send N bits of hex byte)
 *   aux       N [in|out|pp] [0|1]
 *   help
 */

#include "hydrabus_plugin.hpp"
#include "hydrabus_generic.hpp"

#include "uNumeric.hpp"
#include "uHexlify.hpp"
#include "uHexdump.hpp"
#include "uLogger.hpp"

///////////////////////////////////////////////////////////////////
//                       LOG DEFINES                             //
///////////////////////////////////////////////////////////////////

#ifdef  LT_HDR
#undef  LT_HDR
#endif
#ifdef  LOG_HDR
#undef  LOG_HDR
#endif
#define LT_HDR   "HB_NFC     |"
#define LOG_HDR  LOG_STRING(LT_HDR)

#define PROTOCOL_NAME "NFC"

///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_nfc_help(const std::string&) const
{
    return generic_module_list_commands<HydrabusPlugin>(this, PROTOCOL_NAME);
}

bool HydrabusPlugin::m_handle_nfc_mode(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: mode [14443a|15693]"));
        return true;
    }
    auto* p = m_nfc();
    if (!p) return false;

    if      (args == "14443a") { p->set_mode(HydraHAL::NFC::Mode::ISO_14443A); }
    else if (args == "15693")  { p->set_mode(HydraHAL::NFC::Mode::ISO_15693);  }
    else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown mode:"); LOG_STRING(args));
        return false;
    }
    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("NFC mode set:"); LOG_STRING(args));
    return true;
}

bool HydrabusPlugin::m_handle_nfc_rf(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: rf [on|off]"));
        return true;
    }
    auto* p = m_nfc();
    if (!p) return false;

    if      (args == "on")  { p->set_rf(true);  }
    else if (args == "off") { p->set_rf(false); }
    else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected on or off"));
        return false;
    }
    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("RF field:"); LOG_STRING(args));
    return true;
}

// write AABB.. [crc]
bool HydrabusPlugin::m_handle_nfc_write(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: write AABB.. [crc]  (append CRC if 'crc' present)"));
        return true;
    }
    auto* p = m_nfc();
    if (!p) return false;

    // Split off optional trailing "crc" keyword
    std::vector<std::string> parts;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, parts);
    if (parts.empty()) return false;

    bool appendCrc = (parts.size() >= 2 && parts.back() == "crc");
    std::string hexStr = parts[0];

    std::vector<uint8_t> data;
    if (!hexutils::stringUnhexlify(hexStr, data) || data.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid hex data"));
        return false;
    }

    auto resp = p->write(data, appendCrc);
    if (!resp.empty()) {
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Response:"));
        hexutils::HexDump2(resp.data(), resp.size());
    }
    return true;
}

// write_bits HEXBYTE N
bool HydrabusPlugin::m_handle_nfc_write_bits(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: write_bits HEXBYTE N  (e.g. write_bits 26 7)"));
        return true;
    }
    auto* p = m_nfc();
    if (!p) return false;

    std::vector<std::string> parts;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, parts);
    if (parts.size() != 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected: write_bits HEXBYTE N"));
        return false;
    }

    std::vector<uint8_t> buf;
    if (!hexutils::stringUnhexlify(parts[0], buf) || buf.size() != 1) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected 1 hex byte"));
        return false;
    }
    uint8_t nbits = 0;
    if (!numeric::str2uint8(parts[1], nbits) || nbits == 0 || nbits > 7) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("N must be 1-7"));
        return false;
    }

    auto resp = p->write_bits(buf[0], nbits);
    if (!resp.empty()) {
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Response:"));
        hexutils::HexDump2(resp.data(), resp.size());
    }
    return true;
}

bool HydrabusPlugin::m_handle_nfc_aux(const std::string& args) const
{
    return m_handle_aux_common(args, m_nfc());
}
