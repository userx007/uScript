/*
 * CP2112 Plugin – GPIO protocol handlers
 *
 * The CP2112 exposes 8 GPIO pins as a single flat byte-masked port —
 * there is no "low bank / high bank" concept like on the FTDI MPSSE chips.
 *
 * All write/set/clear operations use CP2112Gpio::gpio_write(valueMask, applyMask):
 *   applyMask = which pins to touch (1 = update, 0 = leave unchanged)
 *   valueMask = desired levels for those pins (1 = high, 0 = low)
 *
 * Special-function pins (AN495 §5.2):
 *   GPIO.0 → TX LED       (specialFuncMask bit 0)
 *   GPIO.1 → interrupt out (specialFuncMask bit 1)
 *   GPIO.6 → clock output  (specialFuncMask bit 6, frequency via clockDivider)
 *   GPIO.7 → RX LED       (specialFuncMask bit 7)
 *
 * Subcommands:
 *   open   [device=N] [dir=0xNN] [pp=0xNN] [special=0xNN] [clkdiv=N]
 *   close
 *   cfg    [dir=0xNN] [pp=0xNN] [special=0xNN] [clkdiv=N]
 *   write  VALUE MASK
 *   set    MASK   (drive masked pins HIGH)
 *   clear  MASK   (drive masked pins LOW)
 *   read
 *   help
 */

#include "cp2112_plugin.hpp"
#include "cp2112_generic.hpp"

#include "uString.hpp"
#include "uNumeric.hpp"
#include "uLogger.hpp"

#include <iomanip>
#include <sstream>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "CP2112_GPIO |"
#define LOG_HDR    LOG_STRING(LT_HDR)

#define PROTOCOL_NAME "GPIO"

///////////////////////////////////////////////////////////////////
//                   Internal helper                             //
///////////////////////////////////////////////////////////////////

static bool parseHexByte(const std::string& s, uint8_t& out)
{
    return numeric::str2uint8(s, out);
}

///////////////////////////////////////////////////////////////////
//                       HELP                                    //
///////////////////////////////////////////////////////////////////

bool CP2112Plugin::m_handle_gpio_help(const std::string&) const
{
    return generic_module_list_commands<CP2112Plugin>(this, PROTOCOL_NAME);
}

///////////////////////////////////////////////////////////////////
//              parseGpioKv — shared key/value parser            //
///////////////////////////////////////////////////////////////////

bool CP2112Plugin::parseGpioKv(const std::string& key,
                               const std::string& val,
                               GpioPendingCfg& cfg)
{
    // We access GpioPendingCfg fields directly; the struct is publicly
    // accessible through the plugin header's private section.  Since this
    // static helper is only called from within this translation unit (which
    // is part of CP2112Plugin's implementation), direct field access is fine.
    bool ok = true;
    if      (key == "dir"     || key == "direction") { ok = parseHexByte(val, cfg.directionMask);   }
    else if (key == "pp"      || key == "pushpull")  { ok = parseHexByte(val, cfg.pushPullMask);    }
    else if (key == "special" || key == "sf")        { ok = parseHexByte(val, cfg.specialFuncMask); }
    else if (key == "clkdiv"  || key == "divider")   { ok = parseHexByte(val, cfg.clockDivider);    }
    else {
        // Unknown — caller must handle
        return false;
    }
    if (!ok) {
        LOG_PRINT(LOG_ERROR, LOG_STRING("CP2112_GPIO|");
                  LOG_STRING("Invalid value for:"); LOG_STRING(key));
    }
    return ok;
}

///////////////////////////////////////////////////////////////////
//                       OPEN                                    //
///////////////////////////////////////////////////////////////////

bool CP2112Plugin::m_handle_gpio_open(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: open [device=N] [dir=0xNN] [pp=0xNN] [special=0xNN] [clkdiv=N]"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("  dir     : direction mask  — 1=output, 0=input (default 0x00 = all inputs)"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("  pp      : drive mode mask — 1=push-pull, 0=open-drain"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("  special : special-func    — bit0=TX_LED bit1=IRQ bit6=CLK_OUT bit7=RX_LED"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("  clkdiv  : clock divider   — only used when GPIO.6=CLK_OUT"));
        return true;
    }

    std::vector<std::string> pairs;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, pairs);

    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() != 2) continue;

        if (kv[0] == "device") {
            uint8_t v = 0;
            if (!numeric::str2uint8(kv[1], v)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid value for: device"));
                return false;
            }
            const_cast<CP2112Plugin*>(this)->m_sIniValues.u8DeviceIndex = v;
        } else if (!parseGpioKv(kv[0], kv[1], m_sGpioCfg)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Unknown or invalid key:"); LOG_STRING(kv[0]));
            return false;
        }
    }

    if (m_pGPIO) { m_pGPIO->close(); m_pGPIO.reset(); }

    m_pGPIO = std::make_unique<CP2112Gpio>();
    auto s = m_pGPIO->open(m_sIniValues.u8DeviceIndex);
    if (s != CP2112Gpio::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("GPIO open failed"));
        m_pGPIO.reset();
        return false;
    }

    // Push the initial pin configuration
    IGpioDriver::GpioConfig cfg;
    cfg.directionMask   = m_sGpioCfg.directionMask;
    cfg.pushPullMask    = m_sGpioCfg.pushPullMask;
    cfg.specialFuncMask = m_sGpioCfg.specialFuncMask;
    cfg.clockDivider    = m_sGpioCfg.clockDivider;

    s = m_pGPIO->gpio_configure(cfg);
    if (s != CP2112Gpio::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("gpio_configure failed after open"));
        m_pGPIO->close();
        m_pGPIO.reset();
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("GPIO opened and configured:");
              LOG_STRING("dir=0x");     LOG_HEX8(cfg.directionMask);
              LOG_STRING("pp=0x");      LOG_HEX8(cfg.pushPullMask);
              LOG_STRING("special=0x"); LOG_HEX8(cfg.specialFuncMask));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CLOSE                                   //
///////////////////////////////////////////////////////////////////

bool CP2112Plugin::m_handle_gpio_close(const std::string&) const
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

bool CP2112Plugin::m_handle_gpio_cfg(const std::string& args) const
{
    if (args == "help" || args == "?") {
        LOG_PRINT(LOG_EMPTY, LOG_STRING("GPIO pending config:"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("  dir=0x");     LOG_HEX8(m_sGpioCfg.directionMask);
                  LOG_STRING("pp=0x");        LOG_HEX8(m_sGpioCfg.pushPullMask);
                  LOG_STRING("special=0x");   LOG_HEX8(m_sGpioCfg.specialFuncMask);
                  LOG_STRING("clkdiv=");      LOG_UINT32(m_sGpioCfg.clockDivider));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: cfg [dir=0xNN] [pp=0xNN] [special=0xNN] [clkdiv=N]"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("  Changes to cfg take effect immediately if GPIO is open,"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("  otherwise they are stored and applied on the next open."));
        return true;
    }

    std::vector<std::string> pairs;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, pairs);

    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() != 2) continue;

        if (!parseGpioKv(kv[0], kv[1], m_sGpioCfg)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Unknown or invalid key:"); LOG_STRING(kv[0]));
            return false;
        }
    }

    // If GPIO is already open, push the updated configuration immediately
    if (m_pGPIO && m_pGPIO->is_open()) {
        IGpioDriver::GpioConfig cfg;
        cfg.directionMask   = m_sGpioCfg.directionMask;
        cfg.pushPullMask    = m_sGpioCfg.pushPullMask;
        cfg.specialFuncMask = m_sGpioCfg.specialFuncMask;
        cfg.clockDivider    = m_sGpioCfg.clockDivider;

        auto s = m_pGPIO->gpio_configure(cfg);
        if (s != CP2112Gpio::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("gpio_configure failed"));
            return false;
        }
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("GPIO config applied to open device"));
    } else {
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("GPIO config stored (takes effect on next open)"));
    }
    return true;
}

///////////////////////////////////////////////////////////////////
//                       WRITE                                   //
///////////////////////////////////////////////////////////////////

bool CP2112Plugin::m_handle_gpio_write(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: write VALUE MASK"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("  VALUE : desired pin levels  — 0x00..0xFF (1=high, 0=low)"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("  MASK  : which pins to touch — 0x00..0xFF (1=update, 0=leave unchanged)"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("  Example: write 0x01 0x01  (set GPIO.0 high, leave others unchanged)"));
        return true;
    }

    auto* p = m_gpio();
    if (!p) return false;

    std::vector<std::string> parts;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, parts);
    if (parts.size() < 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Use: write VALUE MASK"));
        return false;
    }

    uint8_t value = 0;
    uint8_t mask  = 0;
    if (!parseHexByte(parts[0], value) || !parseHexByte(parts[1], mask)) {
        return false;
    }

    auto s = p->gpio_write(value, mask);
    if (s != CP2112Gpio::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("gpio_write failed: value=0x"); LOG_HEX8(value);
                  LOG_STRING("mask=0x"); LOG_HEX8(mask));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Written: value=0x"); LOG_HEX8(value);
              LOG_STRING("mask=0x"); LOG_HEX8(mask));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       SET                                     //
///////////////////////////////////////////////////////////////////

bool CP2112Plugin::m_handle_gpio_set(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: set MASK  (drive all masked pins HIGH, leave others unchanged)"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("  Example: set 0x05  (GPIO.0 and GPIO.2 HIGH)"));
        return true;
    }

    auto* p = m_gpio();
    if (!p) return false;

    uint8_t mask = 0;
    if (!parseHexByte(args, mask)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid mask value:"); LOG_STRING(args));
        return false;
    }

    // gpio_write(mask, mask) → set all bits in mask HIGH
    auto s = p->gpio_write(mask, mask);
    if (s != CP2112Gpio::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("gpio_write (set) failed: mask=0x"); LOG_HEX8(mask));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Pins set HIGH: mask=0x"); LOG_HEX8(mask));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       CLEAR                                   //
///////////////////////////////////////////////////////////////////

bool CP2112Plugin::m_handle_gpio_clear(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: clear MASK  (drive all masked pins LOW, leave others unchanged)"));
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("  Example: clear 0x05  (GPIO.0 and GPIO.2 LOW)"));
        return true;
    }

    auto* p = m_gpio();
    if (!p) return false;

    uint8_t mask = 0;
    if (!parseHexByte(args, mask)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid mask value:"); LOG_STRING(args));
        return false;
    }

    // gpio_write(0x00, mask) → set all bits in mask LOW
    auto s = p->gpio_write(0x00u, mask);
    if (s != CP2112Gpio::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("gpio_write (clear) failed: mask=0x"); LOG_HEX8(mask));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Pins cleared LOW: mask=0x"); LOG_HEX8(mask));
    return true;
}

///////////////////////////////////////////////////////////////////
//                       READ                                    //
///////////////////////////////////////////////////////////////////

bool CP2112Plugin::m_handle_gpio_read(const std::string& args) const
{
    if (args == "help") {
        LOG_PRINT(LOG_EMPTY,
                  LOG_STRING("Use: read  (reads current logic levels of all 8 GPIO pins)"));
        return true;
    }

    auto* p = m_gpio();
    if (!p) return false;

    uint8_t value = 0;
    auto s = p->gpio_read(value);
    if (s != CP2112Gpio::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("gpio_read failed"));
        return false;
    }

    // Format: "GPIO: 0x3F  [00111111]"
    std::ostringstream oss;
    oss << "GPIO: 0x"
        << std::hex << std::uppercase
        << std::setw(2) << std::setfill('0') << static_cast<int>(value)
        << "  [";
    for (int bit = 7; bit >= 0; --bit) {
        oss << ((value >> bit) & 1);
    }
    oss << "]";

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(oss.str()));
    return true;
}
