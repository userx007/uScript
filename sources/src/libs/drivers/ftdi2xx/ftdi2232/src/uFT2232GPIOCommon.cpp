#include "FT2232Base.hpp"
#include "uFT2232GPIO.hpp"
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

#define LT_HDR  "FT2232_GPIO |"
#define LOG_HDR LOG_STRING(LT_HDR)

// ============================================================================
// open / close
// ============================================================================

FT2232GPIO::Status FT2232GPIO::open(const GpioConfig &sConfig, uint8_t u8DeviceIndex)
{
    Status s = open_device(sConfig.variant, sConfig.channel, u8DeviceIndex);
    if (s != Status::SUCCESS) {
        return s;
    }

    s = configure_mpsse_gpio(sConfig);
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("MPSSE GPIO init failed"));
        FT2232Base::close();
        return s;
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("FT2232 GPIO opened: variant=");
              LOG_UINT32(static_cast<uint8_t>(sConfig.variant));
              LOG_STRING("ch="); LOG_UINT32(static_cast<uint8_t>(sConfig.channel));
              LOG_STRING("idx="); LOG_UINT32(u8DeviceIndex);
              LOG_STRING("lowDir="); LOG_HEX8(sConfig.lowDirMask);
              LOG_STRING("highDir="); LOG_HEX8(sConfig.highDirMask));

    return Status::SUCCESS;
}

FT2232GPIO::Status FT2232GPIO::close()
{
    if (is_open()) {
        // Drive all output pins low before releasing the handle
        (void)apply_low(0x00u, m_lowDir);
        (void)apply_high(0x00u, m_highDir);
    }
    return FT2232Base::close();
}

// ============================================================================
// MPSSE CONFIGURATION
// ============================================================================

FT2232GPIO::Status FT2232GPIO::configure_mpsse_gpio(const GpioConfig &sConfig)
{
    m_lowValue  = sConfig.lowValue;
    m_lowDir    = sConfig.lowDirMask;
    m_highValue = sConfig.highValue;
    m_highDir   = sConfig.highDirMask;

    // MPSSE sync
    {
        const uint8_t s[1] = {0xAA};
        (void)mpsse_write(s, 1);
        uint8_t e[2] = {0};
        size_t g     = 0;
        (void)mpsse_read(e, 2, 200, g);
    }

    std::vector<uint8_t> init;
    init.reserve(16);

    push_clock_init(init); // DIS_DIV5 for FT2232H; nothing for FT2232D
    init.push_back(MPSSE_DIS_ADAPTIVE);
    init.push_back(MPSSE_DIS_3PHASE);
    init.push_back(MPSSE_LOOPBACK_OFF);

    // Apply initial pin state — both banks
    init.push_back(MPSSE_SET_BITS_LOW);
    init.push_back(m_lowValue);
    init.push_back(m_lowDir);

    init.push_back(MPSSE_SET_BITS_HIGH);
    init.push_back(m_highValue);
    init.push_back(m_highDir);

    init.push_back(MPSSE_SEND_IMMEDIATE);

    Status s = mpsse_write(init.data(), init.size());
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("configure_mpsse_gpio: init failed"));
    }
    return s;
}

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

FT2232GPIO::Status FT2232GPIO::apply_low(uint8_t u8Value, uint8_t u8Dir) const
{
    const uint8_t cmd[3] = {MPSSE_SET_BITS_LOW, u8Value, u8Dir};
    return mpsse_write(cmd, sizeof(cmd));
}

FT2232GPIO::Status FT2232GPIO::apply_high(uint8_t u8Value, uint8_t u8Dir) const
{
    const uint8_t cmd[3] = {MPSSE_SET_BITS_HIGH, u8Value, u8Dir};
    return mpsse_write(cmd, sizeof(cmd));
}

// ============================================================================
// DIRECTION CONTROL
// ============================================================================

FT2232GPIO::Status FT2232GPIO::set_direction(Bank eBank, uint8_t u8DirMask, uint8_t u8InitialValue)
{
    if (!is_open()) {
        return Status::PORT_ACCESS;
    }

    if (eBank == Bank::Low) {
        const uint8_t newOut = static_cast<uint8_t>(u8DirMask & ~m_lowDir);
        m_lowValue           = static_cast<uint8_t>((m_lowValue & ~newOut) | (u8InitialValue & newOut));
        m_lowDir             = u8DirMask;
        return apply_low(m_lowValue, m_lowDir);
    } else {
        const uint8_t newOut = static_cast<uint8_t>(u8DirMask & ~m_highDir);
        m_highValue          = static_cast<uint8_t>((m_highValue & ~newOut) | (u8InitialValue & newOut));
        m_highDir            = u8DirMask;
        return apply_high(m_highValue, m_highDir);
    }
}

// ============================================================================
// OUTPUT CONTROL
// ============================================================================

FT2232GPIO::Status FT2232GPIO::write(Bank eBank, uint8_t u8Value)
{
    if (!is_open()) {
        return Status::PORT_ACCESS;
    }

    if (eBank == Bank::Low) {
        m_lowValue = u8Value;
        return apply_low(m_lowValue, m_lowDir);
    } else {
        m_highValue = u8Value;
        return apply_high(m_highValue, m_highDir);
    }
}

FT2232GPIO::Status FT2232GPIO::set_pins(Bank eBank, uint8_t u8PinMask)
{
    if (!is_open()) {
        return Status::PORT_ACCESS;
    }
    return (eBank == Bank::Low)
               ? write(Bank::Low, static_cast<uint8_t>(m_lowValue | u8PinMask))
               : write(Bank::High, static_cast<uint8_t>(m_highValue | u8PinMask));
}

FT2232GPIO::Status FT2232GPIO::clear_pins(Bank eBank, uint8_t u8PinMask)
{
    if (!is_open()) {
        return Status::PORT_ACCESS;
    }
    return (eBank == Bank::Low)
               ? write(Bank::Low, static_cast<uint8_t>(m_lowValue & ~u8PinMask))
               : write(Bank::High, static_cast<uint8_t>(m_highValue & ~u8PinMask));
}

FT2232GPIO::Status FT2232GPIO::toggle_pins(Bank eBank, uint8_t u8PinMask)
{
    if (!is_open()) {
        return Status::PORT_ACCESS;
    }
    return (eBank == Bank::Low)
               ? write(Bank::Low, static_cast<uint8_t>(m_lowValue ^ u8PinMask))
               : write(Bank::High, static_cast<uint8_t>(m_highValue ^ u8PinMask));
}

// ============================================================================
// INPUT READING
// ============================================================================

FT2232GPIO::Status FT2232GPIO::read(Bank eBank, uint8_t &u8Value)
{
    if (!is_open()) {
        return Status::PORT_ACCESS;
    }

    u8Value                = 0;

    const uint8_t getCmd = (eBank == Bank::Low) ? MPSSE_GET_BITS_LOW : MPSSE_GET_BITS_HIGH;
    const uint8_t cmd[2] = {getCmd, MPSSE_SEND_IMMEDIATE};

    Status s             = mpsse_write(cmd, sizeof(cmd));
    if (s != Status::SUCCESS) {
        return s;
    }

    size_t got = 0;
    s          = mpsse_read(&u8Value, 1, 200, got);
    if (s != Status::SUCCESS || got == 0) {
        return Status::READ_ERROR;
    }

    return Status::SUCCESS;
}

FT2232GPIO::Status FT2232GPIO::read_pins(Bank eBank, uint8_t u8PinMask, uint8_t &u8Value)
{
    if (!is_open()) {
        return Status::PORT_ACCESS;
    }

    uint8_t raw = 0;
    Status s    = read(eBank, raw);
    if (s == Status::SUCCESS) {
        u8Value = static_cast<uint8_t>(raw & u8PinMask);
    }
    return s;
}
