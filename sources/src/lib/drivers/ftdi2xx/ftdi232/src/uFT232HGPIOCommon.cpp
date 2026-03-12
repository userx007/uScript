/*
 * FT232H GPIO – cross-platform MPSSE implementation
 *
 * Identical in logic to uFT4232GPIOCommon.cpp.
 * No channel argument — FT232H has a single MPSSE interface.
 */

#include "uFT232HGPIO.hpp"
#include "uLogger.hpp"

#include <vector>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "FT232H_GPIO |"
#define LOG_HDR    LOG_STRING(LT_HDR)


// ============================================================================
// open / close
// ============================================================================

FT232HGPIO::Status FT232HGPIO::open(const GpioConfig& config, uint8_t u8DeviceIndex)
{
    if (is_open()) close();

    auto s = open_device(u8DeviceIndex);
    if (s != Status::SUCCESS) return s;

    mpsse_purge();

    s = configure_mpsse_gpio(config);
    if (s != Status::SUCCESS) {
        FT232HBase::close();
        return s;
    }
    return Status::SUCCESS;
}

FT232HGPIO::Status FT232HGPIO::close()
{
    // Drive all output pins low before releasing the handle
    if (is_open()) {
        apply_low (0x00u, m_lowDir);
        apply_high(0x00u, m_highDir);
    }
    return FT232HBase::close();
}


// ============================================================================
// MPSSE configuration
// ============================================================================

FT232HGPIO::Status FT232HGPIO::configure_mpsse_gpio(const GpioConfig& config)
{
    m_lowValue  = config.lowValue;
    m_lowDir    = config.lowDirMask;
    m_highValue = config.highValue;
    m_highDir   = config.highDirMask;

    std::vector<uint8_t> init;
    init.reserve(16);
    init.push_back(MPSSE_DIS_DIV5);
    init.push_back(MPSSE_DIS_3PHASE);
    init.push_back(MPSSE_DIS_ADAPTIVE);
    init.push_back(MPSSE_LOOPBACK_OFF);
    // Apply initial pin states
    init.push_back(MPSSE_SET_BITS_LOW);
    init.push_back(config.lowValue);
    init.push_back(config.lowDirMask);
    init.push_back(MPSSE_SET_BITS_HIGH);
    init.push_back(config.highValue);
    init.push_back(config.highDirMask);

    return mpsse_write(init.data(), init.size());
}


// ============================================================================
// Internal apply helpers
// ============================================================================

FT232HGPIO::Status FT232HGPIO::apply_low(uint8_t value, uint8_t dir) const
{
    uint8_t cmd[3] = { MPSSE_SET_BITS_LOW, value, dir };
    return mpsse_write(cmd, 3);
}

FT232HGPIO::Status FT232HGPIO::apply_high(uint8_t value, uint8_t dir) const
{
    uint8_t cmd[3] = { MPSSE_SET_BITS_HIGH, value, dir };
    return mpsse_write(cmd, 3);
}


// ============================================================================
// Direction control
// ============================================================================

FT232HGPIO::Status FT232HGPIO::set_direction(Bank bank, uint8_t dirMask,
                                              uint8_t initialValue)
{
    if (bank == Bank::Low) {
        m_lowDir    = dirMask;
        m_lowValue  = (m_lowValue & ~dirMask) | (initialValue & dirMask);
        return apply_low(m_lowValue, m_lowDir);
    } else {
        m_highDir   = dirMask;
        m_highValue = (m_highValue & ~dirMask) | (initialValue & dirMask);
        return apply_high(m_highValue, m_highDir);
    }
}


// ============================================================================
// Output control
// ============================================================================

FT232HGPIO::Status FT232HGPIO::write(Bank bank, uint8_t value)
{
    if (bank == Bank::Low) {
        m_lowValue = value;
        return apply_low(m_lowValue, m_lowDir);
    } else {
        m_highValue = value;
        return apply_high(m_highValue, m_highDir);
    }
}

FT232HGPIO::Status FT232HGPIO::set_pins(Bank bank, uint8_t pinMask)
{
    if (bank == Bank::Low) {
        m_lowValue |= pinMask;
        return apply_low(m_lowValue, m_lowDir);
    } else {
        m_highValue |= pinMask;
        return apply_high(m_highValue, m_highDir);
    }
}

FT232HGPIO::Status FT232HGPIO::clear_pins(Bank bank, uint8_t pinMask)
{
    if (bank == Bank::Low) {
        m_lowValue &= static_cast<uint8_t>(~pinMask);
        return apply_low(m_lowValue, m_lowDir);
    } else {
        m_highValue &= static_cast<uint8_t>(~pinMask);
        return apply_high(m_highValue, m_highDir);
    }
}

FT232HGPIO::Status FT232HGPIO::toggle_pins(Bank bank, uint8_t pinMask)
{
    if (bank == Bank::Low) {
        m_lowValue ^= pinMask;
        return apply_low(m_lowValue, m_lowDir);
    } else {
        m_highValue ^= pinMask;
        return apply_high(m_highValue, m_highDir);
    }
}


// ============================================================================
// Input reading
// ============================================================================

FT232HGPIO::Status FT232HGPIO::read(Bank bank, uint8_t& value)
{
    uint8_t cmd[2];
    cmd[0] = (bank == Bank::Low) ? MPSSE_GET_BITS_LOW : MPSSE_GET_BITS_HIGH;
    cmd[1] = MPSSE_SEND_IMMEDIATE;

    auto s = mpsse_write(cmd, 2);
    if (s != Status::SUCCESS) return s;

    size_t got = 0;
    return mpsse_read(&value, 1, FT232H_READ_DEFAULT_TIMEOUT, got);
}

FT232HGPIO::Status FT232HGPIO::read_pins(Bank bank, uint8_t pinMask, uint8_t& value)
{
    uint8_t raw = 0;
    auto s = read(bank, raw);
    value = raw & pinMask;
    return s;
}
