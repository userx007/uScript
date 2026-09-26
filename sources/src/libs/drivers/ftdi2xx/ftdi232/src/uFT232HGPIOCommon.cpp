/*
 * FT232H GPIO – cross-platform MPSSE implementation
 *
 * Identical in logic to uFT4232GPIOCommon.cpp.
 * No channel argument — FT232H has a single MPSSE interface.
 */
#include "FT232HBase.hpp"
#include "uFT232HGPIO.hpp"
#include "uLogger.hpp"

#include <stddef.h>
#include <stdint.h>
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

#define LT_HDR  "FT232H_GPIO |"
#define LOG_HDR LOG_STRING(LT_HDR)

// ============================================================================
// open / close
// ============================================================================

FT232HGPIO::Status FT232HGPIO::open(const GpioConfig &sConfig, uint8_t u8DeviceIndex)
{
    if (is_open()) {
        close();
    }

    auto s = open_device(u8DeviceIndex);
    if (s != Status::SUCCESS) {
        return s;
    }

    mpsse_purge();

    s = configure_mpsse_gpio(sConfig);
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
        apply_low(0x00u, m_lowDir);
        apply_high(0x00u, m_highDir);
    }
    return FT232HBase::close();
}

// ============================================================================
// MPSSE configuration
// ============================================================================

FT232HGPIO::Status FT232HGPIO::configure_mpsse_gpio(const GpioConfig &sConfig)
{
    m_lowValue  = sConfig.lowValue;
    m_lowDir    = sConfig.lowDirMask;
    m_highValue = sConfig.highValue;
    m_highDir   = sConfig.highDirMask;

    std::vector<uint8_t> init;
    init.reserve(16);
    init.push_back(MPSSE_DIS_DIV5);
    init.push_back(MPSSE_DIS_3PHASE);
    init.push_back(MPSSE_DIS_ADAPTIVE);
    init.push_back(MPSSE_LOOPBACK_OFF);
    // Apply initial pin states
    init.push_back(MPSSE_SET_BITS_LOW);
    init.push_back(sConfig.lowValue);
    init.push_back(sConfig.lowDirMask);
    init.push_back(MPSSE_SET_BITS_HIGH);
    init.push_back(sConfig.highValue);
    init.push_back(sConfig.highDirMask);

    return mpsse_write(init.data(), init.size());
}

// ============================================================================
// Internal apply helpers
// ============================================================================

FT232HGPIO::Status FT232HGPIO::apply_low(uint8_t u8Value, uint8_t u8Dir) const
{
    uint8_t cmd[3] = {MPSSE_SET_BITS_LOW, u8Value, u8Dir};
    return mpsse_write(cmd, 3);
}

FT232HGPIO::Status FT232HGPIO::apply_high(uint8_t u8Value, uint8_t u8Dir) const
{
    uint8_t cmd[3] = {MPSSE_SET_BITS_HIGH, u8Value, u8Dir};
    return mpsse_write(cmd, 3);
}

// ============================================================================
// Direction control
// ============================================================================

FT232HGPIO::Status FT232HGPIO::set_direction(Bank eBank, uint8_t u8DirMask,
                                             uint8_t u8InitialValue)
{
    if (eBank == Bank::Low) {
        m_lowDir   = u8DirMask;
        m_lowValue = (m_lowValue & ~u8DirMask) | (u8InitialValue & u8DirMask);
        return apply_low(m_lowValue, m_lowDir);
    } else {
        m_highDir   = u8DirMask;
        m_highValue = (m_highValue & ~u8DirMask) | (u8InitialValue & u8DirMask);
        return apply_high(m_highValue, m_highDir);
    }
}

// ============================================================================
// Output control
// ============================================================================

FT232HGPIO::Status FT232HGPIO::write(Bank eBank, uint8_t u8Value)
{
    if (eBank == Bank::Low) {
        m_lowValue = u8Value;
        return apply_low(m_lowValue, m_lowDir);
    } else {
        m_highValue = u8Value;
        return apply_high(m_highValue, m_highDir);
    }
}

FT232HGPIO::Status FT232HGPIO::set_pins(Bank eBank, uint8_t u8PinMask)
{
    if (eBank == Bank::Low) {
        m_lowValue |= u8PinMask;
        return apply_low(m_lowValue, m_lowDir);
    } else {
        m_highValue |= u8PinMask;
        return apply_high(m_highValue, m_highDir);
    }
}

FT232HGPIO::Status FT232HGPIO::clear_pins(Bank eBank, uint8_t u8PinMask)
{
    if (eBank == Bank::Low) {
        m_lowValue &= static_cast<uint8_t>(~u8PinMask);
        return apply_low(m_lowValue, m_lowDir);
    } else {
        m_highValue &= static_cast<uint8_t>(~u8PinMask);
        return apply_high(m_highValue, m_highDir);
    }
}

FT232HGPIO::Status FT232HGPIO::toggle_pins(Bank eBank, uint8_t u8PinMask)
{
    if (eBank == Bank::Low) {
        m_lowValue ^= u8PinMask;
        return apply_low(m_lowValue, m_lowDir);
    } else {
        m_highValue ^= u8PinMask;
        return apply_high(m_highValue, m_highDir);
    }
}

// ============================================================================
// Input reading
// ============================================================================

FT232HGPIO::Status FT232HGPIO::read(Bank eBank, uint8_t &u8Value)
{
    uint8_t cmd[2];
    cmd[0] = (eBank == Bank::Low) ? MPSSE_GET_BITS_LOW : MPSSE_GET_BITS_HIGH;
    cmd[1] = MPSSE_SEND_IMMEDIATE;

    auto s = mpsse_write(cmd, 2);
    if (s != Status::SUCCESS) {
        return s;
    }

    size_t got = 0;
    return mpsse_read(&u8Value, 1, FT232H_READ_DEFAULT_TIMEOUT, got);
}

FT232HGPIO::Status FT232HGPIO::read_pins(Bank eBank, uint8_t u8PinMask, uint8_t &u8Value)
{
    uint8_t raw = 0;
    auto s      = read(eBank, raw);
    u8Value       = raw & u8PinMask;
    return s;
}
