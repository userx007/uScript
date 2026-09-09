#include "I2C.hpp"
#include "Hydrabus.hpp"
#include "Support.hpp"
#include "uLogger.hpp"

#include <array>
#include <utility>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "HYDRA_I2C   |"
#define LOG_HDR    LOG_STRING(LT_HDR)


/////////////////////////////////////////////////////////////////////////////////
//                         NAMESPACE IMPLEMENTATION                            //
/////////////////////////////////////////////////////////////////////////////////

namespace HydraHAL {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

I2C::I2C(std::shared_ptr<Hydrabus> hydrabus)
    : Protocol(std::move(hydrabus), "I2C1", "I2C", 0x02)
{
    _configure_port();
}

// ---------------------------------------------------------------------------
// Bus conditions
// ---------------------------------------------------------------------------

bool I2C::start(std::stop_token stop_tok)
{
    _write_byte(0x02, stop_tok);
    if (!_ack("start", stop_tok)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Cannot send START condition"));
        return false;
    }
    return true;
}

bool I2C::stop(std::stop_token stop_tok)
{
    _write_byte(0x03, stop_tok);
    if (!_ack("stop", stop_tok)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Cannot send STOP condition"));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Byte-level primitives
// ---------------------------------------------------------------------------

uint8_t I2C::read_byte(std::stop_token stop_tok)
{
    _write_byte(0x04, stop_tok);
    return _read_byte(stop_tok);
}

bool I2C::send_ack(std::stop_token stop_tok)
{
    _write_byte(0x06, stop_tok);
    if (!_ack("send_ack", stop_tok)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Cannot send ACK"));
        return false;
    }
    return true;
}

bool I2C::send_nack(std::stop_token stop_tok)
{
    _write_byte(0x07, stop_tok);
    if (!_ack("send_nack", stop_tok)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Cannot send NACK"));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Bulk write (up to 16 bytes, HydraFW 0b0001xxxx)
// ---------------------------------------------------------------------------

std::vector<uint8_t> I2C::bulk_write(std::span<const uint8_t> data, std::stop_token stop_tok)
{
    if (data.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("bulk_write: data must not be empty"));
        return {};
    }
    if (data.size() > 16) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("bulk_write: maximum 16 bytes per call"));
        return {};
    }

    uint8_t cmd = static_cast<uint8_t>(0b00010000 | (data.size() - 1));
    _write_byte(cmd, stop_tok);

    if (!_ack("bulk_write ready", stop_tok)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("bulk_write: unknown error"));
        return {};
    }

    _write(data, stop_tok);

    // One status byte per transmitted byte: 0x00 = ACK, 0x01 = NACK
    return _read(data.size(), stop_tok);
}

// ---------------------------------------------------------------------------
// Write-then-read (HydraFW 0b00001000)
// ---------------------------------------------------------------------------

std::optional<std::vector<uint8_t>> I2C::write_read(
        std::span<const uint8_t> data,
        size_t                   read_len,
        std::stop_token          stop_tok)
{
    _write_byte(0b00001000, stop_tok);
    _write_u16_be(static_cast<uint16_t>(data.size()), stop_tok);
    _write_u16_be(static_cast<uint16_t>(read_len), stop_tok);

    // Firmware replies 0x00 immediately if the parameters are invalid,
    // or returns no byte (timeout) if it is ready to receive data.
    auto peek = _read_with_timeout(1, Hydrabus::ZERO_TIMEOUT_MS, stop_tok);
    if (!peek.empty() && peek[0] == 0x00) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("write_read: firmware rejected command (too many bytes?)"));
        return std::nullopt;
    }

    _write(data, stop_tok);

    // Firmware replies 0x01 if data was ACKed, 0x00 otherwise
    auto status = _read(1, stop_tok);
    if (status.empty() || status[0] != 0x01) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("write_read: data not ACKed, aborting"));
        return std::nullopt;
    }

    if (read_len == 0) return std::vector<uint8_t>{};
    return _read(read_len, stop_tok);
}

// ---------------------------------------------------------------------------
// High-level write / read
// ---------------------------------------------------------------------------

bool I2C::write(std::span<const uint8_t> data, std::stop_token stop_tok)
{
    return write_read(data, 0, stop_tok).has_value();
}

std::vector<uint8_t> I2C::read(size_t length, std::stop_token stop_tok)
{
    if (length == 0) return {};

    std::vector<uint8_t> result;
    result.reserve(length);

    // ACK all bytes except the last, then NACK to signal end-of-read
    for (size_t i = 0; i < length - 1; ++i) {
        result.push_back(read_byte(stop_tok));
        send_ack(stop_tok);
    }
    result.push_back(read_byte(stop_tok));
    send_nack(stop_tok);

    return result;
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

bool I2C::set_speed(Speed speed)
{
    auto s = static_cast<uint8_t>(speed);
    if (s > 0b11) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("set_speed: invalid speed value"));
        return false;
    }

    uint8_t cmd = static_cast<uint8_t>(0b01100000 | s);
    _write_byte(cmd);

    if (!_ack("set_speed")) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Error setting speed"));
        return false;
    }
    return true;
}

bool I2C::set_clock_stretch(uint32_t clocks)
{
    _write_byte(0b00100000);
    _write_u32_be(clocks);

    if (!_ack("set_clock_stretch")) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Error setting clock stretch"));
        return false;
    }
    return true;
}

bool I2C::get_pullup() const
{
    return (_config & 0b100) != 0;
}

bool I2C::set_pullup(bool enable)
{
    if (enable)
        _config = static_cast<uint8_t>(_config |  (1 << 2));
    else
        _config = static_cast<uint8_t>(_config & ~(1 << 2));
    return _configure_port();
}

// ---------------------------------------------------------------------------
// Bus scanner
// ---------------------------------------------------------------------------

std::vector<uint8_t> I2C::scan(std::stop_token stop_tok)
{
    std::vector<uint8_t> found;

    // Probe 7-bit addresses 0x01–0x77 (skip reserved ranges)
    for (uint8_t addr = 0x01; addr < 0x78; ++addr) {
        if (stop_tok.stop_requested()) break;
        uint8_t probe = static_cast<uint8_t>(addr << 1);  // shift to 8-bit write addr
        start(stop_tok);
        const std::array<uint8_t, 1> probe_buf{probe};
        auto ack_flags = bulk_write(probe_buf, stop_tok);
        // 0x00 = ACK means a device responded
        if (!ack_flags.empty() && ack_flags[0] == 0x00) {
            found.push_back(addr);
        }
        stop(stop_tok);
    }

    return found;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

bool I2C::_configure_port()
{
    uint8_t cmd = static_cast<uint8_t>(0b01000000 | (_config & 0x3F));
    _write_byte(cmd);

    if (!_ack("_configure_port")) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Error setting config"));
        return false;
    }
    return true;
}

} // namespace HydraHAL
