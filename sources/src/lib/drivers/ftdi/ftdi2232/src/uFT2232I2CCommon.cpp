#include "FT2232Base.hpp"
#include "uFT2232I2C.hpp"
#include "uLogger.hpp"

#include <algorithm>
#include <vector>
#include <thread>
#include <chrono>

#define LT_HDR  "FT2232_I2C|"
#define LOG_HDR LOG_STRING(LT_HDR)


// ============================================================================
// open / close
// ============================================================================

FT2232I2C::Status FT2232I2C::open(uint8_t  u8I2CAddress,
                                   uint32_t u32ClockHz,
                                   Variant  variant,
                                   Channel  channel,
                                   uint8_t  u8DeviceIndex)
{
    if (u32ClockHz == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid clock speed"));
        return Status::INVALID_PARAM;
    }

    Status s = open_device(variant, channel, u8DeviceIndex);
    if (s != Status::SUCCESS) {
        return s;
    }

    m_u8I2CAddress = u8I2CAddress;

    s = configure_mpsse_i2c(u32ClockHz);
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("MPSSE I2C init failed, clock="); LOG_UINT32(u32ClockHz));
        FT2232Base::close();
        return s;
    }

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("FT2232 I2C opened: variant="); LOG_UINT32(static_cast<uint8_t>(variant));
              LOG_STRING("ch="); LOG_UINT32(static_cast<uint8_t>(channel));
              LOG_STRING("idx="); LOG_UINT32(u8DeviceIndex);
              LOG_STRING("addr=0x"); LOG_HEX8(u8I2CAddress);
              LOG_STRING("clock="); LOG_UINT32(u32ClockHz));

    return Status::SUCCESS;
}


FT2232I2C::Status FT2232I2C::close()
{
    if (is_open()) {
        (void)i2c_stop(); // best-effort clean bus state
    }
    return FT2232Base::close();
}


// ============================================================================
// PUBLIC UNIFIED INTERFACE  (ICommDriver)
// ============================================================================

FT2232I2C::ReadResult FT2232I2C::tout_read(uint32_t u32ReadTimeout,
                                            std::span<uint8_t> buffer,
                                            const ReadOptions& options) const
{
    ReadResult result;

    if (!is_open()) {
        result.status = Status::PORT_ACCESS;
        return result;
    }

    uint32_t timeout = (u32ReadTimeout == 0) ? FT2232_READ_DEFAULT_TIMEOUT : u32ReadTimeout;

    switch (options.mode)
    {
        case ReadMode::Exact:
        {
            size_t bytesRead = 0;
            result.status           = i2c_read(buffer, bytesRead, timeout);
            result.bytes_read       = bytesRead;
            result.found_terminator = false;
            break;
        }

        case ReadMode::UntilDelimiter:
        {
            if (buffer.size() < 2) {
                result.status = Status::INVALID_PARAM;
                break;
            }

            size_t pos    = 0;
            result.status = Status::READ_TIMEOUT;

            while (pos < buffer.size() - 1) {
                uint8_t byte = 0;
                size_t  got  = 0;
                Status  s    = i2c_read(std::span<uint8_t>(&byte, 1), got, timeout);

                if (s != Status::SUCCESS || got == 0) { result.status = s; break; }

                if (byte == options.delimiter) {
                    buffer[pos]             = '\0';
                    result.found_terminator = true;
                    result.status           = Status::SUCCESS;
                    break;
                }
                buffer[pos++] = byte;
            }

            if (pos == buffer.size() - 1 && result.status == Status::READ_TIMEOUT) {
                result.status = Status::BUFFER_OVERFLOW;
            }

            result.bytes_read = pos;
            break;
        }

        case ReadMode::UntilToken:
        {
            if (options.token.empty()) {
                result.status = Status::INVALID_PARAM;
                break;
            }

            const auto& token = options.token;
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

                if (s != Status::SUCCESS || got == 0) { result.status = s; break; }

                while (matched > 0 && byte != token[matched]) {
                    matched = static_cast<size_t>(lps[matched - 1]);
                }
                if (byte == token[matched]) { ++matched; }
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


FT2232I2C::WriteResult FT2232I2C::tout_write(uint32_t u32WriteTimeout,
                                              std::span<const uint8_t> buffer) const
{
    WriteResult result;

    if (!is_open()) {
        result.status = Status::PORT_ACCESS;
        return result;
    }

    uint32_t timeout    = (u32WriteTimeout == 0) ? FT2232_WRITE_DEFAULT_TIMEOUT : u32WriteTimeout;
    size_t   bytesWritten = 0;

    result.status        = i2c_write(buffer, timeout, bytesWritten);
    result.bytes_written = bytesWritten;

    return result;
}


// ============================================================================
// MPSSE CONFIGURATION
// ============================================================================

FT2232I2C::Status FT2232I2C::configure_mpsse_i2c(uint32_t u32ClockHz) const
{
    // MPSSE sync: send bad opcode, expect 0xFA 0xAA echo
    {
        const uint8_t sync[1] = { 0xAA };
        (void)mpsse_write(sync, 1);
        uint8_t echo[2] = {0};
        size_t  got     = 0;
        (void)mpsse_read(echo, 2, 200, got);
    }

    // Clock divisor:  SCK = clock_base_hz() / ((1 + divisor) * 2)
    //   → divisor = (clock_base_hz() / 2 / clockHz) - 1
    const uint32_t half = clock_base_hz() / 2u;
    uint32_t divisor = (u32ClockHz > 0 && u32ClockHz < half)
                       ? (half / u32ClockHz) - 1u
                       : 0u;
    divisor = std::min(divisor, static_cast<uint32_t>(0xFFFFu));

    const uint8_t divLow  = static_cast<uint8_t>( divisor       & 0xFFu);
    const uint8_t divHigh = static_cast<uint8_t>((divisor >> 8) & 0xFFu);

    std::vector<uint8_t> init;
    init.reserve(20);

    push_clock_init(init);           // DIS_DIV5 for FT2232H; nothing for FT2232D
    init.push_back(MPSSE_DIS_ADAPTIVE);
    init.push_back(MPSSE_DIS_3PHASE);
    init.push_back(MPSSE_LOOPBACK_OFF);

    init.push_back(MPSSE_SET_CLK_DIV);
    init.push_back(divLow);
    init.push_back(divHigh);

    // All ADBUS pins low/output as safe reset state
    init.push_back(MPSSE_SET_BITS_LOW);
    init.push_back(0x00u);
    init.push_back(0xFFu);

    // Release I²C bus to idle: SCL=H (output), SDA_O=input (float high)
    init.push_back(MPSSE_SET_BITS_LOW);
    init.push_back(I2C_SCL);
    init.push_back(DIR_SCL_ONLY);

    init.push_back(MPSSE_SEND_IMMEDIATE);

    Status s = mpsse_write(init.data(), init.size());
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("configure_mpsse_i2c: init sequence failed"));
    }
    return s;
}


// ============================================================================
// PIN-STATE HELPERS
// ============================================================================

void FT2232I2C::push_pin_state(std::vector<uint8_t>& buf, bool scl, bool drive_sda_low)
{
    const uint8_t value = scl ? I2C_SCL : 0x00u;
    const uint8_t dir   = drive_sda_low ? DIR_SCL_SDA_OUT : DIR_SCL_ONLY;
    buf.push_back(MPSSE_SET_BITS_LOW);
    buf.push_back(value);
    buf.push_back(dir);
}

void FT2232I2C::push_read_sda(std::vector<uint8_t>& buf)
{
    buf.push_back(MPSSE_GET_BITS_LOW);
    buf.push_back(MPSSE_SEND_IMMEDIATE);
}


// ============================================================================
// I²C BUS CONDITIONS
// ============================================================================

FT2232I2C::Status FT2232I2C::i2c_start() const
{
    std::vector<uint8_t> cmd;
    cmd.reserve(9);
    push_pin_state(cmd, true,  false); // SCL=H, SDA=H (idle)
    push_pin_state(cmd, true,  true ); // SCL=H, SDA=L → START
    push_pin_state(cmd, false, true ); // SCL=L, SDA=L
    return mpsse_write(cmd.data(), cmd.size());
}

FT2232I2C::Status FT2232I2C::i2c_repeated_start() const
{
    std::vector<uint8_t> cmd;
    cmd.reserve(12);
    push_pin_state(cmd, false, false); // SCL=L, SDA=H (release SDA)
    push_pin_state(cmd, true,  false); // SCL=H, SDA=H
    push_pin_state(cmd, true,  true ); // SCL=H, SDA=L → Sr
    push_pin_state(cmd, false, true ); // SCL=L, SDA=L
    return mpsse_write(cmd.data(), cmd.size());
}

FT2232I2C::Status FT2232I2C::i2c_stop() const
{
    std::vector<uint8_t> cmd;
    cmd.reserve(9);
    push_pin_state(cmd, false, true ); // SCL=L, SDA=L
    push_pin_state(cmd, true,  true ); // SCL=H, SDA=L
    push_pin_state(cmd, true,  false); // SCL=H, SDA=H → STOP
    return mpsse_write(cmd.data(), cmd.size());
}


// ============================================================================
// BYTE TRANSFER
// ============================================================================

FT2232I2C::Status FT2232I2C::i2c_write_byte(uint8_t byte, bool& ack) const
{
    ack = false;

    std::vector<uint8_t> cmd;
    cmd.reserve(72);

    for (int bit = 7; bit >= 0; --bit) {
        bool bitVal = (byte >> bit) & 0x01u;
        push_pin_state(cmd, false, !bitVal);
        push_pin_state(cmd, true,  !bitVal);
        push_pin_state(cmd, false, !bitVal);
    }

    Status s = mpsse_write(cmd.data(), cmd.size());
    if (s != Status::SUCCESS) return s;

    std::vector<uint8_t> ackCmd;
    ackCmd.reserve(11);
    push_pin_state(ackCmd, false, false);
    push_pin_state(ackCmd, true,  false);
    push_read_sda (ackCmd);
    push_pin_state(ackCmd, false, false);

    s = mpsse_write(ackCmd.data(), ackCmd.size());
    if (s != Status::SUCCESS) return s;

    uint8_t response = 0xFF;
    size_t  got      = 0;
    s = mpsse_read(&response, 1, 200, got);
    if (s != Status::SUCCESS || got == 0) return Status::READ_ERROR;

    ack = ((response & I2C_SDA_I) == 0);
    return Status::SUCCESS;
}

FT2232I2C::Status FT2232I2C::i2c_read_byte(uint8_t& byte, bool sendAck) const
{
    byte = 0;

    std::vector<uint8_t> cmd;
    cmd.reserve(80);

    for (int bit = 0; bit < 8; ++bit) {
        push_pin_state(cmd, false, false);
        push_pin_state(cmd, true,  false);
        push_read_sda (cmd);
        push_pin_state(cmd, false, false);
    }

    Status s = mpsse_write(cmd.data(), cmd.size());
    if (s != Status::SUCCESS) return s;

    uint8_t responses[8] = {0};
    size_t  got          = 0;
    s = mpsse_read(responses, 8, 500, got);
    if (s != Status::SUCCESS || got != 8) return Status::READ_ERROR;

    for (int i = 0; i < 8; ++i) {
        byte = static_cast<uint8_t>((byte << 1) | ((responses[i] >> 2) & 0x01u));
    }

    std::vector<uint8_t> ackCmd;
    ackCmd.reserve(9);
    push_pin_state(ackCmd, false, sendAck);
    push_pin_state(ackCmd, true,  sendAck);
    push_pin_state(ackCmd, false, sendAck);

    return mpsse_write(ackCmd.data(), ackCmd.size());
}


// ============================================================================
// FULL TRANSACTIONS
// ============================================================================

FT2232I2C::Status FT2232I2C::i2c_write(std::span<const uint8_t> data,
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

FT2232I2C::Status FT2232I2C::i2c_read(std::span<uint8_t> data,
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
