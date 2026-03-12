// FT4232Base.hpp is the source of all MPSSE_* opcode constants
// (MPSSE_SET_BITS_LOW, MPSSE_GET_BITS_LOW, MPSSE_SET_BITS_HIGH, etc.).
// They are defined as protected static constexpr members of FT4232Base
// and are accessible here through FT4232GPIO's inheritance chain.
// Included explicitly so this file's dependencies are self-documenting.
#include "FT4232Base.hpp"
#include "uFT4232GPIO.hpp"
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

#define LT_HDR     "FT4232_GPIO |"
#define LOG_HDR    LOG_STRING(LT_HDR)


// ============================================================================
// MPSSE GPIO primer
// ============================================================================
//
// GPIO control on an MPSSE channel is entirely built on two command pairs:
//
//   SET_BITS_LOW  (0x80) { value, direction } → drives ADBUS[7:0]
//   GET_BITS_LOW  (0x81)                      → queues 1 byte: ADBUS state
//
//   SET_BITS_HIGH (0x82) { value, direction } → drives ACBUS[7:0]
//   GET_BITS_HIGH (0x83)                      → queues 1 byte: ACBUS state
//
// SET_BITS format:
//   byte 0: opcode (0x80 or 0x82)
//   byte 1: value      — desired output level for each bit (output pins only)
//   byte 2: direction  — 1 = output, 0 = input
//
// GET_BITS format:
//   byte 0: opcode (0x81 or 0x83)
//   → queues 1 response byte; must follow with SEND_IMMEDIATE (0x87) to flush
//   → collect with mpsse_read(1 byte)
//
// No clock divisor is configured for GPIO-only use — MPSSE clock commands
// affect only TCK and are irrelevant when no serial shift commands are used.
// The MPSSE sync handshake (bad-opcode echo) is still performed at open()
// to flush any leftover state from a previous session.
//
// Output value caching:
//   m_lowValue / m_highValue track the last value sent to SET_BITS_LOW/HIGH.
//   This enables read-modify-write operations (set_pins, clear_pins, toggle_pins)
//   without a GET_BITS round-trip, which saves a USB latency cycle (~1 ms).
//   read() always performs a live GET_BITS to reflect actual pin state.


// ============================================================================
// open / close
// ============================================================================

FT4232GPIO::Status FT4232GPIO::open(const GpioConfig& config, uint8_t u8DeviceIndex)
{
    Status s = open_device(config.channel, u8DeviceIndex);
    if (s != Status::SUCCESS) {
        return s;
    }

    s = configure_mpsse_gpio(config);
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("MPSSE GPIO init failed"));
        FT4232Base::close();
        return s;
    }

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("FT4232H GPIO opened: ch=");
              LOG_UINT32(static_cast<uint8_t>(config.channel));
              LOG_STRING("idx="); LOG_UINT32(u8DeviceIndex);
              LOG_STRING("lowDir=0x");  LOG_HEX8(config.lowDirMask);
              LOG_STRING("highDir=0x"); LOG_HEX8(config.highDirMask));

    return Status::SUCCESS;
}


FT4232GPIO::Status FT4232GPIO::close()
{
    if (is_open()) {
        // Drive all output pins low before closing — leaves the bus in a
        // safe, defined state rather than floating at whatever level they
        // happened to be driven to.
        (void)apply_low (0x00u, m_lowDir);
        (void)apply_high(0x00u, m_highDir);
    }
    return FT4232Base::close();
}


// ============================================================================
// MPSSE CONFIGURATION
// ============================================================================

FT4232GPIO::Status FT4232GPIO::configure_mpsse_gpio(const GpioConfig& config)
{
    // ── Cache initial state ───────────────────────────────────────────────────
    m_lowValue  = config.lowValue;
    m_lowDir    = config.lowDirMask;
    m_highValue = config.highValue;
    m_highDir   = config.highDirMask;

    // ── MPSSE synchronisation (bad-opcode echo) ───────────────────────────────
    // Sending 0xAA causes the MPSSE to echo 0xFA 0xAA, confirming it is in
    // command mode and flushing any leftover bytes from a previous session.
    {
        const uint8_t sync[1] = { 0xAA };
        (void)mpsse_write(sync, 1);
        uint8_t echo[2] = { 0 };
        size_t  got     = 0;
        (void)mpsse_read(echo, 2, 200, got);
    }

    // ── Build MPSSE init sequence ─────────────────────────────────────────────
    std::vector<uint8_t> init;
    init.reserve(16);

    init.push_back(MPSSE_DIS_DIV5);      // 60 MHz base clock (consistent with I2C/SPI)
    init.push_back(MPSSE_DIS_ADAPTIVE);  // No adaptive clocking
    init.push_back(MPSSE_DIS_3PHASE);    // No 3-phase clocking
    init.push_back(MPSSE_LOOPBACK_OFF);  // No internal loopback

    // ── Apply initial pin state — both banks ──────────────────────────────────
    // Low bank (ADBUS)
    init.push_back(MPSSE_SET_BITS_LOW);
    init.push_back(m_lowValue);
    init.push_back(m_lowDir);

    // High bank (ACBUS)
    init.push_back(MPSSE_SET_BITS_HIGH);
    init.push_back(m_highValue);
    init.push_back(m_highDir);

    init.push_back(MPSSE_SEND_IMMEDIATE);

    Status s = mpsse_write(init.data(), init.size());
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("configure_mpsse_gpio: init sequence failed"));
    }

    return s;
}


// ============================================================================
// INTERNAL PIN-STATE HELPERS
// ============================================================================

FT4232GPIO::Status FT4232GPIO::apply_low(uint8_t value, uint8_t dir) const
{
    const uint8_t cmd[3] = { MPSSE_SET_BITS_LOW, value, dir };
    return mpsse_write(cmd, sizeof(cmd));
}


FT4232GPIO::Status FT4232GPIO::apply_high(uint8_t value, uint8_t dir) const
{
    const uint8_t cmd[3] = { MPSSE_SET_BITS_HIGH, value, dir };
    return mpsse_write(cmd, sizeof(cmd));
}


// ============================================================================
// DIRECTION CONTROL
// ============================================================================

FT4232GPIO::Status FT4232GPIO::set_direction(Bank    bank,
                                             uint8_t dirMask,
                                             uint8_t initialValue)
{
    if (!is_open()) return Status::PORT_ACCESS;

    if (bank == Bank::Low) {
        // Pins switching from input to output are driven to initialValue.
        // Pins already output keep their current cached value.
        const uint8_t newOutputPins = static_cast<uint8_t>(dirMask & ~m_lowDir);
        m_lowValue = static_cast<uint8_t>(
            (m_lowValue & ~newOutputPins) | (initialValue & newOutputPins));
        m_lowDir = dirMask;

        Status s = apply_low(m_lowValue, m_lowDir);
        if (s != Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("set_direction Low failed, dir=0x"); LOG_HEX8(dirMask));
        }
        return s;
    }
    else {
        const uint8_t newOutputPins = static_cast<uint8_t>(dirMask & ~m_highDir);
        m_highValue = static_cast<uint8_t>(
            (m_highValue & ~newOutputPins) | (initialValue & newOutputPins));
        m_highDir = dirMask;

        Status s = apply_high(m_highValue, m_highDir);
        if (s != Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("set_direction High failed, dir=0x"); LOG_HEX8(dirMask));
        }
        return s;
    }
}


// ============================================================================
// OUTPUT CONTROL
// ============================================================================

FT4232GPIO::Status FT4232GPIO::write(Bank bank, uint8_t value)
{
    if (!is_open()) return Status::PORT_ACCESS;

    if (bank == Bank::Low) {
        m_lowValue = value;
        Status s = apply_low(m_lowValue, m_lowDir);
        if (s != Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("write Low failed, value=0x"); LOG_HEX8(value));
        }
        return s;
    }
    else {
        m_highValue = value;
        Status s = apply_high(m_highValue, m_highDir);
        if (s != Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("write High failed, value=0x"); LOG_HEX8(value));
        }
        return s;
    }
}


FT4232GPIO::Status FT4232GPIO::set_pins(Bank bank, uint8_t pinMask)
{
    if (!is_open()) return Status::PORT_ACCESS;

    if (bank == Bank::Low) {
        return write(Bank::Low,  static_cast<uint8_t>(m_lowValue  | pinMask));
    } else {
        return write(Bank::High, static_cast<uint8_t>(m_highValue | pinMask));
    }
}


FT4232GPIO::Status FT4232GPIO::clear_pins(Bank bank, uint8_t pinMask)
{
    if (!is_open()) return Status::PORT_ACCESS;

    if (bank == Bank::Low) {
        return write(Bank::Low,  static_cast<uint8_t>(m_lowValue  & ~pinMask));
    } else {
        return write(Bank::High, static_cast<uint8_t>(m_highValue & ~pinMask));
    }
}


FT4232GPIO::Status FT4232GPIO::toggle_pins(Bank bank, uint8_t pinMask)
{
    if (!is_open()) return Status::PORT_ACCESS;

    if (bank == Bank::Low) {
        return write(Bank::Low,  static_cast<uint8_t>(m_lowValue  ^ pinMask));
    } else {
        return write(Bank::High, static_cast<uint8_t>(m_highValue ^ pinMask));
    }
}


// ============================================================================
// INPUT READING
// ============================================================================

FT4232GPIO::Status FT4232GPIO::read(Bank bank, uint8_t& value)
{
    if (!is_open()) return Status::PORT_ACCESS;

    value = 0;

    // Build: GET_BITS + SEND_IMMEDIATE → fetch 1 response byte
    const uint8_t getCmd  = (bank == Bank::Low) ? MPSSE_GET_BITS_LOW : MPSSE_GET_BITS_HIGH;
    const uint8_t cmd[2]  = { getCmd, MPSSE_SEND_IMMEDIATE };

    Status s = mpsse_write(cmd, sizeof(cmd));
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("read: GET_BITS cmd failed, bank=");
                  LOG_UINT32(static_cast<uint8_t>(bank)));
        return s;
    }

    size_t got = 0;
    s = mpsse_read(&value, 1, 200, got);
    if (s != Status::SUCCESS || got == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("read: mpsse_read failed, bank=");
                  LOG_UINT32(static_cast<uint8_t>(bank)));
        return Status::READ_ERROR;
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("read: bank="); LOG_UINT32(static_cast<uint8_t>(bank));
              LOG_STRING("value=0x"); LOG_HEX8(value));

    return Status::SUCCESS;
}


FT4232GPIO::Status FT4232GPIO::read_pins(Bank bank, uint8_t pinMask, uint8_t& value)
{
    if (!is_open()) return Status::PORT_ACCESS;

    uint8_t raw = 0;
    Status s = read(bank, raw);
    if (s == Status::SUCCESS) {
        value = static_cast<uint8_t>(raw & pinMask);
    }
    return s;
}
