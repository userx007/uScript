// FT4232Base.hpp is the source of all MPSSE_* opcode constants
// (MPSSE_SET_BITS_LOW, MPSSE_GET_BITS_LOW, MPSSE_DIS_DIV5, etc.).
// They are defined as protected static constexpr members of FT4232Base
// and are accessible here through FT4232I2C's inheritance chain.
// Included explicitly so this file's dependencies are self-documenting.
#include "FT4232Base.hpp"
#include "uFT4232I2C.hpp"
#include "uLogger.hpp"

#include <algorithm>
#include <vector>
#include <thread>
#include <chrono>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "FT4232_I2C  |"
#define LOG_HDR    LOG_STRING(LT_HDR)


// ============================================================================
// MPSSE I²C bit-bang primer
// ============================================================================
//
// Every I²C state transition is expressed as a sequence of SET_BITS_LOW
// (0x80) commands, each 3 bytes: { 0x80, value, direction }.
//
// Pin layout (ADBUS low byte):
//   bit 0 — SCL  : always output  (direction bit always 1)
//   bit 1 — SDA_O: output-low when driving; input (hi-Z) when releasing
//   bit 2 — SDA_I: always input; reads actual SDA wire level
//
// "Releasing" SDA means setting direction bit 1 to 0 (input), which lets
// the external pull-up resistor pull SDA high. This emulates open-drain.
//
// Command batching:
//   Write operations: all SET_BITS_LOW commands for a full byte are built
//   into a single std::vector<uint8_t> and sent in one mpsse_write() call.
//
//   Read operations: GET_BITS_LOW (0x81) is interleaved with SEND_IMMEDIATE
//   (0x87) in the batch. Every GET_BITS_LOW queues one response byte.
//   All 8 response bytes for a received byte are fetched in one mpsse_read().
//
// Clock timing note:
//   At USB full-speed (12 Mbps) the round-trip latency is typically 1–2 ms.
//   The MPSSE clock divisor controls the TCK period _within_ a single USB
//   packet; it does not affect packet-to-packet latency. For batch-style
//   bit-bang the effective I²C speed is limited by USB latency rather than
//   the divisor, but configuring a sensible divisor is good practice.


// ============================================================================
// open / close
// ============================================================================

FT4232I2C::Status FT4232I2C::open(uint8_t  u8I2CAddress,
                                   uint32_t u32ClockHz,
                                   Channel  channel,
                                   uint8_t  u8DeviceIndex)
{
    if (u32ClockHz == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid clock speed"));
        return Status::INVALID_PARAM;
    }

    Status s = open_device(channel, u8DeviceIndex);
    if (s != Status::SUCCESS) {
        return s;
    }

    m_u8I2CAddress = u8I2CAddress;

    s = configure_mpsse_i2c(u32ClockHz);
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("MPSSE I2C init failed, clock="); LOG_UINT32(u32ClockHz));
        FT4232Base::close();
        return s;
    }

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("FT4232H I2C opened: ch="); LOG_UINT32(static_cast<uint8_t>(channel));
              LOG_STRING("idx="); LOG_UINT32(u8DeviceIndex);
              LOG_STRING("addr=0x"); LOG_HEX8(u8I2CAddress);
              LOG_STRING("clock="); LOG_UINT32(u32ClockHz));

    return Status::SUCCESS;
}


FT4232I2C::Status FT4232I2C::close()
{
    if (is_open()) {
        // Best-effort STOP to leave the bus in a clean state.
        // Ignore errors — we are closing regardless.
        (void)i2c_stop();
    }
    return FT4232Base::close();
}


// ============================================================================
// PUBLIC UNIFIED INTERFACE  (ICommDriver)
// ============================================================================

FT4232I2C::ReadResult FT4232I2C::tout_read(uint32_t u32ReadTimeout,
                                            std::span<uint8_t> buffer,
                                            const ReadOptions& options) const
{
    ReadResult result;

    if (!is_open()) {
        result.status = Status::PORT_ACCESS;
        return result;
    }

    uint32_t timeout = (u32ReadTimeout == 0) ? FT4232_READ_DEFAULT_TIMEOUT : u32ReadTimeout;

    switch (options.mode)
    {
        // ------------------------------------------------------------------
        case ReadMode::Exact:
        {
            size_t bytesRead = 0;
            result.status           = i2c_read(buffer, bytesRead, timeout);
            result.bytes_read       = bytesRead;
            result.found_terminator = false;
            break;
        }

        // ------------------------------------------------------------------
        case ReadMode::UntilDelimiter:
        {
            if (buffer.size() < 2) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("Buffer too small for delimiter + null terminator"));
                result.status = Status::INVALID_PARAM;
                break;
            }

            size_t pos    = 0;
            result.status = Status::READ_TIMEOUT;

            while (pos < buffer.size() - 1) {
                uint8_t byte = 0;
                size_t  got  = 0;
                Status  s    = i2c_read(std::span<uint8_t>(&byte, 1), got, timeout);

                if (s != Status::SUCCESS || got == 0) {
                    result.status = s;
                    break;
                }

                if (byte == options.delimiter) {
                    buffer[pos]             = '\0';
                    result.found_terminator = true;
                    result.status           = Status::SUCCESS;
                    break;
                }

                buffer[pos++] = byte;
            }

            if (pos == buffer.size() - 1 && result.status == Status::READ_TIMEOUT) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Buffer full before delimiter found"));
                result.status = Status::BUFFER_OVERFLOW;
            }

            result.bytes_read = pos;
            break;
        }

        // ------------------------------------------------------------------
        case ReadMode::UntilToken:
        {
            if (options.token.empty()) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Empty token"));
                result.status = Status::INVALID_PARAM;
                break;
            }

            const auto& token = options.token;

            // Build KMP failure table
            std::vector<int> lps(token.size(), 0);
            for (size_t i = 1, len = 0; i < token.size(); ) {
                if (token[i] == token[len]) {
                    lps[i++] = static_cast<int>(++len);
                } else if (len != 0) {
                    len = static_cast<size_t>(lps[len - 1]);
                } else {
                    lps[i++] = 0;
                }
            }

            size_t matched = 0;
            result.status  = Status::READ_TIMEOUT;

            while (true) {
                uint8_t byte = 0;
                size_t  got  = 0;
                Status  s    = i2c_read(std::span<uint8_t>(&byte, 1), got, timeout);

                if (s != Status::SUCCESS || got == 0) {
                    result.status = s;
                    break;
                }

                while (matched > 0 && byte != token[matched]) {
                    matched = static_cast<size_t>(lps[matched - 1]);
                }
                if (byte == token[matched]) {
                    ++matched;
                }
                if (matched == token.size()) {
                    result.found_terminator = true;
                    result.status           = Status::SUCCESS;
                    break;
                }
            }

            result.bytes_read = 0;
            break;
        }

        default:
            result.status = Status::INVALID_PARAM;
            break;
    }

    return result;
}


FT4232I2C::WriteResult FT4232I2C::tout_write(uint32_t u32WriteTimeout,
                                              std::span<const uint8_t> buffer) const
{
    WriteResult result;

    if (!is_open()) {
        result.status = Status::PORT_ACCESS;
        return result;
    }

    uint32_t timeout      = (u32WriteTimeout == 0) ? FT4232_WRITE_DEFAULT_TIMEOUT : u32WriteTimeout;
    size_t   bytesWritten = 0;

    result.status        = i2c_write(buffer, timeout, bytesWritten);
    result.bytes_written = bytesWritten;

    return result;
}


// ============================================================================
// MPSSE CONFIGURATION
// ============================================================================

FT4232I2C::Status FT4232I2C::configure_mpsse_i2c(uint32_t u32ClockHz) const
{
    // Synchronise the MPSSE by sending a bad command (0xAA).
    // The chip echoes back 0xFA 0xAA to indicate it is in command mode.
    // This clears any leftover state from a previous session.
    {
        const uint8_t sync[1] = { 0xAA };
        Status s = mpsse_write(sync, sizeof(sync));
        if (s != Status::SUCCESS) return s;

        uint8_t echo[2]   = { 0 };
        size_t  got       = 0;
        s = mpsse_read(echo, 2, 200, got);
        // Non-fatal: some platforms return the echo, others don't in all modes.
        (void)s; (void)got;
    }

    // Clock divisor for bit-bang I²C (no 3-phase):
    //   TCK = 60 MHz / ((1 + divisor) * 2)
    //   → divisor = (30,000,000 / clockHz) - 1
    // Clamp to [0, 0xFFFF].
    uint32_t divisor = (u32ClockHz > 0 && u32ClockHz < 30000000u)
                       ? (30000000u / u32ClockHz) - 1u
                       : 0u;
    divisor = std::min(divisor, static_cast<uint32_t>(0xFFFFu));

    const uint8_t divLow  = static_cast<uint8_t>( divisor       & 0xFFu);
    const uint8_t divHigh = static_cast<uint8_t>((divisor >> 8) & 0xFFu);

    // Build the MPSSE initialisation sequence:
    //   1. Use 60 MHz base clock (DIS_DIV5)
    //   2. Disable adaptive clocking (not needed for I²C bit-bang)
    //   3. Disable 3-phase clocking (not used in bit-bang mode)
    //   4. Disable loopback
    //   5. Set clock divisor
    //   6. Drive all ADBUS pins low as initial safe state, then release bus

    std::vector<uint8_t> init;
    init.reserve(32);

    // ── Clock and feature configuration ─────────────────────────────────────
    init.push_back(MPSSE_DIS_DIV5);      // 60 MHz base clock
    init.push_back(MPSSE_DIS_ADAPTIVE);  // No adaptive clocking
    init.push_back(MPSSE_DIS_3PHASE);    // No 3-phase clocking
    init.push_back(MPSSE_LOOPBACK_OFF);  // No internal loopback

    // ── Clock divisor ────────────────────────────────────────────────────────
    init.push_back(MPSSE_SET_CLK_DIV);
    init.push_back(divLow);
    init.push_back(divHigh);

    // ── Drive all ADBUS pins to 0, all as outputs (safe reset state) ─────────
    //   { SET_BITS_LOW, value=0x00, direction=0xFF }
    init.push_back(MPSSE_SET_BITS_LOW);
    init.push_back(0x00u);
    init.push_back(0xFFu);

    // ── Release I²C bus to idle: SCL=H (out), SDA=H (input/float) ────────────
    //   SCL = output high:  direction bit 0 = 1, value bit 0 = 1
    //   SDA = input:        direction bit 1 = 0, value bit 1 = x
    init.push_back(MPSSE_SET_BITS_LOW);
    init.push_back(I2C_SCL);           // value:     SCL=1, SDA_O=0 (don't care, pin is input)
    init.push_back(DIR_SCL_ONLY);      // direction: SCL=output, SDA_O=input

    // ── SEND_IMMEDIATE to flush the init sequence to the chip ────────────────
    init.push_back(MPSSE_SEND_IMMEDIATE);

    Status s = mpsse_write(init.data(), init.size());
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("configure_mpsse_i2c: init sequence failed"));
    }

    return s;
}


// ============================================================================
// MPSSE PIN-STATE HELPERS
// ============================================================================

/**
 * @brief Append a SET_BITS_LOW command (3 bytes) to a command buffer
 *
 * @param buf           Target command buffer
 * @param scl           Desired SCL level (true=high)
 * @param drive_sda_low true  → drive SDA_O low  (direction=output, value=0)
 *                      false → release SDA       (direction=input,  float=high)
 */
void FT4232I2C::push_pin_state(std::vector<uint8_t>& buf,
                                bool scl,
                                bool drive_sda_low)
{
    const uint8_t value = scl ? I2C_SCL : 0x00u;       // SDA_O value is 0; direction controls drive
    const uint8_t dir   = drive_sda_low
                          ? DIR_SCL_SDA_OUT             // SCL=out, SDA_O=out (drive low)
                          : DIR_SCL_ONLY;               // SCL=out, SDA_O=input (float high)

    buf.push_back(MPSSE_SET_BITS_LOW);
    buf.push_back(value);
    buf.push_back(dir);
}


/**
 * @brief Append GET_BITS_LOW + SEND_IMMEDIATE (2 bytes) to a command buffer
 *
 * When the MPSSE processes GET_BITS_LOW it places 1 byte (the current ADBUS
 * pin state) into the device RX FIFO. SEND_IMMEDIATE forces it over USB
 * without waiting for a full USB packet. Call mpsse_read(1 byte) after
 * sending the buffer to receive it. Bit 2 of the returned byte = SDA_I.
 */
void FT4232I2C::push_read_sda(std::vector<uint8_t>& buf)
{
    buf.push_back(MPSSE_GET_BITS_LOW);
    buf.push_back(MPSSE_SEND_IMMEDIATE);
}


// ============================================================================
// I²C BUS CONDITIONS
// ============================================================================

/**
 * @brief Send I²C START condition
 *
 * Pin transitions (idle → start → data-phase-ready):
 *   SCL=H, SDA=H (idle)
 *   SCL=H, SDA=L (SDA falls while SCL high → START)
 *   SCL=L, SDA=L (SCL falls  → data phase begins)
 */
FT4232I2C::Status FT4232I2C::i2c_start() const
{
    std::vector<uint8_t> cmd;
    cmd.reserve(9);

    push_pin_state(cmd, true,  false); // SCL=H, SDA=H (idle, ensure)
    push_pin_state(cmd, true,  true ); // SCL=H, SDA=L → START
    push_pin_state(cmd, false, true ); // SCL=L, SDA=L → data phase

    return mpsse_write(cmd.data(), cmd.size());
}


/**
 * @brief Send I²C Repeated START condition
 *
 * Used between a write phase and a read phase in the same transaction.
 * Pin transitions (SCL is already low from previous data bit):
 *   SCL=L, SDA=H (release SDA)
 *   SCL=H, SDA=H (SCL rises while SDA is high — safe)
 *   SCL=H, SDA=L (SDA falls while SCL high → Repeated START)
 *   SCL=L, SDA=L (SCL falls → data phase)
 */
FT4232I2C::Status FT4232I2C::i2c_repeated_start() const
{
    std::vector<uint8_t> cmd;
    cmd.reserve(12);

    push_pin_state(cmd, false, false); // SCL=L, SDA=H (release SDA)
    push_pin_state(cmd, true,  false); // SCL=H, SDA=H
    push_pin_state(cmd, true,  true ); // SCL=H, SDA=L → Repeated START
    push_pin_state(cmd, false, true ); // SCL=L, SDA=L → data phase

    return mpsse_write(cmd.data(), cmd.size());
}


/**
 * @brief Send I²C STOP condition
 *
 * Pin transitions (SCL is low from last data bit):
 *   SCL=L, SDA=L (ensure SDA is low)
 *   SCL=H, SDA=L (SCL rises while SDA low)
 *   SCL=H, SDA=H (SDA rises while SCL high → STOP)
 */
FT4232I2C::Status FT4232I2C::i2c_stop() const
{
    std::vector<uint8_t> cmd;
    cmd.reserve(9);

    push_pin_state(cmd, false, true ); // SCL=L, SDA=L
    push_pin_state(cmd, true,  true ); // SCL=H, SDA=L
    push_pin_state(cmd, true,  false); // SCL=H, SDA=H → STOP

    return mpsse_write(cmd.data(), cmd.size());
}


// ============================================================================
// I²C BYTE TRANSFER
// ============================================================================

/**
 * @brief Write one byte MSB-first and read the ACK bit
 *
 * For each bit:
 *   SCL=L, set SDA to bit value → SCL=H (slave samples) → SCL=L
 *
 * ACK read:
 *   SCL=L, release SDA → SCL=H, GET_BITS_LOW, SEND_IMMEDIATE → SCL=L
 *
 * All SCL transitions for the 8 data bits are batched into a single
 * mpsse_write(). The ACK is read with a separate mpsse_write / mpsse_read.
 *
 * @param byte  Byte to transmit
 * @param ack   Receives true if slave pulled SDA low (ACK), false = NAK
 */
FT4232I2C::Status FT4232I2C::i2c_write_byte(uint8_t byte, bool& ack) const
{
    ack = false;

    // ── Build data-bit commands (72 bytes: 8 bits × 9 bytes each) ────────────
    std::vector<uint8_t> cmd;
    cmd.reserve(72);

    for (int bit = 7; bit >= 0; --bit) {
        bool bitVal = (byte >> bit) & 0x01u;

        push_pin_state(cmd, false, !bitVal); // SCL=L, set SDA
        push_pin_state(cmd, true,  !bitVal); // SCL=H (slave samples)
        push_pin_state(cmd, false, !bitVal); // SCL=L
    }

    Status s = mpsse_write(cmd.data(), cmd.size());
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("i2c_write_byte: data bits write failed"));
        return s;
    }

    // ── ACK bit: release SDA, clock, read, clock low ──────────────────────
    std::vector<uint8_t> ackCmd;
    ackCmd.reserve(11);

    push_pin_state(ackCmd, false, false); // SCL=L, SDA=input (release for slave ACK)
    push_pin_state(ackCmd, true,  false); // SCL=H  (slave drives ACK/NAK)
    push_read_sda (ackCmd);               // GET_BITS_LOW + SEND_IMMEDIATE
    push_pin_state(ackCmd, false, false); // SCL=L

    s = mpsse_write(ackCmd.data(), ackCmd.size());
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("i2c_write_byte: ACK cmd write failed"));
        return s;
    }

    // ── Fetch 1-byte ACK response ─────────────────────────────────────────
    uint8_t response = 0xFF;
    size_t  got      = 0;
    s = mpsse_read(&response, 1, 200, got);

    if (s != Status::SUCCESS || got == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("i2c_write_byte: ACK read failed"));
        return Status::READ_ERROR;
    }

    // ACK = SDA_I (bit 2) is LOW; NAK = HIGH
    ack = ((response & I2C_SDA_I) == 0);

    return Status::SUCCESS;
}


/**
 * @brief Read one byte MSB-first and send ACK or NAK
 *
 * For each bit:
 *   SCL=L, release SDA → SCL=H, GET_BITS_LOW, SEND_IMMEDIATE → SCL=L
 *
 * All 8 GET_BITS_LOW commands are batched (80 bytes).
 * mpsse_read() fetches all 8 response bytes in one call.
 * The received byte is reconstructed from bit 2 (SDA_I) of each response.
 *
 * @param byte     Receives the byte read from the slave
 * @param sendAck  true → drive SDA low (ACK), false → release SDA (NAK)
 */
FT4232I2C::Status FT4232I2C::i2c_read_byte(uint8_t& byte, bool sendAck) const
{
    byte = 0;

    // ── Build read commands for all 8 bits (80 bytes) ────────────────────────
    std::vector<uint8_t> cmd;
    cmd.reserve(80);

    for (int bit = 0; bit < 8; ++bit) {
        push_pin_state(cmd, false, false); // SCL=L, SDA=input
        push_pin_state(cmd, true,  false); // SCL=H (master samples SDA here)
        push_read_sda (cmd);               // GET_BITS_LOW + SEND_IMMEDIATE
        push_pin_state(cmd, false, false); // SCL=L
    }

    Status s = mpsse_write(cmd.data(), cmd.size());
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("i2c_read_byte: bit commands write failed"));
        return s;
    }

    // ── Fetch 8 response bytes (one per bit) ─────────────────────────────────
    uint8_t responses[8] = {0};
    size_t  got          = 0;
    s = mpsse_read(responses, 8, 500, got);

    if (s != Status::SUCCESS || got != 8) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("i2c_read_byte: expected 8 response bytes, got"); LOG_UINT32(got));
        return Status::READ_ERROR;
    }

    // Reconstruct byte — each response's bit 2 (SDA_I) is the data bit.
    // First iteration = MSB (bit 7).
    for (int i = 0; i < 8; ++i) {
        byte = static_cast<uint8_t>((byte << 1) | ((responses[i] >> 2) & 0x01u));
    }

    // ── ACK/NAK: drive SDA and clock one more time ────────────────────────────
    std::vector<uint8_t> ackCmd;
    ackCmd.reserve(9);

    push_pin_state(ackCmd, false, sendAck); // SCL=L, SDA = ACK(low) or NAK(float)
    push_pin_state(ackCmd, true,  sendAck); // SCL=H
    push_pin_state(ackCmd, false, sendAck); // SCL=L

    s = mpsse_write(ackCmd.data(), ackCmd.size());
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("i2c_read_byte: ACK/NAK write failed"));
    }

    return s;
}


// ============================================================================
// FULL I²C TRANSACTIONS
// ============================================================================

FT4232I2C::Status FT4232I2C::i2c_write(std::span<const uint8_t> data,
                                        uint32_t timeoutMs,
                                        size_t& bytesWritten) const
{
    (void)timeoutMs; // per-bit timeout is fixed at the MPSSE level; kept for API symmetry

    if (data.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("i2c_write: empty buffer"));
        return Status::INVALID_PARAM;
    }

    bytesWritten = 0;

    // ── START ────────────────────────────────────────────────────────────────
    Status s = i2c_start();
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("i2c_write: START failed"));
        return s;
    }

    // ── Slave address + W ────────────────────────────────────────────────────
    bool ack = false;
    s = i2c_write_byte(static_cast<uint8_t>(m_u8I2CAddress << 1), ack);
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("i2c_write: address byte failed"));
        (void)i2c_stop();
        return s;
    }
    if (!ack) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("i2c_write: NAK on address 0x"); LOG_HEX8(m_u8I2CAddress));
        (void)i2c_stop();
        return Status::WRITE_ERROR;
    }

    // ── Data bytes ───────────────────────────────────────────────────────────
    for (size_t i = 0; i < data.size(); ++i) {
        s = i2c_write_byte(data[i], ack);
        if (s != Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("i2c_write: byte failed at index"); LOG_UINT32(i));
            (void)i2c_stop();
            return s;
        }
        if (!ack) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("i2c_write: NAK at byte index"); LOG_UINT32(i));
            (void)i2c_stop();
            return Status::WRITE_ERROR;
        }
        ++bytesWritten;
    }

    // ── STOP ─────────────────────────────────────────────────────────────────
    s = i2c_stop();
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("i2c_write: STOP failed"));
    }

    return s;
}


FT4232I2C::Status FT4232I2C::i2c_read(std::span<uint8_t> data,
                                       size_t& bytesRead,
                                       uint32_t timeoutMs) const
{
    (void)timeoutMs;

    if (data.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("i2c_read: empty buffer"));
        return Status::INVALID_PARAM;
    }

    bytesRead = 0;

    // ── Repeated START ───────────────────────────────────────────────────────
    Status s = i2c_repeated_start();
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("i2c_read: repeated START failed"));
        return s;
    }

    // ── Slave address + R ────────────────────────────────────────────────────
    bool ack = false;
    s = i2c_write_byte(static_cast<uint8_t>((m_u8I2CAddress << 1) | 0x01u), ack);
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("i2c_read: address byte failed"));
        (void)i2c_stop();
        return s;
    }
    if (!ack) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("i2c_read: NAK on read address 0x"); LOG_HEX8(m_u8I2CAddress));
        (void)i2c_stop();
        return Status::READ_ERROR;
    }

    // ── Data bytes — ACK all but last, NAK the last ──────────────────────────
    for (size_t i = 0; i < data.size(); ++i) {
        const bool isLast = (i == data.size() - 1);
        s = i2c_read_byte(data[i], /*sendAck=*/!isLast);
        if (s != Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("i2c_read: byte failed at index"); LOG_UINT32(i));
            (void)i2c_stop();
            return s;
        }
        ++bytesRead;
    }

    // ── STOP ─────────────────────────────────────────────────────────────────
    s = i2c_stop();
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("i2c_read: STOP failed"));
    }

    return s;
}
