/*
 * HydraBus Plugin – SPI protocol handlers
 *
 * Subcommands:
 *   cfg   polarity=[0|1] phase=[0|1] device=[0|1]
 *   cs    [en|dis]
 *   speed [320kHz|650kHz|1MHz|2MHz|5MHz|10MHz|21MHz|42MHz]
 *   write AABB..        (hex, 1-16 bytes, full-duplex — MISO printed)
 *   read  N             (read N bytes by clocking 0xFF)
 *   wrrd  [hexdata][:rdlen]
 *   wrrdf filename[:wrchunk][:rdchunk]
 *   aux   N [in|out|pp] [0|1]
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
#define LT_HDR   "HB_SPI     |"
#define LOG_HDR  LOG_STRING(LT_HDR)

#define PROTOCOL_NAME "SPI"

///////////////////////////////////////////////////////////////////
//                       HELP                                    //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_spi_help(const std::string&) const
{
    return generic_module_list_commands<HydrabusPlugin>(this, PROTOCOL_NAME);
}

///////////////////////////////////////////////////////////////////
//                       CFG                                     //
///////////////////////////////////////////////////////////////////

// cfg polarity=0 phase=1 device=1
bool HydrabusPlugin::m_handle_spi_cfg(const std::string& args) const
{
    auto* p = m_spi();

    if (args == "help" || args == "?") {
        if (p) {
            LOG_PRINT(LOG_FIXED, LOG_HDR;
                      LOG_STRING("polarity="); LOG_INT(p->get_polarity());
                      LOG_STRING("phase=");    LOG_INT(p->get_phase());
                      LOG_STRING("device=");   LOG_INT(p->get_device()));
        }
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Use: cfg polarity=[0|1] phase=[0|1] device=[0|1]"));
        return true;
    }

    if (!p) return false;

    // Parse key=value pairs (space-separated)
    std::vector<std::string> pairs;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, pairs);

    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() != 2) continue;

        uint8_t v = 0;
        if (!numeric::str2uint8(kv[1], v)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid value:"); LOG_STRING(kv[1]));
            return false;
        }

        if      (kv[0] == "polarity") { if (!p->set_polarity(v)) return false; }
        else if (kv[0] == "phase")    { if (!p->set_phase(v))    return false; }
        else if (kv[0] == "device")   { if (!p->set_device(v))   return false; }
        else {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown key:"); LOG_STRING(kv[0]));
            return false;
        }
    }
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CS                                      //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_spi_cs(const std::string& args) const
{
    auto* p = m_spi();
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: cs [en|dis]"));
        return true;
    }
    if (!p) return false;

    if      (args == "en")  return p->set_cs(0);  // active-low
    else if (args == "dis") return p->set_cs(1);
    else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown cs arg:"); LOG_STRING(args));
        return false;
    }
}

///////////////////////////////////////////////////////////////////
//                       SPEED                                   //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_spi_speed(const std::string& args) const
{
    return generic_module_set_speed<HydrabusPlugin>(this, PROTOCOL_NAME, args);
}

///////////////////////////////////////////////////////////////////
//                       WRITE (bulk, full-duplex)               //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_spi_write(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: write AABB..  (hex, 1-16 bytes)"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Full-duplex: MISO bytes are printed"));
        return true;
    }
    auto* p = m_spi();
    if (!p) return false;

    std::vector<uint8_t> data;
    if (!hexutils::stringUnhexlify(args, data) || data.empty() || data.size() > 16) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected 1-16 hex bytes"));
        return false;
    }

    auto miso = p->bulk_write(data);
    if (miso.empty() && !data.empty()) return false;

    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("MISO:"));
    hexutils::HexDump2(miso.data(), miso.size());
    return true;
}

///////////////////////////////////////////////////////////////////
//                       READ                                    //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_spi_read(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: read N  (read N bytes, clocks 0xFF)"));
        return true;
    }
    auto* p = m_spi();
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
//                       WRRD                                    //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_spi_wrrd_cb(std::span<const uint8_t> req, size_t rdlen) const
{
    auto* p = m_spi();
    if (!p) return false;

    auto result = p->write_read(req, rdlen);
    if (!result) return false;

    if (!result->empty()) {
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Read:"));
        hexutils::HexDump2(result->data(), result->size());
    }
    return true;
}

bool HydrabusPlugin::m_handle_spi_wrrd(const std::string& args) const
{
    return generic_write_read_data<HydrabusPlugin>(
        this, args, &HydrabusPlugin::m_spi_wrrd_cb);
}

bool HydrabusPlugin::m_handle_spi_wrrdf(const std::string& args) const
{
    return generic_write_read_file<HydrabusPlugin>(
        this, args, &HydrabusPlugin::m_spi_wrrd_cb,
        m_sIniValues.strArtefactsPath);
}

///////////////////////////////////////////////////////////////////
//                       AUX                                     //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_spi_aux(const std::string& args) const
{
    return m_handle_aux_common(args, m_spi());
}
