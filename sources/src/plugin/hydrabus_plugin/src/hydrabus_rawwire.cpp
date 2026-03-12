/*
 * HydraBus Plugin – RawWire protocol handlers
 *
 * Subcommands:
 *   cfg     polarity=[0|1] wires=[2|3] gpio=[0|1]
 *   speed   [5kHz|50kHz|100kHz|1MHz]
 *   sda     [0|1]           (drive SDA pin)
 *   clk     [0|1|tick]      (drive CLK pin or send one tick)
 *   bit     N data          (send N bits of hex byte data)
 *   ticks   N               (send N clock ticks, 1-16)
 *   write   AABB..          (bulk write, MISO printed)
 *   read    N               (read N bytes)
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

///////////////////////////////////////////////////////////////////
//                       LOG DEFINES                             //
///////////////////////////////////////////////////////////////////

#ifdef  LT_HDR
#undef  LT_HDR
#endif
#ifdef  LOG_HDR
#undef  LOG_HDR
#endif
#define LT_HDR   "HB_RAWWIRE |"
#define LOG_HDR  LOG_STRING(LT_HDR)

#define PROTOCOL_NAME "RAWWIRE"

///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_rawwire_help(const std::string&) const
{
    return generic_module_list_commands<HydrabusPlugin>(this, PROTOCOL_NAME);
}

///////////////////////////////////////////////////////////////////
//                       CFG                                     //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_rawwire_cfg(const std::string& args) const
{
    auto* p = m_rawwire();

    if (args == "help" || args == "?") {
        if (p) {
            LOG_PRINT(LOG_EMPTY,
                      LOG_STRING("polarity="); LOG_INT(p->get_polarity());
                      LOG_STRING("wires=");    LOG_INT(p->get_wires());
                      LOG_STRING("gpio=");     LOG_INT(p->get_gpio_mode()));
        }
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: cfg polarity=[0|1] wires=[2|3] gpio=[0|1]"));
        return true;
    }
    if (!p) return false;

    std::vector<std::string> pairs;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, pairs);

    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() != 2) continue;

        uint8_t v = 0;
        if (!numeric::str2uint8(kv[1], v)) return false;

        if      (kv[0] == "polarity") { if (!p->set_polarity(v))   return false; }
        else if (kv[0] == "wires")    { if (!p->set_wires(v))      return false; }
        else if (kv[0] == "gpio")     { if (!p->set_gpio_mode(v))  return false; }
        else {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown key:"); LOG_STRING(kv[0]));
            return false;
        }
    }
    return true;
}

///////////////////////////////////////////////////////////////////
//                       SPEED                                   //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_rawwire_speed(const std::string& args) const
{
    return generic_module_set_speed<HydrabusPlugin>(this, PROTOCOL_NAME, args);
}

///////////////////////////////////////////////////////////////////
//                       SDA                                     //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_rawwire_sda(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: sda [0|1]"));
        return true;
    }
    auto* p = m_rawwire();
    if (!p) return false;

    uint8_t v = 0;
    if (!numeric::str2uint8(args, v)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected 0 or 1"));
        return false;
    }
    return p->set_sda(v);
}

///////////////////////////////////////////////////////////////////
//                       CLK                                     //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_rawwire_clk(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: clk [0|1|tick]"));
        return true;
    }
    auto* p = m_rawwire();
    if (!p) return false;

    if (args == "tick") return p->clock();

    uint8_t v = 0;
    if (!numeric::str2uint8(args, v)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected 0, 1 or tick"));
        return false;
    }
    return p->set_clk(v);
}

///////////////////////////////////////////////////////////////////
//                       BIT                                     //
///////////////////////////////////////////////////////////////////

// bit N HEXBYTE  – send N bits from HEXBYTE (e.g. "bit 7 A5")
bool HydrabusPlugin::m_handle_rawwire_bit(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: bit N HEXBYTE  (e.g. bit 7 A5 – send 7 bits of 0xA5)"));
        return true;
    }
    auto* p = m_rawwire();
    if (!p) return false;

    std::vector<std::string> parts;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, parts);
    if (parts.size() != 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected: bit N HEXBYTE"));
        return false;
    }

    size_t n = 0;
    if (!numeric::str2sizet(parts[0], n) || n == 0 || n > 8) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("N must be 1-8"));
        return false;
    }

    std::vector<uint8_t> databuf;
    if (!hexutils::stringUnhexlify(parts[1], databuf) || databuf.size() != 1) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected 1 hex byte"));
        return false;
    }

    return p->write_bits(databuf, n);
}

///////////////////////////////////////////////////////////////////
//                       TICKS                                   //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_rawwire_ticks(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: ticks N  (1-16)"));
        return true;
    }
    auto* p = m_rawwire();
    if (!p) return false;

    size_t n = 0;
    if (!numeric::str2sizet(args, n) || n == 0 || n > 16) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("N must be 1-16"));
        return false;
    }
    return p->bulk_ticks(n);
}

///////////////////////////////////////////////////////////////////
//                       WRITE                                   //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_rawwire_write(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: write AABB..  (hex, 1-16 bytes)"));
        return true;
    }
    auto* p = m_rawwire();
    if (!p) return false;

    std::vector<uint8_t> data;
    if (!hexutils::stringUnhexlify(args, data) || data.empty() || data.size() > 16) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected 1-16 hex bytes"));
        return false;
    }

    auto miso = p->bulk_write(data);
    if (!miso.empty()) {
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("MISO:"));
        hexutils::HexDump2(miso.data(), miso.size());
    }
    return true;
}

///////////////////////////////////////////////////////////////////
//                       READ                                    //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_rawwire_read(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: read N"));
        return true;
    }
    auto* p = m_rawwire();
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
//                       AUX                                     //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_rawwire_aux(const std::string& args) const
{
    return m_handle_aux_common(args, m_rawwire());
}
