/*
 * FT2232 Plugin – GPIO protocol handlers
 *
 * Same structure as FT4232H GPIO handlers.
 * Key difference: open() accepts variant=H|D forwarded to FT2232GPIO::open().
 * FT2232D restricts MPSSE to channel A only — enforced at open time.
 *
 * Subcommands:
 *   open   [variant=H|D] [channel=A|B] [device=N]
 *          [lowdir=0xNN] [lowval=0xNN] [highdir=0xNN] [highval=0xNN]
 *   close
 *   cfg    [variant=H|D] [channel=A|B]
 *          [lowdir=0xNN] [lowval=0xNN] [highdir=0xNN] [highval=0xNN]
 *   dir    [low|high] MASK          — set direction byte (1=output, 0=input)
 *   write  [low|high] VALUE         — write output byte (hex)
 *   set    [low|high] MASK          — drive masked pins HIGH
 *   clear  [low|high] MASK          — drive masked pins LOW
 *   toggle [low|high] MASK          — toggle masked pins
 *   read   [low|high]               — read current pin levels, print hex + binary
 *   help
 */

#include "ft2232_plugin.hpp"
#include "ft2232_generic.hpp"

#include "uString.hpp"
#include "uNumeric.hpp"
#include "uLogger.hpp"

#include <sstream>
#include <iomanip>
#include <vector>

///////////////////////////////////////////////////////////////////
//                       LOG DEFINES                             //
///////////////////////////////////////////////////////////////////

#ifdef  LT_HDR
#undef  LT_HDR
#endif
#ifdef  LOG_HDR
#undef  LOG_HDR
#endif
#define LT_HDR   "FT2_GPIO   |"
#define LOG_HDR  LOG_STRING(LT_HDR)

#define PROTOCOL_NAME "GPIO"

///////////////////////////////////////////////////////////////////
//             Internal parse helpers                            //
///////////////////////////////////////////////////////////////////

static bool parseBank(const std::string& s, FT2232GPIO::Bank& out)
{
    if (s == "low"  || s == "LOW"  || s == "l") { out = FT2232GPIO::Bank::Low;  return true; }
    if (s == "high" || s == "HIGH" || s == "h") { out = FT2232GPIO::Bank::High; return true; }
    LOG_PRINT(LOG_ERROR, LOG_STRING("FT2_GPIO   |");
              LOG_STRING("Invalid bank (use 'low' or 'high'):"); LOG_STRING(s));
    return false;
}

static bool parseHexByte(const std::string& s, uint8_t& out)
{
    return numeric::str2uint8(s, out);
}

///////////////////////////////////////////////////////////////////
//                       HELP                                    //
///////////////////////////////////////////////////////////////////

bool FT2232Plugin::m_handle_gpio_help(const std::string&) const
{
    return generic_module_list_commands<FT2232Plugin>(this, PROTOCOL_NAME);
}

///////////////////////////////////////////////////////////////////
//                       OPEN                                    //
///////////////////////////////////////////////////////////////////

bool FT2232Plugin::m_handle_gpio_open(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Use: open [variant=H|D] [channel=A|B] [device=N]"));
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("          [lowdir=0xNN] [lowval=0xNN] [highdir=0xNN] [highval=0xNN]"));
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("  FT2232D: channel A only"));
        return true;
    }

    std::vector<std::string> pairs;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, pairs);

    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() != 2) continue;

        bool ok = true;
        if      (kv[0] == "variant")  { ok = parseVariant (kv[1], m_sGpioCfg.variant);    }
        else if (kv[0] == "channel")  { ok = parseChannel (kv[1], m_sGpioCfg.channel);    }
        else if (kv[0] == "lowdir")   { ok = parseHexByte (kv[1], m_sGpioCfg.lowDirMask); }
        else if (kv[0] == "lowval")   { ok = parseHexByte (kv[1], m_sGpioCfg.lowValue);   }
        else if (kv[0] == "highdir")  { ok = parseHexByte (kv[1], m_sGpioCfg.highDirMask);}
        else if (kv[0] == "highval")  { ok = parseHexByte (kv[1], m_sGpioCfg.highValue);  }
        else if (kv[0] == "device") {
            uint8_t v = 0;
            ok = numeric::str2uint8(kv[1], v);
            if (ok) const_cast<FT2232Plugin*>(this)->m_sIniValues.u8DeviceIndex = v;
        } else {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown key:"); LOG_STRING(kv[0]));
            return false;
        }

        if (!ok) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Invalid value for:"); LOG_STRING(kv[0]));
            return false;
        }
    }

    if (m_sGpioCfg.variant == FT2232Base::Variant::FT2232D &&
        m_sGpioCfg.channel != FT2232Base::Channel::A) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("FT2232D has MPSSE on channel A only"));
        return false;
    }

    if (m_pGPIO) { m_pGPIO->close(); m_pGPIO.reset(); }

    FT2232GPIO::GpioConfig cfg;
    cfg.variant    = m_sGpioCfg.variant;
    cfg.channel    = m_sGpioCfg.channel;
    cfg.lowDirMask = m_sGpioCfg.lowDirMask;
    cfg.lowValue   = m_sGpioCfg.lowValue;
    cfg.highDirMask= m_sGpioCfg.highDirMask;
    cfg.highValue  = m_sGpioCfg.highValue;

    m_pGPIO = std::make_unique<FT2232GPIO>();
    auto s = m_pGPIO->open(cfg, m_sIniValues.u8DeviceIndex);
    if (s != FT2232GPIO::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("GPIO open failed"));
        m_pGPIO.reset();
        return false;
    }

    const char* varStr = (cfg.variant == FT2232Base::Variant::FT2232H) ? "H" : "D";
    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("GPIO opened: variant="); LOG_STRING(varStr);
              LOG_STRING("ch="); LOG_UINT32(static_cast<uint8_t>(cfg.channel));
              LOG_STRING("lowdir=0x"); LOG_HEX8(cfg.lowDirMask);
              LOG_STRING("highdir=0x"); LOG_HEX8(cfg.highDirMask));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CLOSE                                   //
///////////////////////////////////////////////////////////////////

bool FT2232Plugin::m_handle_gpio_close(const std::string&) const
{
    if (m_pGPIO) {
        m_pGPIO->close();
        m_pGPIO.reset();
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("GPIO closed"));
    } else {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("GPIO was not open"));
    }
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CFG                                     //
///////////////////////////////////////////////////////////////////

bool FT2232Plugin::m_handle_gpio_cfg(const std::string& args) const
{
    if (args == "help" || args == "?") {
        const char* varStr = (m_sGpioCfg.variant == FT2232Base::Variant::FT2232H) ? "H" : "D";
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("GPIO pending config:"));
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("  variant=");  LOG_STRING(varStr);
                  LOG_STRING("lowdir=0x");   LOG_HEX8(m_sGpioCfg.lowDirMask);
                  LOG_STRING("lowval=0x");   LOG_HEX8(m_sGpioCfg.lowValue);
                  LOG_STRING("highdir=0x");  LOG_HEX8(m_sGpioCfg.highDirMask);
                  LOG_STRING("highval=0x");  LOG_HEX8(m_sGpioCfg.highValue));
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Use: cfg [variant=H|D] [channel=A|B]"));
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("         [lowdir=0xNN] [lowval=0xNN] [highdir=0xNN] [highval=0xNN]"));
        return true;
    }

    std::vector<std::string> pairs;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, pairs);

    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() != 2) continue;

        bool ok = true;
        if      (kv[0] == "variant")  { ok = parseVariant (kv[1], m_sGpioCfg.variant);    }
        else if (kv[0] == "channel")  { ok = parseChannel (kv[1], m_sGpioCfg.channel);    }
        else if (kv[0] == "lowdir")   { ok = parseHexByte (kv[1], m_sGpioCfg.lowDirMask); }
        else if (kv[0] == "lowval")   { ok = parseHexByte (kv[1], m_sGpioCfg.lowValue);   }
        else if (kv[0] == "highdir")  { ok = parseHexByte (kv[1], m_sGpioCfg.highDirMask);}
        else if (kv[0] == "highval")  { ok = parseHexByte (kv[1], m_sGpioCfg.highValue);  }
        else {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown key:"); LOG_STRING(kv[0]));
            return false;
        }

        if (!ok) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Invalid value for:"); LOG_STRING(kv[0]));
            return false;
        }
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("GPIO config updated (takes effect on next open)"));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       DIR                                     //
///////////////////////////////////////////////////////////////////

bool FT2232Plugin::m_handle_gpio_dir(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Use: dir [low|high] MASK  (hex byte; 1=output 0=input)"));
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("     Optional: dir [low|high] MASK INITVAL"));
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("  e.g.  dir low FF 00  — all ADBUS outputs, initially low"));
        return true;
    }

    auto* p = m_gpio();
    if (!p) return false;

    std::vector<std::string> parts;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, parts);
    if (parts.size() < 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Use: dir [low|high] MASK [INITVAL]"));
        return false;
    }

    FT2232GPIO::Bank bank;
    if (!parseBank(parts[0], bank)) return false;

    uint8_t mask = 0;
    if (!parseHexByte(parts[1], mask)) return false;

    uint8_t initVal = 0x00u;
    if (parts.size() >= 3 && !parseHexByte(parts[2], initVal)) return false;

    auto s = p->set_direction(bank, mask, initVal);
    if (s != FT2232GPIO::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("set_direction failed"));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Direction set: bank="); LOG_STRING(parts[0]);
              LOG_STRING("dir=0x"); LOG_HEX8(mask);
              LOG_STRING("initval=0x"); LOG_HEX8(initVal));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       WRITE                                   //
///////////////////////////////////////////////////////////////////

bool FT2232Plugin::m_handle_gpio_write(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Use: write [low|high] VALUE  (hex byte)"));
        return true;
    }

    auto* p = m_gpio();
    if (!p) return false;

    std::vector<std::string> parts;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, parts);
    if (parts.size() < 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Use: write [low|high] VALUE"));
        return false;
    }

    FT2232GPIO::Bank bank;
    if (!parseBank(parts[0], bank)) return false;

    uint8_t value = 0;
    if (!parseHexByte(parts[1], value)) return false;

    auto s = p->write(bank, value);
    if (s != FT2232GPIO::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("write failed"));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Wrote: bank="); LOG_STRING(parts[0]);
              LOG_STRING("value=0x"); LOG_HEX8(value));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       SET                                     //
///////////////////////////////////////////////////////////////////

bool FT2232Plugin::m_handle_gpio_set(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Use: set [low|high] MASK  (drive masked pins HIGH)"));
        return true;
    }

    auto* p = m_gpio();
    if (!p) return false;

    std::vector<std::string> parts;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, parts);
    if (parts.size() < 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Use: set [low|high] MASK"));
        return false;
    }

    FT2232GPIO::Bank bank;
    if (!parseBank(parts[0], bank)) return false;

    uint8_t mask = 0;
    if (!parseHexByte(parts[1], mask)) return false;

    auto s = p->set_pins(bank, mask);
    if (s != FT2232GPIO::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("set_pins failed"));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Pins set HIGH: bank="); LOG_STRING(parts[0]);
              LOG_STRING("mask=0x"); LOG_HEX8(mask));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CLEAR                                   //
///////////////////////////////////////////////////////////////////

bool FT2232Plugin::m_handle_gpio_clear(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Use: clear [low|high] MASK  (drive masked pins LOW)"));
        return true;
    }

    auto* p = m_gpio();
    if (!p) return false;

    std::vector<std::string> parts;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, parts);
    if (parts.size() < 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Use: clear [low|high] MASK"));
        return false;
    }

    FT2232GPIO::Bank bank;
    if (!parseBank(parts[0], bank)) return false;

    uint8_t mask = 0;
    if (!parseHexByte(parts[1], mask)) return false;

    auto s = p->clear_pins(bank, mask);
    if (s != FT2232GPIO::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("clear_pins failed"));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Pins cleared LOW: bank="); LOG_STRING(parts[0]);
              LOG_STRING("mask=0x"); LOG_HEX8(mask));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       TOGGLE                                  //
///////////////////////////////////////////////////////////////////

bool FT2232Plugin::m_handle_gpio_toggle(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Use: toggle [low|high] MASK"));
        return true;
    }

    auto* p = m_gpio();
    if (!p) return false;

    std::vector<std::string> parts;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, parts);
    if (parts.size() < 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Use: toggle [low|high] MASK"));
        return false;
    }

    FT2232GPIO::Bank bank;
    if (!parseBank(parts[0], bank)) return false;

    uint8_t mask = 0;
    if (!parseHexByte(parts[1], mask)) return false;

    auto s = p->toggle_pins(bank, mask);
    if (s != FT2232GPIO::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("toggle_pins failed"));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Pins toggled: bank="); LOG_STRING(parts[0]);
              LOG_STRING("mask=0x"); LOG_HEX8(mask));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       READ                                    //
///////////////////////////////////////////////////////////////////

bool FT2232Plugin::m_handle_gpio_read(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Use: read [low|high]  (returns pin levels as hex + binary)"));
        return true;
    }

    auto* p = m_gpio();
    if (!p) return false;

    FT2232GPIO::Bank bank;
    if (!parseBank(args, bank)) return false;

    uint8_t value = 0;
    auto s = p->read(bank, value);
    if (s != FT2232GPIO::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("read failed"));
        return false;
    }

    std::ostringstream oss;
    oss << "Bank " << args
        << ": 0x" << std::hex << std::uppercase
        << std::setw(2) << std::setfill('0') << static_cast<int>(value)
        << "  [";
    for (int bit = 7; bit >= 0; --bit)
        oss << ((value >> bit) & 1);
    oss << "]";

    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING(oss.str()));
    return true;
}
