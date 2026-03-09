/*
 * FT232H I2C – cross-platform MPSSE bit-bang implementation
 *
 * Identical in protocol logic to uFT4232I2CCommon.cpp.
 * No channel argument — FT232H has a single MPSSE interface.
 */

#include "uFT232HI2C.hpp"
#include "uLogger.hpp"

#include <vector>
#include <array>

#define LT_HDR  "FT232H_I2C |"
#define LOG_HDR LOG_STRING(LT_HDR)


// ============================================================================
// open / close
// ============================================================================

FT232HI2C::Status FT232HI2C::open(uint8_t  u8I2CAddress,
                                   uint32_t u32ClockHz,
                                   uint8_t  u8DeviceIndex)
{
    if (is_open()) close();

    auto s = open_device(u8DeviceIndex);
    if (s != Status::SUCCESS) return s;

    mpsse_purge();

    s = configure_mpsse_i2c(u32ClockHz);
    if (s != Status::SUCCESS) {
        FT232HBase::close();
        return s;
    }

    m_u8I2CAddress = u8I2CAddress;
    return Status::SUCCESS;
}

FT232HI2C::Status FT232HI2C::close()
{
    if (is_open()) {
        i2c_stop();
    }
    return FT232HBase::close();
}


// ============================================================================
// configure_mpsse_i2c
// ============================================================================

FT232HI2C::Status FT232HI2C::configure_mpsse_i2c(uint32_t u32ClockHz) const
{
    // Divide by 3 due to 3-phase clocking: each clock period is 3 MPSSE phases
    uint32_t divisor = (CLOCK_BASE_HZ / 3u / u32ClockHz) - 1u;

    std::vector<uint8_t> init;
    init.reserve(16);
    init.push_back(MPSSE_DIS_DIV5);         // 60 MHz base
    init.push_back(MPSSE_EN_3PHASE);        // I2C: 3-phase clocking
    init.push_back(MPSSE_DIS_ADAPTIVE);     // no adaptive clocking
    init.push_back(MPSSE_LOOPBACK_OFF);
    init.push_back(MPSSE_SET_CLK_DIV);
    init.push_back(static_cast<uint8_t>(divisor & 0xFFu));
    init.push_back(static_cast<uint8_t>((divisor >> 8u) & 0xFFu));
    // Set SCL/SDA high (idle)
    init.push_back(MPSSE_SET_BITS_LOW);
    init.push_back(I2C_SCL | I2C_SDA_O);   // SCL=H, SDA=H
    init.push_back(DIR_SCL_SDA_OUT);        // both initially outputs
    init.push_back(MPSSE_SEND_IMMEDIATE);

    return mpsse_write(init.data(), init.size());
}


// ============================================================================
// Pin-state helpers
// ============================================================================

void FT232HI2C::push_pin_state(std::vector<uint8_t>& buf,
                                bool scl, bool drive_sda_low)
{
    uint8_t val = 0x00u;
    uint8_t dir = DIR_SCL_ONLY; // SCL output; SDA released (input) unless driving
    if (scl)          val |= I2C_SCL;
    if (drive_sda_low) {
        val &= static_cast<uint8_t>(~I2C_SDA_O); // SDA = low
        dir |= I2C_SDA_O;                          // SDA = output (actively driven)
    }
    buf.push_back(MPSSE_SET_BITS_LOW);
    buf.push_back(val);
    buf.push_back(dir);
}

void FT232HI2C::push_read_sda(std::vector<uint8_t>& buf)
{
    buf.push_back(MPSSE_GET_BITS_LOW);
    buf.push_back(MPSSE_SEND_IMMEDIATE);
}


// ============================================================================
// I2C bus conditions
// ============================================================================

FT232HI2C::Status FT232HI2C::i2c_start() const
{
    // SCL=H, SDA=H → SCL=H, SDA=L → SCL=L, SDA=L
    std::vector<uint8_t> buf;
    push_pin_state(buf, true,  false); // SCL=H, SDA=H
    push_pin_state(buf, true,  true);  // SCL=H, SDA=L (start)
    push_pin_state(buf, false, true);  // SCL=L, SDA=L
    return mpsse_write(buf.data(), buf.size());
}

FT232HI2C::Status FT232HI2C::i2c_repeated_start() const
{
    std::vector<uint8_t> buf;
    push_pin_state(buf, false, false); // SCL=L, SDA=H (release)
    push_pin_state(buf, true,  false); // SCL=H, SDA=H
    push_pin_state(buf, true,  true);  // SCL=H, SDA=L (re-start)
    push_pin_state(buf, false, true);  // SCL=L, SDA=L
    return mpsse_write(buf.data(), buf.size());
}

FT232HI2C::Status FT232HI2C::i2c_stop() const
{
    std::vector<uint8_t> buf;
    push_pin_state(buf, false, true);  // SCL=L, SDA=L
    push_pin_state(buf, true,  true);  // SCL=H, SDA=L
    push_pin_state(buf, true,  false); // SCL=H, SDA=H (stop)
    return mpsse_write(buf.data(), buf.size());
}


// ============================================================================
// Byte-level I/O
// ============================================================================

FT232HI2C::Status FT232HI2C::i2c_write_byte(uint8_t byte, bool& ack) const
{
    std::vector<uint8_t> buf;

    for (int bit = 7; bit >= 0; --bit) {
        bool b = (byte >> bit) & 0x01u;
        push_pin_state(buf, false, !b); // set SDA, SCL=L
        push_pin_state(buf, true,  !b); // SCL=H (clock high)
        push_pin_state(buf, false, !b); // SCL=L
    }

    // Release SDA for ACK from slave
    push_pin_state(buf, false, false); // SCL=L, SDA released
    push_pin_state(buf, true,  false); // SCL=H
    push_read_sda(buf);                // sample SDA
    push_pin_state(buf, false, false); // SCL=L

    auto s = mpsse_write(buf.data(), buf.size());
    if (s != Status::SUCCESS) return s;

    uint8_t raw = 0;
    size_t  got = 0;
    s = mpsse_read(&raw, 1, FT232H_READ_DEFAULT_TIMEOUT, got);
    if (s != Status::SUCCESS) return s;

    // ACK = slave drives SDA low; read bit is ADBUS2
    ack = ((raw & I2C_SDA_I) == 0);
    return Status::SUCCESS;
}

FT232HI2C::Status FT232HI2C::i2c_read_byte(uint8_t& byte, bool sendAck) const
{
    byte = 0;
    std::vector<uint8_t> buf;

    for (int bit = 7; bit >= 0; --bit) {
        push_pin_state(buf, false, false); // SCL=L, SDA released
        push_pin_state(buf, true,  false); // SCL=H
        push_read_sda(buf);                // sample SDA
        push_pin_state(buf, false, false); // SCL=L
    }

    // Send ACK or NAK
    push_pin_state(buf, false, sendAck);   // SCL=L, SDA=L(ACK) or H(NAK)
    push_pin_state(buf, true,  sendAck);   // SCL=H
    push_pin_state(buf, false, sendAck);   // SCL=L

    auto s = mpsse_write(buf.data(), buf.size());
    if (s != Status::SUCCESS) return s;

    // Read the 8 bit samples
    std::array<uint8_t, 8> samples{};
    size_t got = 0;
    s = mpsse_read(samples.data(), 8, FT232H_READ_DEFAULT_TIMEOUT, got);
    if (s != Status::SUCCESS) return s;

    for (int i = 0; i < 8; ++i) {
        byte = static_cast<uint8_t>(byte << 1u);
        if (samples[i] & I2C_SDA_I) byte |= 0x01u;
    }
    return Status::SUCCESS;
}


// ============================================================================
// Full transactions
// ============================================================================

FT232HI2C::Status FT232HI2C::i2c_write(std::span<const uint8_t> data,
                                        uint32_t timeoutMs,
                                         size_t& bytesWritten) const
{
    (void)timeoutMs;

    if (data.empty()) return Status::INVALID_PARAM;

    bytesWritten = 0;

    Status s = i2c_start();
    if (s != Status::SUCCESS) return s;

    bool ack = false;
    s = i2c_write_byte(static_cast<uint8_t>(m_u8I2CAddress << 1u), ack);
    if (s != Status::SUCCESS || !ack) {
        (void)i2c_stop();
        return (s != Status::SUCCESS) ? s : Status::WRITE_ERROR;
    }

    for (size_t i = 0; i < data.size(); ++i) {
        s = i2c_write_byte(data[i], ack);
        if (s != Status::SUCCESS || !ack) {
            (void)i2c_stop();
            return (s != Status::SUCCESS) ? s : Status::WRITE_ERROR;
        }
        ++bytesWritten;
    }

    return i2c_stop();
}

FT232HI2C::Status FT232HI2C::i2c_read(std::span<uint8_t> data,
                                        size_t& bytesRead,
                                       uint32_t timeoutMs) const
{
    (void)timeoutMs;

    if (data.empty()) return Status::INVALID_PARAM;

    bytesRead = 0;

    Status s = i2c_repeated_start();
    if (s != Status::SUCCESS) return s;

    bool ack = false;
    s = i2c_write_byte(static_cast<uint8_t>((m_u8I2CAddress << 1) | 0x01u), ack);
    if (s != Status::SUCCESS || !ack) {
        (void)i2c_stop();
        return (s != Status::SUCCESS) ? s : Status::READ_ERROR;
    }

    for (size_t i = 0; i < data.size(); ++i) {
        const bool isLast = (i == data.size() - 1);
        s = i2c_read_byte(data[i], !isLast);
        if (s != Status::SUCCESS) {
            (void)i2c_stop();
            return s;
        }
        ++bytesRead;
    }

    return i2c_stop();
}


// ============================================================================
// ICommDriver interface
// ============================================================================

FT232HI2C::WriteResult
FT232HI2C::tout_write(uint32_t u32WriteTimeout,
                       std::span<const uint8_t> buffer) const
{
    WriteResult r;
    r.bytes_written = 0;
    size_t written  = 0;
    r.status = i2c_write(buffer, u32WriteTimeout, written);
    r.bytes_written = written;
    return r;
}

FT232HI2C::ReadResult
FT232HI2C::tout_read(uint32_t u32ReadTimeout,
                      std::span<uint8_t> buffer,
                      const ReadOptions& /*options*/) const
{
    ReadResult r;
    r.bytes_read = 0;
    size_t got   = 0;
    r.status = i2c_read(buffer, got, u32ReadTimeout);
    r.bytes_read = got;
    return r;
}
