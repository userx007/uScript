/*
 * CH347 Plugin – GPIO protocol handlers
 *
 * The CH347 exposes GPIO0–GPIO7 (8 pins) as a flat bitmask,
 * unlike the FT2232 dual-bank model.  All commands operate on
 * pin-bitmasks directly.
 *
 * Subcommands:
 *   open   [device=/dev/... (Linux) or 0 (Windows)]
 *   close
 *   dir    output=0xNN [input=0xNN]   — set pin directions (1=output, 0=input)
 *   write  pins=0xNN levels=0xNN     — drive output pins to specified levels
 *   set    pins=0xNN                 — drive masked pins HIGH
 *   clear  pins=0xNN                 — drive masked pins LOW
 *   toggle pins=0xNN                 — toggle masked pins (read-modify-write)
 *   read                             — snapshot all pins, print hex + binary
 *   help
 */

#include "ch347_plugin.hpp"
#include "ch347_generic.hpp"

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
#define LT_HDR   "CH347_GPIO |"
#define LOG_HDR  LOG_STRING(LT_HDR)

#define PROTOCOL_NAME "GPIO"

///////////////////////////////////////////////////////////////////
//             Internal parse helper                             //
///////////////////////////////////////////////////////////////////

static bool parseHexByte(const std::string& s, uint8_t& out)
{
    return numeric::str2uint8(s, out);
}

static std::string fmtBinary8(uint8_t v)
{
    std::string s;
    for (int bit = 7; bit >= 0; --bit)
        s += ((v >> bit) & 1) ? '1' : '0';
    return s;
}

///////////////////////////////////////////////////////////////////
//                       HELP                                    //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_gpio_help(const std::string&) const
{
    return generic_module_list_commands<CH347Plugin>(this, PROTOCOL_NAME);
}

///////////////////////////////////////////////////////////////////
//                       OPEN                                    //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_gpio_open(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Use: open [device=/dev/... (Linux) or 0 (Windows)]"));
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("  Opens GPIO interface; all 8 pins default to inputs"));
        return true;
    }

    std::string devPath = m_sIniValues.strDevicePath;

    // Accept optional device=path override
    std::vector<std::string> pairs;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, pairs);
    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() == 2 && kv[0] == "device") {
            devPath = kv[1];
        }
    }
    const_cast<CH347Plugin*>(this)->m_sIniValues.strDevicePath = devPath;

    if (m_pGPIO) { m_pGPIO->close(); m_pGPIO.reset(); }

    m_pGPIO = std::make_unique<CH347GPIO>();
    auto s = m_pGPIO->open(devPath);
    if (s != CH347GPIO::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("GPIO open failed"));
        m_pGPIO.reset();
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("GPIO opened: device="); LOG_STRING(devPath));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CLOSE                                   //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_gpio_close(const std::string&) const
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
//                       DIR                                     //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_gpio_dir(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Use: dir output=0xNN [input=0xNN]"));
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("  output mask: bit N=1 → pin N is output"));
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("  e.g.  dir output=0x0F  — pins 0-3 output, 4-7 input"));
        return true;
    }

    auto* p = m_gpio();
    if (!p) return false;

    uint8_t outMask = 0x00u;
    std::vector<std::string> pairs;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, pairs);
    bool parsed = false;

    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() != 2) continue;
        if (kv[0] == "output") {
            if (!parseHexByte(kv[1], outMask)) return false;
            parsed = true;
        }
        // "input" is just ~output; ignore if provided (informational only)
    }

    if (!parsed) {
        // Also accept bare hex: dir 0x0F
        if (!parseHexByte(args, outMask)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Use: dir output=0xNN"));
            return false;
        }
    }

    m_sGpioCfg.dirMask = outMask;

    auto s = p->pin_set_direction(GpioPin::GPIO_ALL, outMask > 0);
    if (s != CH347GPIO::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("dir: failed to set direction"));
        return false;
    }

    // Apply each pin's direction individually via the 3-byte buffer API
    const uint8_t buf[3] = { 0xFFu, outMask, m_sGpioCfg.dataValue };
    auto wr = p->tout_write(0, std::span<const uint8_t>(buf, 3));

    if (wr.status != CH347GPIO::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("dir: failed to apply direction"));
        return false;
    }

    std::ostringstream oss;
    oss << "Direction set: out=0x"
        << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
        << static_cast<int>(outMask)
        << "  [" << fmtBinary8(outMask) << "]";
    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING(oss.str()));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       WRITE                                   //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_gpio_write(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Use: write pins=0xNN levels=0xNN"));
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("  pins: bitmask of pins to update"));
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("  levels: desired output level for each pin"));
        return true;
    }

    auto* p = m_gpio();
    if (!p) return false;

    uint8_t pinMask   = 0xFFu;
    uint8_t levelMask = 0x00u;
    bool    hasPins   = false, hasLevels = false;

    std::vector<std::string> pairs;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, pairs);
    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() != 2) continue;
        if (kv[0] == "pins")   { if (!parseHexByte(kv[1], pinMask))   return false; hasPins   = true; }
        if (kv[0] == "levels") { if (!parseHexByte(kv[1], levelMask)) return false; hasLevels = true; }
    }

    if (!hasLevels) {
        // Accept bare form: write 0xNN  (apply to all pins)
        if (!parseHexByte(args, levelMask)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Use: write pins=0xNN levels=0xNN"));
            return false;
        }
        pinMask = 0xFFu;
    }
    (void)hasPins;

    m_sGpioCfg.dataValue = (m_sGpioCfg.dataValue & ~pinMask) | (levelMask & pinMask);

    auto s = p->pins_write(pinMask, levelMask);
    if (s != CH347GPIO::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("write failed"));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Wrote: pins=0x"); LOG_HEX8(pinMask);
              LOG_STRING("levels=0x"); LOG_HEX8(levelMask));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       SET                                     //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_gpio_set(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Use: set pins=0xNN  (drive masked pins HIGH)"));
        return true;
    }

    auto* p = m_gpio();
    if (!p) return false;

    uint8_t mask = 0;
    // Accept "pins=0xNN" or bare "0xNN"
    std::vector<std::string> pairs;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, pairs);
    bool parsed = false;
    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() == 2 && kv[0] == "pins") {
            if (!parseHexByte(kv[1], mask)) return false;
            parsed = true;
        }
    }
    if (!parsed && !parseHexByte(args, mask)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Use: set pins=0xNN"));
        return false;
    }

    m_sGpioCfg.dataValue |= mask;
    auto s = p->pin_write(mask, true);
    if (s != CH347GPIO::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("set failed"));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Pins set HIGH: mask=0x"); LOG_HEX8(mask));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CLEAR                                   //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_gpio_clear(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Use: clear pins=0xNN  (drive masked pins LOW)"));
        return true;
    }

    auto* p = m_gpio();
    if (!p) return false;

    uint8_t mask = 0;
    std::vector<std::string> pairs;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, pairs);
    bool parsed = false;
    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() == 2 && kv[0] == "pins") {
            if (!parseHexByte(kv[1], mask)) return false;
            parsed = true;
        }
    }
    if (!parsed && !parseHexByte(args, mask)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Use: clear pins=0xNN"));
        return false;
    }

    m_sGpioCfg.dataValue &= ~mask;
    auto s = p->pin_write(mask, false);
    if (s != CH347GPIO::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("clear failed"));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Pins cleared LOW: mask=0x"); LOG_HEX8(mask));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       TOGGLE                                  //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_gpio_toggle(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Use: toggle pins=0xNN  (invert masked output pins)"));
        return true;
    }

    auto* p = m_gpio();
    if (!p) return false;

    uint8_t mask = 0;
    std::vector<std::string> pairs;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, pairs);
    bool parsed = false;
    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() == 2 && kv[0] == "pins") {
            if (!parseHexByte(kv[1], mask)) return false;
            parsed = true;
        }
    }
    if (!parsed && !parseHexByte(args, mask)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Use: toggle pins=0xNN"));
        return false;
    }

    // Read-modify-write using cached data value
    m_sGpioCfg.dataValue ^= mask;
    auto s = p->pins_write(mask, m_sGpioCfg.dataValue & mask);
    if (s != CH347GPIO::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("toggle failed"));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Pins toggled: mask=0x"); LOG_HEX8(mask);
              LOG_STRING("new_levels=0x"); LOG_HEX8(m_sGpioCfg.dataValue & mask));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       READ                                    //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_handle_gpio_read(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR;
                  LOG_STRING("Use: read  (snapshots all GPIO pins, prints hex + binary)"));
        return true;
    }

    auto* p = m_gpio();
    if (!p) return false;

    uint8_t iDir  = 0;
    uint8_t iData = 0;

    // Use the 2-byte read interface
    uint8_t buf[2] = {0, 0};
    ICommDriver::ReadOptions opts;
    opts.mode = ICommDriver::ReadMode::Exact;
    auto rd = p->tout_read(0, std::span<uint8_t>(buf, 2), opts);

    if (rd.status != CH347GPIO::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("read failed"));
        return false;
    }

    iDir  = buf[0];
    iData = buf[1];

    std::ostringstream oss;
    oss << "GPIO state:  dir=0x"
        << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
        << static_cast<int>(iDir)
        << "  data=0x"
        << std::setw(2) << static_cast<int>(iData)
        << "  [" << fmtBinary8(iData) << "]";
    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING(oss.str()));
    return true;
}
