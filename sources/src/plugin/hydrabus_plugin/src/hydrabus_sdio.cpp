/*
 * HydraBus Plugin – SDIO protocol handlers
 *
 * Subcommands:
 *   cfg        width=[1|4] freq=[slow|fast]
 *   send_no    cmd_id cmd_arg     (no response)
 *   send_short cmd_id cmd_arg     (4-byte response)
 *   send_long  cmd_id cmd_arg     (16-byte response)
 *   read       cmd_id cmd_arg     (CMD17 block read)
 *   write      cmd_id cmd_arg HEXDATA  (CMD24 block write, 512 bytes)
 *   aux        N [in|out|pp] [0|1]
 *   help
 *
 * cmd_id  : decimal (0-63)
 * cmd_arg : hex 32-bit value (e.g. 000001AA)
 */

#include "hydrabus_plugin.hpp"
#include "hydrabus_generic.hpp"

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
#define LT_HDR   "HB_SDIO    |"
#define LOG_HDR  LOG_STRING(LT_HDR)

#define PROTOCOL_NAME "SDIO"

///////////////////////////////////////////////////////////////////
//              Helper: parse cmd_id and cmd_arg                 //
///////////////////////////////////////////////////////////////////

static bool parseCmdArgs(const std::vector<std::string>& parts,
                          uint8_t& cmd_id, uint32_t& cmd_arg)
{
    if (parts.size() < 2) return false;

    uint32_t id = 0;
    if (!numeric::str2uint32(parts[0], id) || id > 63) return false;
    cmd_id = static_cast<uint8_t>(id);

    std::vector<uint8_t> argBuf;
    if (hexutils::stringUnhexlify(parts[1], argBuf) && argBuf.size() == 4) {
        cmd_arg = (static_cast<uint32_t>(argBuf[0]) << 24)
                | (static_cast<uint32_t>(argBuf[1]) << 16)
                | (static_cast<uint32_t>(argBuf[2]) <<  8)
                |  static_cast<uint32_t>(argBuf[3]);
    } else {
        if (!numeric::str2uint32(parts[1], cmd_arg)) return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_sdio_help(const std::string&) const
{
    return generic_module_list_commands<HydrabusPlugin>(this, PROTOCOL_NAME);
}

bool HydrabusPlugin::m_handle_sdio_cfg(const std::string& args) const
{
    auto* p = m_sdio();
    if (args == "help" || args == "?") {
        if (p) {
            LOG_PRINT(LOG_EMPTY,
                      LOG_STRING("width="); LOG_INT(p->get_bus_width());
                      LOG_STRING("freq=");  LOG_INT(p->get_frequency()));
        }
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: cfg width=[1|4] freq=[slow|fast]"));
        return true;
    }
    if (!p) return false;

    std::vector<std::string> pairs;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, pairs);
    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() != 2) continue;

        if (kv[0] == "width") {
            uint8_t v = 0;
            if (!numeric::str2uint8(kv[1], v)) return false;
            if (!p->set_bus_width(v)) return false;
        } else if (kv[0] == "freq") {
            int f = (kv[1] == "fast") ? 1 : 0;
            if (!p->set_frequency(f)) return false;
        }
    }
    return true;
}

bool HydrabusPlugin::m_handle_sdio_send_no(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: send_no cmd_id cmd_arg  (e.g. send_no 0 00000000)"));
        return true;
    }
    auto* p = m_sdio();
    if (!p) return false;

    std::vector<std::string> parts;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, parts);

    uint8_t  cmd_id  = 0;
    uint32_t cmd_arg = 0;
    if (!parseCmdArgs(parts, cmd_id, cmd_arg)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected: send_no cmd_id cmd_arg"));
        return false;
    }

    return p->send_no(cmd_id, cmd_arg);
}

bool HydrabusPlugin::m_handle_sdio_send_short(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: send_short cmd_id cmd_arg  (4-byte response)"));
        return true;
    }
    auto* p = m_sdio();
    if (!p) return false;

    std::vector<std::string> parts;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, parts);

    uint8_t  cmd_id  = 0;
    uint32_t cmd_arg = 0;
    if (!parseCmdArgs(parts, cmd_id, cmd_arg)) return false;

    auto resp = p->send_short(cmd_id, cmd_arg);
    if (!resp) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Command failed"));
        return false;
    }
    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Response:"));
    hexutils::HexDump2(resp->data(), resp->size());
    return true;
}

bool HydrabusPlugin::m_handle_sdio_send_long(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: send_long cmd_id cmd_arg  (16-byte response)"));
        return true;
    }
    auto* p = m_sdio();
    if (!p) return false;

    std::vector<std::string> parts;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, parts);

    uint8_t  cmd_id  = 0;
    uint32_t cmd_arg = 0;
    if (!parseCmdArgs(parts, cmd_id, cmd_arg)) return false;

    auto resp = p->send_long(cmd_id, cmd_arg);
    if (!resp) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Command failed"));
        return false;
    }
    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Response:"));
    hexutils::HexDump2(resp->data(), resp->size());
    return true;
}

bool HydrabusPlugin::m_handle_sdio_read(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: read cmd_id cmd_arg  (e.g. read 17 00000000)"));
        return true;
    }
    auto* p = m_sdio();
    if (!p) return false;

    std::vector<std::string> parts;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, parts);

    uint8_t  cmd_id  = 0;
    uint32_t cmd_arg = 0;
    if (!parseCmdArgs(parts, cmd_id, cmd_arg)) return false;

    auto data = p->read(cmd_id, cmd_arg);
    if (data.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Read failed"));
        return false;
    }
    hexutils::HexDump2(data.data(), data.size());
    return true;
}

// write cmd_id cmd_arg HEXDATA
bool HydrabusPlugin::m_handle_sdio_write(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: write cmd_id cmd_arg HEXDATA  (512 bytes)"));
        return true;
    }
    auto* p = m_sdio();
    if (!p) return false;

    std::vector<std::string> parts;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, parts);
    if (parts.size() != 3) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Expected: write cmd_id cmd_arg HEXDATA"));
        return false;
    }

    uint8_t  cmd_id  = 0;
    uint32_t cmd_arg = 0;
    if (!parseCmdArgs(parts, cmd_id, cmd_arg)) return false;

    std::vector<uint8_t> data;
    if (!hexutils::stringUnhexlify(parts[2], data) ||
        data.size() != HydraHAL::SDIO::BLOCK_SIZE) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Payload must be exactly 512 bytes"));
        return false;
    }

    return p->write(cmd_id, cmd_arg, data);
}

bool HydrabusPlugin::m_handle_sdio_aux(const std::string& args) const
{
    return m_handle_aux_common(args, m_sdio());
}
