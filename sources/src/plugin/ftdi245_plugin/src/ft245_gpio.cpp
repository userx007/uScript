/*
 * FT245 Plugin – GPIO bit-bang protocol handlers
 *
 * Controls all 8 data pins (D0–D7) via BITMODE_BITBANG.
 * Unlike the FT2232 GPIO driver there is only a single 8-bit port —
 * no bank selector is needed; every command operates on all 8 pins.
 *
 * Subcommands:
 *   open   [variant=BM|R] [dir=0xNN] [val=0xNN] [device=N]
 *   close
 *   cfg    [variant=BM|R] [dir=0xNN] [val=0xNN]
 *   dir    MASK [INITVAL]    — set direction (1=out, 0=in)
 *   write  VALUE             — write output byte (hex)
 *   set    MASK              — drive masked pins HIGH
 *   clear  MASK              — drive masked pins LOW
 *   toggle MASK              — toggle masked pins
 *   read                     — read current pin levels, prints hex + binary
 *   help
 */

#include "ft245_plugin.hpp"
#include "ft245_generic.hpp"

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
#define LT_HDR   "FT245_GPIO |"
#define LOG_HDR  LOG_STRING(LT_HDR)

#define PROTOCOL_NAME "GPIO"

///////////////////////////////////////////////////////////////////
//             Internal parse helpers                            //
///////////////////////////////////////////////////////////////////

static bool parseHexByte(const std::string& s, uint8_t& out)
{
    return numeric::str2uint8(s, out);
}

///////////////////////////////////////////////////////////////////
//                       HELP                                    //
///////////////////////////////////////////////////////////////////

bool FT245Plugin::m_handle_gpio_help(const std::string&) const
{
    return generic_module_list_commands<FT245Plugin>(this, PROTOCOL_NAME);
}

///////////////////////////////////////////////////////////////////
//                       OPEN                                    //
///////////////////////////////////////////////////////////////////

bool FT245Plugin::m_handle_gpio_open(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: open [variant=BM|R] [dir=0xNN] [val=0xNN] [device=N]"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("  dir : direction mask — 1=output, 0=input (default 0x00 = all inputs)"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("  val : initial output levels (default 0x00)"));
        return true;
    }

    uint8_t devIdx = m_sIniValues.u8DeviceIndex;
    if (!parseGpioParams(args, m_sGpioCfg, &devIdx)) return false;
    const_cast<FT245Plugin*>(this)->m_sIniValues.u8DeviceIndex = devIdx;

    if (m_pGPIO) { m_pGPIO->close(); m_pGPIO.reset(); }

    FT245GPIO::GpioConfig cfg;
    cfg.variant      = m_sGpioCfg.variant;
    cfg.dirMask      = m_sGpioCfg.dirMask;
    cfg.initialValue = m_sGpioCfg.initValue;

    m_pGPIO = std::make_unique<FT245GPIO>();
    auto s = m_pGPIO->open(cfg, m_sIniValues.u8DeviceIndex);
    if (s != FT245GPIO::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("GPIO open failed"));
        m_pGPIO.reset();
        return false;
    }

    const char* varStr = (cfg.variant == FT245Base::Variant::FT245BM) ? "BM" : "R";
    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("GPIO opened: variant="); LOG_STRING(varStr);
              LOG_STRING("dir=0x");  LOG_HEX8(cfg.dirMask);
              LOG_STRING("val=0x");  LOG_HEX8(cfg.initialValue);
              LOG_STRING("device="); LOG_UINT32(m_sIniValues.u8DeviceIndex));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CLOSE                                   //
///////////////////////////////////////////////////////////////////

bool FT245Plugin::m_handle_gpio_close(const std::string&) const
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

bool FT245Plugin::m_handle_gpio_cfg(const std::string& args) const
{
    if (args == "help" || args == "?") {
        const char* varStr = (m_sGpioCfg.variant == FT245Base::Variant::FT245BM) ? "BM" : "R";
        LOG_PRINT(LOG_EMPTY, LOG_STRING("GPIO pending config:"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("  variant="); LOG_STRING(varStr);
                  LOG_STRING("dir=0x");     LOG_HEX8(m_sGpioCfg.dirMask);
                  LOG_STRING("val=0x");     LOG_HEX8(m_sGpioCfg.initValue));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: cfg [variant=BM|R] [dir=0xNN] [val=0xNN]"));
        return true;
    }

    if (!parseGpioParams(args, m_sGpioCfg)) return false;

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("GPIO config updated (takes effect on next open)"));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       DIR                                     //
///////////////////////////////////////////////////////////////////

bool FT245Plugin::m_handle_gpio_dir(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: dir MASK [INITVAL]  (hex byte; 1=output 0=input per D0–D7)"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("  e.g.  dir FF 00  — all D0–D7 outputs, initially low"));
        return true;
    }

    auto* p = m_gpio();
    if (!p) return false;

    std::vector<std::string> parts;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, parts);
    if (parts.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Use: dir MASK [INITVAL]"));
        return false;
    }

    uint8_t mask = 0;
    if (!parseHexByte(parts[0], mask)) return false;

    uint8_t initVal = 0x00u;
    if (parts.size() >= 2 && !parseHexByte(parts[1], initVal)) return false;

    auto s = p->set_direction(mask, initVal);
    if (s != FT245GPIO::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("set_direction failed"));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Direction set: dir=0x"); LOG_HEX8(mask);
              LOG_STRING("initval=0x"); LOG_HEX8(initVal));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       WRITE                                   //
///////////////////////////////////////////////////////////////////

bool FT245Plugin::m_handle_gpio_write(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: write VALUE  (hex byte — written to D0–D7 output pins)"));
        return true;
    }

    auto* p = m_gpio();
    if (!p) return false;

    uint8_t value = 0;
    if (!parseHexByte(args, value)) return false;

    auto s = p->write(value);
    if (s != FT245GPIO::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("write failed"));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Wrote: value=0x"); LOG_HEX8(value));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       SET                                     //
///////////////////////////////////////////////////////////////////

bool FT245Plugin::m_handle_gpio_set(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: set MASK  (drive masked D0–D7 pins HIGH)"));
        return true;
    }

    auto* p = m_gpio();
    if (!p) return false;

    uint8_t mask = 0;
    if (!parseHexByte(args, mask)) return false;

    auto s = p->set_pins(mask);
    if (s != FT245GPIO::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("set_pins failed"));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Pins set HIGH: mask=0x"); LOG_HEX8(mask));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CLEAR                                   //
///////////////////////////////////////////////////////////////////

bool FT245Plugin::m_handle_gpio_clear(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: clear MASK  (drive masked D0–D7 pins LOW)"));
        return true;
    }

    auto* p = m_gpio();
    if (!p) return false;

    uint8_t mask = 0;
    if (!parseHexByte(args, mask)) return false;

    auto s = p->clear_pins(mask);
    if (s != FT245GPIO::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("clear_pins failed"));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Pins cleared LOW: mask=0x"); LOG_HEX8(mask));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       TOGGLE                                  //
///////////////////////////////////////////////////////////////////

bool FT245Plugin::m_handle_gpio_toggle(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: toggle MASK  (invert masked D0–D7 output pins)"));
        return true;
    }

    auto* p = m_gpio();
    if (!p) return false;

    uint8_t mask = 0;
    if (!parseHexByte(args, mask)) return false;

    auto s = p->toggle_pins(mask);
    if (s != FT245GPIO::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("toggle_pins failed"));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Pins toggled: mask=0x"); LOG_HEX8(mask));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       READ                                    //
///////////////////////////////////////////////////////////////////

bool FT245Plugin::m_handle_gpio_read(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: read  (samples all D0–D7 pins; prints hex + binary)"));
        return true;
    }

    auto* p = m_gpio();
    if (!p) return false;

    uint8_t value = 0;
    auto s = p->read(value);
    if (s != FT245GPIO::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("read failed"));
        return false;
    }

    std::ostringstream oss;
    oss << "D0-D7: 0x"
        << std::hex << std::uppercase
        << std::setw(2) << std::setfill('0') << static_cast<int>(value)
        << "  [";
    for (int bit = 7; bit >= 0; --bit)
        oss << ((value >> bit) & 1);
    oss << "]  (D7..D0)";

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(oss.str()));
    return true;
}
