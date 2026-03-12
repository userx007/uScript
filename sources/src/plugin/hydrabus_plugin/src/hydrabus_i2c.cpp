/*
 * HydraBus Plugin – I2C protocol handlers
 *
 * Subcommands:
 *   cfg     pullup=[0|1] stretch=N
 *   speed   [50kHz|100kHz|400kHz|1MHz]
 *   bit     [start|stop|ack|nack]
 *   write   AABB..        (hex, 1-16 bytes)
 *   read    N             (read N bytes with ACK/NACK sequence)
 *   wrrd    [hexdata][:rdlen]
 *   wrrdf   filename[:wrchunk][:rdchunk]
 *   scan                  (scan all 7-bit addresses)
 *   stretch N             (set clock-stretch timeout in cycles, 0=off)
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
#define LT_HDR   "HB_I2C     |"
#define LOG_HDR  LOG_STRING(LT_HDR)

#define PROTOCOL_NAME "I2C"

///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_i2c_help(const std::string&) const
{
    return generic_module_list_commands<HydrabusPlugin>(this, PROTOCOL_NAME);
}

///////////////////////////////////////////////////////////////////
//                       CFG                                     //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_i2c_cfg(const std::string& args) const
{
    auto* p = m_i2c();

    if (args == "help" || args == "?") {
        if (p) {
            LOG_PRINT(LOG_EMPTY,
                      LOG_STRING("pullup="); LOG_UINT8(p->get_pullup() ? 1 : 0));
        }
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: cfg pullup=[0|1]"));
        return true;
    }
    if (!p) return false;

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
//                       SPEED                                   //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_i2c_speed(const std::string& args) const
{
    return generic_module_set_speed<HydrabusPlugin>(this, PROTOCOL_NAME, args);
}

///////////////////////////////////////////////////////////////////
//                       BIT                                     //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_i2c_bit(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: bit [start|stop|ack|nack]"));
        return true;
    }
    auto* p = m_i2c();
    if (!p) return false;

    if      (args == "start") return p->start();
    else if (args == "stop")  return p->stop();
    else if (args == "ack")   return p->send_ack();
    else if (args == "nack")  return p->send_nack();
    else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown bit cmd:"); LOG_STRING(args));
        return false;
    }
}

///////////////////////////////////////////////////////////////////
//                       WRITE                                   //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_i2c_write(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: write AABB..  (hex, 1-16 bytes)"));
        return true;
    }
    auto* p = m_i2c();
    if (!p) return false;

    std::vector<uint8_t> data;
    if (!hexutils::stringUnhexlify(args, data) || data.empty() || data.size() > 16) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected 1-16 hex bytes"));
        return false;
    }

    auto acks = p->bulk_write(data);
    if (acks.size() != data.size()) return false;

    // Print ACK/NACK status per byte
    for (size_t i = 0; i < acks.size(); ++i) {
        LOG_PRINT(LOG_INFO, LOG_HDR;
                  LOG_STRING("Byte"); LOG_SIZET(i);
                  LOG_STRING(acks[i] == 0 ? "ACK" : "NACK"));
    }
    return true;
}

///////////////////////////////////////////////////////////////////
//                       READ                                    //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_i2c_read(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: read N  (ACKs all but last byte)"));
        return true;
    }
    auto* p = m_i2c();
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
//                       WRRD / WRRDF                           //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_i2c_wrrd_cb(std::span<const uint8_t> req, size_t rdlen) const
{
    auto* p = m_i2c();
    if (!p) return false;

    auto result = p->write_read(req, rdlen);
    if (!result) return false;

    if (!result->empty()) {
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Read:"));
        hexutils::HexDump2(result->data(), result->size());
    }
    return true;
}

bool HydrabusPlugin::m_handle_i2c_wrrd(const std::string& args) const
{
    return generic_write_read_data<HydrabusPlugin>(
        this, args, &HydrabusPlugin::m_i2c_wrrd_cb);
}

bool HydrabusPlugin::m_handle_i2c_wrrdf(const std::string& args) const
{
    return generic_write_read_file<HydrabusPlugin>(
        this, args, &HydrabusPlugin::m_i2c_wrrd_cb,
        m_sIniValues.strArtefactsPath);
}

///////////////////////////////////////////////////////////////////
//                       SCAN                                    //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_i2c_scan(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Probe all 7-bit I2C addresses"));
        return true;
    }
    auto* p = m_i2c();
    if (!p) return false;

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Scanning I2C bus..."));
    auto addrs = p->scan();

    if (addrs.empty()) {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("No devices found"));
    } else {
        for (uint8_t a : addrs) {
            std::ostringstream oss;
            oss << "Found device at 0x" << std::hex << std::uppercase
                << std::setw(2) << std::setfill('0') << (int)a;
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(oss.str()));
        }
    }
    return true;
}

///////////////////////////////////////////////////////////////////
//                       STRETCH                                 //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_i2c_stretch(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: stretch N  (N=0 to disable)"));
        return true;
    }
    auto* p = m_i2c();
    if (!p) return false;

    uint32_t clocks = 0;
    if (!numeric::str2uint32(args, clocks)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid clock count"));
        return false;
    }
    return p->set_clock_stretch(clocks);
}

///////////////////////////////////////////////////////////////////
//                       AUX                                     //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_i2c_aux(const std::string& args) const
{
    return m_handle_aux_common(args, m_i2c());
}

///////////////////////////////////////////////////////////////////
//                       SCRIPT                                  //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_i2c_script(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: <scriptname>"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  Executes script from ARTEFACTS_PATH/scriptname"));
        return true;
    }
    return generic_execute_script(this, args);
}
