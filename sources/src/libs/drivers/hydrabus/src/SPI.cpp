#include "SPI.hpp"

#include "Hydrabus.hpp"
#include "Support.hpp"
#include "uLogger.hpp"

#include <algorithm>
#include <utility>

/////////////////////////////////////////////////////////////////////////////////
//                            LOG DEFINITIONS                                  //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
#undef LT_HDR
#endif
#ifdef LOG_HDR
#undef LOG_HDR
#endif

#define LT_HDR  "HYDRA_SPI   |"
#define LOG_HDR LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                         NAMESPACE IMPLEMENTATION                            //
/////////////////////////////////////////////////////////////////////////////////

namespace HydraHAL {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SPI::SPI(std::shared_ptr<Hydrabus> hydrabus)
    : Protocol(std::move(hydrabus), "SPI1", "SPI", 0x01)
{
    _configure_port();
}

// ---------------------------------------------------------------------------
// Chip-select
// ---------------------------------------------------------------------------

int SPI::get_cs() const
{
    return _cs_val;
}

bool SPI::set_cs(int level)
{
    // CMD 0b0000001x  (x = level)
    uint8_t cmd = static_cast<uint8_t>(0b00000010 | (level & 0x01));
    _write_byte(cmd);

    if (_ack("set_cs")) {
        _cs_val = level & 0x01;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Bulk transfer
// ---------------------------------------------------------------------------

std::vector<uint8_t> SPI::bulk_write(std::span<const uint8_t> data, std::stop_token stop_tok)
{
    if (data.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("bulk_write: data must not be empty"));
        return {};
    }
    if (data.size() > 16) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("bulk_write: maximum 16 bytes per call"));
        return {};
    }

    // CMD 0b0001xxxx  where xxxx = (len - 1)
    uint8_t cmd = static_cast<uint8_t>(0b00010000 | (data.size() - 1));
    _write_byte(cmd, stop_tok);

    if (!_ack("bulk_write ready", stop_tok)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("bulk_write: unexpected status"));
        return {};
    }

    _write(data, stop_tok);
    return _read(data.size(), stop_tok); // SPI is full-duplex: read MISO simultaneously
}

// ---------------------------------------------------------------------------
// Write-then-read (HydraFW optimised path)
// ---------------------------------------------------------------------------

std::optional<std::vector<uint8_t>> SPI::write_read(
    std::span<const uint8_t> data,
    size_t read_len,
    bool manual_cs,
    std::stop_token stop_tok)
{
    // CMD 0b00000100 | drive_cs_bit
    //   drive_cs_bit = 0 → firmware drives CS
    //   drive_cs_bit = 1 → caller drives CS
    uint8_t cmd = static_cast<uint8_t>(0b00000100 | (manual_cs ? 1 : 0));
    _write_byte(cmd, stop_tok);
    _write_u16_be(static_cast<uint16_t>(data.size()), stop_tok);
    _write_u16_be(static_cast<uint16_t>(read_len), stop_tok);

    // Peek with zero timeout to check for an immediate firmware status byte
    auto peek = _read_with_timeout(1, Hydrabus::ZERO_TIMEOUT_MS, stop_tok);

    if (!peek.empty()) {
        if (peek[0] == 0x00) {
            // Firmware rejected the request (too many bytes?)
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("write_read: firmware rejected command"));
            return std::nullopt;
        }
        if (peek[0] == 0x01 && data.empty()) {
            // Firmware confirmed, no data to send — proceed to read
            return _read(read_len, stop_tok);
        }
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("write_read: unexpected status"); LOG_HEX8(peek[0]));
        return std::nullopt;
    }

    // Empty peek means firmware is ready and waiting for write data
    _write(data, stop_tok);

    auto status = _read(1, stop_tok);
    if (status.empty() || status[0] != 0x01) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("write_read: transmit error"));
        return std::nullopt;
    }

    return _read(read_len, stop_tok);
}

// ---------------------------------------------------------------------------
// High-level write / read
// ---------------------------------------------------------------------------

bool SPI::write(std::span<const uint8_t> data, bool manual_cs, std::stop_token stop_tok)
{
    auto result = write_read(data, 0, manual_cs, stop_tok);
    return result.has_value();
}

std::vector<uint8_t> SPI::read(size_t read_len, bool manual_cs, std::stop_token stop_tok)
{
    std::vector<uint8_t> result;
    result.reserve(read_len);

    if (!manual_cs) {
        set_cs(0);
    }

    size_t remaining = read_len;
    while (remaining > 0) {
        size_t chunk = std::min(remaining, size_t{16});
        // Clock out 0xFF on MOSI; capture MISO
        std::vector<uint8_t> dummy(chunk, 0xFF);
        auto rx = bulk_write(dummy, stop_tok);
        result.insert(result.end(), rx.begin(), rx.end());
        remaining -= chunk;
        if (stop_tok.stop_requested()) {
            break;
        }
    }

    if (!manual_cs) {
        set_cs(1);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

bool SPI::set_speed(Speed speed)
{
    auto s = static_cast<uint8_t>(speed);
    if (s > 0b111) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("set_speed: invalid speed value"));
        return false;
    }
    uint8_t cmd = static_cast<uint8_t>(0b01100000 | s);
    _write_byte(cmd);
    return _ack("set_speed");
}

bool SPI::_configure_port()
{
    uint8_t cmd = static_cast<uint8_t>(0b10000000 | (_config & 0x7F));
    _write_byte(cmd);
    return _ack("_configure_port");
}

// ---- Polarity ---------------------------------------------------------------

int SPI::get_polarity() const
{
    return (_config & 0b100) ? 1 : 0;
}

bool SPI::set_polarity(int value)
{
    if (value == 0) {
        _config = static_cast<uint8_t>(_config & ~(1 << 2));
    } else {
        _config = static_cast<uint8_t>(_config | (1 << 2));
    }
    return _configure_port();
}

// ---- Phase ------------------------------------------------------------------

int SPI::get_phase() const
{
    return (_config & 0b010) ? 1 : 0;
}

bool SPI::set_phase(int value)
{
    if (value == 0) {
        _config = static_cast<uint8_t>(_config & ~(1 << 1));
    } else {
        _config = static_cast<uint8_t>(_config | (1 << 1));
    }
    return _configure_port();
}

// ---- Device -----------------------------------------------------------------

int SPI::get_device() const
{
    return (_config & 0b001) ? 1 : 0;
}

bool SPI::set_device(int value)
{
    // Reset to default config, then apply the device bit
    _config = DEFAULT_CONFIG;
    if (value == 0) {
        _config = static_cast<uint8_t>(_config & ~(1 << 0));
    } else {
        _config = static_cast<uint8_t>(_config | (1 << 0));
    }
    return _configure_port();
}

} // namespace HydraHAL
