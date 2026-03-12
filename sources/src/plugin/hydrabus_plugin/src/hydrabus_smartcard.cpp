/*
 * HydraBus Plugin – Smartcard (ISO 7816) protocol handlers
 *
 * Subcommands:
 *   cfg         pullup=[0|1]
 *   rst         [0|1]
 *   baud        N
 *   prescaler   N   (1 byte)
 *   guardtime   N   (1 byte)
 *   write       AABB..
 *   read        N
 *   atr              (retrieve Answer-To-Reset)
 *   aux         N [in|out|pp] [0|1]
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
#define LT_HDR   "HB_SMCARD  |"
#define LOG_HDR  LOG_STRING(LT_HDR)

#define PROTOCOL_NAME "SMARTCARD"

///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_smartcard_help(const std::string&) const
{
    return generic_module_list_commands<HydrabusPlugin>(this, PROTOCOL_NAME);
}

bool HydrabusPlugin::m_handle_smartcard_cfg(const std::string& args) const
{
    auto* p = m_smartcard();
    if (args == "help" || args == "?") {
        if (p)
            LOG_PRINT(LOG_EMPTY,
                      LOG_STRING("pullup="); LOG_UINT8(p->get_pullup() ? 1 : 0));
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
        }
    }
    return true;
}

bool HydrabusPlugin::m_handle_smartcard_rst(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: rst [0|1]"));
        return true;
    }
    auto* p = m_smartcard();
    if (!p) return false;

    uint8_t v = 0;
    if (!numeric::str2uint8(args, v)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected 0 or 1"));
        return false;
    }
    return p->set_rst(v);
}

bool HydrabusPlugin::m_handle_smartcard_baud(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: baud N"));
        return true;
    }
    auto* p = m_smartcard();
    if (!p) return false;

    uint32_t baud = 0;
    if (!numeric::str2uint32(args, baud)) return false;
    return p->set_baud(baud);
}

bool HydrabusPlugin::m_handle_smartcard_prescaler(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: prescaler N  (0-255)"));
        return true;
    }
    auto* p = m_smartcard();
    if (!p) return false;

    uint8_t v = 0;
    if (!numeric::str2uint8(args, v)) return false;
    return p->set_prescaler(v);
}

bool HydrabusPlugin::m_handle_smartcard_guardtime(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: guardtime N  (0-255)"));
        return true;
    }
    auto* p = m_smartcard();
    if (!p) return false;

    uint8_t v = 0;
    if (!numeric::str2uint8(args, v)) return false;
    return p->set_guardtime(v);
}

bool HydrabusPlugin::m_handle_smartcard_write(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: write AABB.."));
        return true;
    }
    auto* p = m_smartcard();
    if (!p) return false;

    std::vector<uint8_t> data;
    if (!hexutils::stringUnhexlify(args, data) || data.empty()) return false;
    return p->write(data);
}

bool HydrabusPlugin::m_handle_smartcard_read(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: read N"));
        return true;
    }
    auto* p = m_smartcard();
    if (!p) return false;

    size_t n = 0;
    if (!numeric::str2sizet(args, n) || n == 0) return false;

    auto data = p->read(n);
    hexutils::HexDump2(data.data(), data.size());
    return true;
}

bool HydrabusPlugin::m_handle_smartcard_atr(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Retrieve card ATR"));
        return true;
    }
    auto* p = m_smartcard();
    if (!p) return false;

    auto atr = p->get_atr();
    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("ATR:"));
    hexutils::HexDump2(atr.data(), atr.size());
    return true;
}

bool HydrabusPlugin::m_handle_smartcard_aux(const std::string& args) const
{
    return m_handle_aux_common(args, m_smartcard());
}
