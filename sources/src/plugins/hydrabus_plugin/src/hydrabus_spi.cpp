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
#include "SPI.hpp"
#include "hydrabus_generic.hpp"
#include "hydrabus_plugin.hpp"
#include "uHexdump.hpp"
#include "uHexlify.hpp"
#include "uLogger.hpp"
#include "uNumeric.hpp"
#include "uSharedConfig.hpp"
#include "uString.hpp"

#include <optional>
#include <span>
#include <stddef.h>
#include <stdint.h>
#include <stop_token>
#include <string>
#include <vector>

/////////////////////////////////////////////////////////////////////////////////
//                            LOG DEFINITIONS                                  //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
#undef LT_HDR
#endif
#ifdef LOG_HDR
#undef LOG_HDR
#endif
#define LT_HDR        "HB_SPI     |"
#define LOG_HDR       LOG_STRING(LT_HDR)

#define PROTOCOL_NAME "SPI"

///////////////////////////////////////////////////////////////////
//                       HELP                                    //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_spi_help(const std::string &, std::stop_token /*st*/) const
{
    return generic_module_list_commands<HydrabusPlugin>(this, PROTOCOL_NAME);
}

///////////////////////////////////////////////////////////////////
//                       CFG                                     //
///////////////////////////////////////////////////////////////////

// cfg polarity=0 phase=1 device=1
bool HydrabusPlugin::m_handle_spi_cfg(const std::string &strArgs, std::stop_token /*st*/) const
{
    auto *p = m_spi();

    if (strArgs == "help" || strArgs == "?") {
        if (p) {
            LOG_PRINT(LOG_EMPTY,
                      LOG_STRING("polarity=");
                      LOG_INT(p->get_polarity());
                      LOG_STRING("phase="); LOG_INT(p->get_phase());
                      LOG_STRING("device="); LOG_INT(p->get_device()));
        }
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: cfg polarity=[0|1] phase=[0|1] device=[0|1]"));
        return true;
    }

    if (!p) {
        return false;
    }

    // Parse key=value pairs (space-separated)
    std::vector<std::string> pairs;
    ustring::tokenize(strArgs, CHAR_SEPARATOR_SPACE, pairs);

    for (const auto &pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() != 2) {
            continue;
        }

        uint8_t v = 0;
        if (!numeric::str2uint8(kv[1], v)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid value:"); LOG_STRING(kv[1]));
            return false;
        }

        if (kv[0] == "polarity") {
            if (!p->set_polarity(v)) {
                return false;
            }
        } else if (kv[0] == "phase") {
            if (!p->set_phase(v)) {
                return false;
            }
        } else if (kv[0] == "device") {
            if (!p->set_device(v)) {
                return false;
            }
        } else {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown key:"); LOG_STRING(kv[0]));
            return false;
        }
    }
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CS                                      //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_spi_cs(const std::string &strArgs, std::stop_token /*st*/) const
{
    auto *p = m_spi();
    if (strArgs == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: cs [en|dis]"));
        return true;
    }
    if (!p) {
        return false;
    }

    if (strArgs == "en") {
        return p->set_cs(0); // active-low
    } else if (strArgs == "dis") {
        return p->set_cs(1);
    } else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown cs arg:"); LOG_STRING(strArgs));
        return false;
    }
}

///////////////////////////////////////////////////////////////////
//                       SPEED                                   //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_spi_speed(const std::string &strArgs, std::stop_token /*st*/) const
{
    return generic_module_set_speed<HydrabusPlugin>(this, PROTOCOL_NAME, strArgs);
}

///////////////////////////////////////////////////////////////////
//                       WRITE (bulk, full-duplex)               //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_spi_write(const std::string &strArgs, std::stop_token st) const
{
    if (strArgs == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: write AABB..  (hex, 1-16 bytes)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Full-duplex: MISO bytes are printed"));
        return true;
    }
    auto *p = m_spi();
    if (!p) {
        return false;
    }

    std::vector<uint8_t> data;
    if (!hexutils::stringUnhexlify(strArgs, data) || data.empty() || data.size() > 16) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected 1-16 hex bytes"));
        return false;
    }

    auto miso = p->bulk_write(data, st);
    if (miso.empty() && !data.empty()) {
        return false;
    }

    LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("MISO:"));
    hexutils::HexDump2(miso.data(), miso.size());
    return true;
}

///////////////////////////////////////////////////////////////////
//                       READ                                    //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_spi_read(const std::string &strArgs, std::stop_token st) const
{
    if (strArgs == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: read N  (read N bytes, clocks 0xFF)"));
        return true;
    }
    auto *p = m_spi();
    if (!p) {
        return false;
    }

    size_t n = 0;
    if (!numeric::str2sizet(strArgs, n) || n == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid byte count"));
        return false;
    }

    auto data = p->read(n, false, st);
    hexutils::HexDump2(data.data(), data.size());
    return true;
}

///////////////////////////////////////////////////////////////////
//                       WRRD                                    //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_spi_wrrd_cb(std::span<const uint8_t> req, size_t rdlen, std::stop_token st) const
{
    auto *p = m_spi();
    if (!p) {
        return false;
    }

    auto result = p->write_read(req, rdlen, false, st);
    if (!result) {
        return false;
    }

    if (!result->empty()) {
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("Read:"));
        hexutils::HexDump2(result->data(), result->size());
    }
    return true;
}

bool HydrabusPlugin::m_handle_spi_wrrd(const std::string &strArgs, std::stop_token st) const
{
    return generic_write_read_data<HydrabusPlugin>(
        this, strArgs, &HydrabusPlugin::m_spi_wrrd_cb, st);
}

bool HydrabusPlugin::m_handle_spi_wrrdf(const std::string &strArgs, std::stop_token st) const
{
    return generic_write_read_file<HydrabusPlugin>(
        this, strArgs, &HydrabusPlugin::m_spi_wrrd_cb,
        m_sIniValues.strArtefactsPath, st);
}

///////////////////////////////////////////////////////////////////
//                       AUX                                     //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_spi_aux(const std::string &strArgs, std::stop_token /*st*/) const
{
    return m_handle_aux_common(strArgs, m_spi());
}

///////////////////////////////////////////////////////////////////
//                       SCRIPT                                  //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_spi_script(const std::string &strArgs, std::stop_token st) const
{
    if (strArgs == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: <scriptname>"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  Executes script from ARTEFACTS_PATH/scriptname"));
        return true;
    }
    return generic_execute_script(this, m_strInstanceName, strArgs, st);
}
