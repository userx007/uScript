/*
 * HydraBus Plugin – MMC/eMMC protocol handlers
 *
 * Subcommands:
 *   cfg     width=[1|4]
 *   cid                  (read 16-byte CID register)
 *   csd                  (read 16-byte CSD register)
 *   ext_csd              (read 512-byte EXT_CSD register)
 *   read    block_num    (read 512-byte block at address)
 *   write   block_num    (write 512 hex bytes to block – data on next line prompt)
 *   aux     N [in|out|pp] [0|1]
 *   help
 *
 * Note: For 'write', the hex payload (512 bytes = 1024 hex chars) is
 * passed as the second space-separated token:
 *   HYDRABUS.MMC write 0 AABB...  (1024 hex chars)
 */

#include "hydrabus_plugin.hpp"
#include "hydrabus_generic.hpp"

#include "uNumeric.hpp"
#include "uHexlify.hpp"
#include "uHexdump.hpp"
#include "uLogger.hpp"

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef  LT_HDR
#undef  LT_HDR
#endif
#ifdef  LOG_HDR
#undef  LOG_HDR
#endif
#define LT_HDR   "HB_MMC     |"
#define LOG_HDR  LOG_STRING(LT_HDR)

#define PROTOCOL_NAME "MMC"

///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_mmc_help(const std::string&) const
{
    return generic_module_list_commands<HydrabusPlugin>(this, PROTOCOL_NAME);
}

bool HydrabusPlugin::m_handle_mmc_cfg(const std::string& args) const
{
    auto* p = m_mmc();
    if (args == "help" || args == "?") {
        if (p)
            LOG_PRINT(LOG_EMPTY,
                      LOG_STRING("width="); LOG_INT(p->get_bus_width()));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: cfg width=[1|4]"));
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
        }
    }
    return true;
}

bool HydrabusPlugin::m_handle_mmc_cid(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Read 16-byte CID register"));
        return true;
    }
    auto* p = m_mmc();
    if (!p) return false;

    auto data = p->get_cid();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CID:"));
    hexutils::HexDump2(data.data(), data.size());
    return true;
}

bool HydrabusPlugin::m_handle_mmc_csd(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Read 16-byte CSD register"));
        return true;
    }
    auto* p = m_mmc();
    if (!p) return false;

    auto data = p->get_csd();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CSD:"));
    hexutils::HexDump2(data.data(), data.size());
    return true;
}

bool HydrabusPlugin::m_handle_mmc_ext_csd(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Read 512-byte EXT_CSD register"));
        return true;
    }
    auto* p = m_mmc();
    if (!p) return false;

    auto data = p->get_ext_csd();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("EXT_CSD:"));
    hexutils::HexDump2(data.data(), data.size());
    return true;
}

bool HydrabusPlugin::m_handle_mmc_read(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: read block_num  (decimal block address)"));
        return true;
    }
    auto* p = m_mmc();
    if (!p) return false;

    uint32_t blk = 0;
    if (!numeric::str2uint32(args, blk)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid block number"));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Reading block"); LOG_UINT32(blk));
    auto data = p->read(blk);
    if (data.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Read failed"));
        return false;
    }
    hexutils::HexDump2(data.data(), data.size());
    return true;
}

// write block_num HEXDATA(1024 chars = 512 bytes)
bool HydrabusPlugin::m_handle_mmc_write(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: write block_num HEXDATA  (512 bytes = 1024 hex chars)"));
        return true;
    }
    auto* p = m_mmc();
    if (!p) return false;

    std::vector<std::string> parts;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, parts);
    if (parts.size() != 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected: write block_num HEXDATA"));
        return false;
    }

    uint32_t blk = 0;
    if (!numeric::str2uint32(parts[0], blk)) return false;

    std::vector<uint8_t> data;
    if (!hexutils::stringUnhexlify(parts[1], data) ||
        data.size() != HydraHAL::MMC::BLOCK_SIZE) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Payload must be exactly 512 bytes"));
        return false;
    }

    return p->write(data, blk);
}

bool HydrabusPlugin::m_handle_mmc_aux(const std::string& args) const
{
    return m_handle_aux_common(args, m_mmc());
}
