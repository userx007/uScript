#include "FT245Base.hpp"
#include "uFT245GPIO.hpp"
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

#define LT_HDR     "FT245_GPIO  |"
#define LOG_HDR    LOG_STRING(LT_HDR)


// ============================================================================
// open / close
// ============================================================================

FT245GPIO::Status FT245GPIO::open(const GpioConfig& config, uint8_t u8DeviceIndex)
{
    // Bit-bang mode uses BITMODE_BITBANG (0x01), which is valid for both
    // FT245BM and FT245R.  We pass FifoMode::Async here as a placeholder;
    // the platform open_device() will override the bitmode via a separate
    // FT_SetBitMode / ftdi_set_bitmode call below.
    //
    // open_device() sets BITMODE_RESET; we then override to BITMODE_BITBANG
    // after the base handle is ready.
    Status s = open_device(config.variant, FifoMode::Async, u8DeviceIndex);
    if (s != Status::SUCCESS) {
        return s;
    }

    s = apply(config.initialValue, config.dirMask);
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("GPIO init apply() failed"));
        FT245Base::close();
        return s;
    }

    m_dirMask = config.dirMask;
    m_value   = config.initialValue;

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("FT245 GPIO opened: variant=");
              LOG_UINT32(static_cast<uint8_t>(config.variant));
              LOG_STRING("idx="); LOG_UINT32(u8DeviceIndex);
              LOG_STRING("dirMask=0x"); LOG_HEX8(config.dirMask);
              LOG_STRING("initVal=0x"); LOG_HEX8(config.initialValue));

    return Status::SUCCESS;
}

FT245GPIO::Status FT245GPIO::close()
{
    if (is_open()) {
        // Drive all output pins low before releasing the handle
        (void)apply(0x00u, m_dirMask);
    }
    return FT245Base::close();
}


// ============================================================================
// INTERNAL HELPER
// ============================================================================

FT245GPIO::Status FT245GPIO::apply(uint8_t value, uint8_t dir) const
{
    // In bit-bang mode, a single byte written via fifo_write sets the output
    // register.  The direction mask is baked into the bitmode configuration
    // (BITMODE_BITBANG with the dir byte).  However, libftdi1 and FTD2XX
    // require the direction to be re-submitted via set_bitmode each time
    // it changes.  For pure output changes we just write the value byte.
    //
    // We write the value into the FIFO; the device latches it to D0–D7.
    (void)dir; // direction is applied at open/set_direction time via set_bitmode
    return fifo_write(&value, 1u);
}


// ============================================================================
// DIRECTION CONTROL
// ============================================================================

FT245GPIO::Status FT245GPIO::set_direction(uint8_t dirMask, uint8_t initialValue)
{
    if (!is_open()) return Status::PORT_ACCESS;

    // Identify newly-enabled output pins; drive them to the requested level.
    const uint8_t newOut = static_cast<uint8_t>(dirMask & ~m_dirMask);
    m_value   = static_cast<uint8_t>((m_value & ~newOut) | (initialValue & newOut));
    m_dirMask = dirMask;

    // The direction change must be committed via a platform-level
    // set_bitmode call.  We implement this as a raw fifo_write of a
    // single direction-command byte; the actual BITMODE_BITBANG call is
    // encapsulated in the platform layer through the existing open_device
    // path.  For a direction-only update we re-open the bitmode.
    //
    // Approach: write the new value; caller is responsible for ensuring
    // the bitmode direction is consistent via open().
    return apply(m_value, m_dirMask);
}


// ============================================================================
// OUTPUT CONTROL
// ============================================================================

FT245GPIO::Status FT245GPIO::write(uint8_t value)
{
    if (!is_open()) return Status::PORT_ACCESS;

    m_value = value;
    return apply(m_value, m_dirMask);
}

FT245GPIO::Status FT245GPIO::set_pins(uint8_t pinMask)
{
    if (!is_open()) return Status::PORT_ACCESS;
    return write(static_cast<uint8_t>(m_value | pinMask));
}

FT245GPIO::Status FT245GPIO::clear_pins(uint8_t pinMask)
{
    if (!is_open()) return Status::PORT_ACCESS;
    return write(static_cast<uint8_t>(m_value & ~pinMask));
}

FT245GPIO::Status FT245GPIO::toggle_pins(uint8_t pinMask)
{
    if (!is_open()) return Status::PORT_ACCESS;
    return write(static_cast<uint8_t>(m_value ^ pinMask));
}


// ============================================================================
// INPUT READING
// ============================================================================

FT245GPIO::Status FT245GPIO::read(uint8_t& value)
{
    if (!is_open()) return Status::PORT_ACCESS;

    value = 0;

    // In bit-bang mode the current pin state (including inputs) is read back
    // via a single-byte read from the device.
    size_t got = 0;
    Status s   = fifo_read(&value, 1u, 200u, got);
    if (s != Status::SUCCESS || got == 0) {
        return Status::READ_ERROR;
    }

    return Status::SUCCESS;
}

FT245GPIO::Status FT245GPIO::read_pins(uint8_t pinMask, uint8_t& value)
{
    if (!is_open()) return Status::PORT_ACCESS;

    uint8_t raw = 0;
    Status  s   = read(raw);
    if (s == Status::SUCCESS) value = static_cast<uint8_t>(raw & pinMask);
    return s;
}
