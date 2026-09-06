#include "RawWire.hpp"
#include "Support.hpp"
#include "uLogger.hpp"

#include <unordered_map>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "HYDRA_RAWIRE|"
#define LOG_HDR    LOG_STRING(LT_HDR)


/////////////////////////////////////////////////////////////////////////////////
//                         NAMESPACE IMPLEMENTATION                            //
/////////////////////////////////////////////////////////////////////////////////

namespace HydraHAL {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

RawWire::RawWire(std::shared_ptr<Hydrabus> hydrabus)
    : Protocol(std::move(hydrabus), "RAW1", "Raw-Wire", 0x05)
{
    _configure_port();
}

// ---------------------------------------------------------------------------
// Low-level bit / pin operations
// ---------------------------------------------------------------------------

uint8_t RawWire::read_bit(std::stop_token stop_tok)
{
    _write_byte(0b00000111, stop_tok);
    return _read_byte(stop_tok);
}

uint8_t RawWire::read_byte(std::stop_token stop_tok)
{
    _write_byte(0b00000110, stop_tok);
    return _read_byte(stop_tok);
}

bool RawWire::clock(std::stop_token stop_tok)
{
    _write_byte(0b00001001, stop_tok);
    if (!_ack("clock", stop_tok)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Error sending clock tick"));
        return false;
    }
    return true;
}

bool RawWire::bulk_ticks(size_t num, std::stop_token stop_tok)
{
    if (num < 1) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("bulk_ticks: must send at least 1 tick"));
        return false;
    }
    if (num > 16) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("bulk_ticks: maximum 16 ticks per call"));
        return false;
    }

    uint8_t cmd = static_cast<uint8_t>(0b00100000 | (num - 1));
    _write_byte(cmd, stop_tok);

    if (!_ack("bulk_ticks", stop_tok)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Error sending clock ticks"));
        return false;
    }
    return true;
}

bool RawWire::clocks(size_t num, std::stop_token stop_tok)
{
    if (num < 1) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("clocks: num must be positive"));
        return false;
    }

    while (num > 16) {
        if (!bulk_ticks(16, stop_tok)) return false;
        num -= 16;
        if (stop_tok.stop_requested()) return false;
    }
    return bulk_ticks(num, stop_tok);
}

bool RawWire::write_bits(std::span<const uint8_t> data, size_t num_bits, std::stop_token stop_tok)
{
    size_t byte_idx = 0;
    size_t remaining = num_bits;

    while (remaining > 0) {
        size_t bits_this_call = std::min(remaining, size_t{8});

        // CMD 0b0011xxxx where xxxx = (bits_this_call - 1)
        uint8_t cmd = static_cast<uint8_t>(0b00110000 | (bits_this_call - 1));
        _write_byte(cmd, stop_tok);

        if (byte_idx < data.size())
            _write_byte(data[byte_idx], stop_tok);
        else
            _write_byte(0x00, stop_tok);

        if (!_ack("write_bits", stop_tok)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Error writing bits"));
            return false;
        }

        remaining -= bits_this_call;
        ++byte_idx;
    }
    return true;
}

std::vector<uint8_t> RawWire::bulk_write(std::span<const uint8_t> data, std::stop_token stop_tok)
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
    _write(data, stop_tok);

    if (!_ack("bulk_write", stop_tok)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("bulk_write: unknown error"));
        return {};
    }

    return _read(data.size(), stop_tok);
}

std::vector<uint8_t> RawWire::write(std::span<const uint8_t> data, std::stop_token stop_tok)
{
    auto chunks = split(std::vector<uint8_t>(data.begin(), data.end()), 16);
    std::vector<uint8_t> result;
    for (auto& chunk : chunks) {
        auto rx = bulk_write(chunk, stop_tok);
        result.insert(result.end(), rx.begin(), rx.end());
        if (stop_tok.stop_requested()) break;
    }
    return result;
}

std::vector<uint8_t> RawWire::read(size_t length, std::stop_token stop_tok)
{
    std::vector<uint8_t> result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        if (stop_tok.stop_requested()) break;
        result.push_back(read_byte(stop_tok));
    }
    return result;
}

// ---------------------------------------------------------------------------
// Pin control
// ---------------------------------------------------------------------------

int RawWire::get_clk() const
{
    return _clk;
}

bool RawWire::set_clk(int level)
{
    level = level & 1;
    uint8_t cmd = static_cast<uint8_t>(0b00001010 | level);
    _write_byte(cmd);

    if (!_ack("set_clk")) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Error setting CLK pin"));
        return false;
    }
    _clk = level;
    return true;
}

int RawWire::get_sda()
{
    // Issue read-SDA command and return the sampled level
    _write_byte(0b00001000);
    return static_cast<int>(_read_byte());
}

bool RawWire::set_sda(int level)
{
    level = level & 1;
    uint8_t cmd = static_cast<uint8_t>(0b00001100 | level);
    _write_byte(cmd);

    if (!_ack("set_sda")) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Error setting SDA pin"));
        return false;
    }
    _sda = level;
    return true;
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

bool RawWire::set_speed(uint32_t hz)
{
    static const std::unordered_map<uint32_t, uint8_t> kSpeedMap = {
        {    5'000, 0b00},
        {   50'000, 0b01},
        {  100'000, 0b10},
        {1'000'000, 0b11},
    };

    auto it = kSpeedMap.find(hz);
    if (it == kSpeedMap.end()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("set_speed: valid values are 5000, 50000, 100000, 1000000"));
        return false;
    }

    uint8_t cmd = static_cast<uint8_t>(0b01100000 | it->second);
    _write_byte(cmd);

    if (!_ack("set_speed")) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Error setting speed"));
        return false;
    }
    return true;
}

int RawWire::get_polarity() const  { return (_config & 0b0001) ? 1 : 0; }

bool RawWire::set_polarity(int value)
{
    if (value == 0)
        _config = static_cast<uint8_t>(_config & ~0b0001);
    else if (value == 1)
        _config = static_cast<uint8_t>(_config |  0b0001);
    else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("set_polarity: value must be 0 or 1"));
        return false;
    }
    return _configure_port();
}

int RawWire::get_wires() const { return (_config & 0b0100) ? 3 : 2; }

bool RawWire::set_wires(int value)
{
    if (value == 2)
        _config = static_cast<uint8_t>(_config & ~(1 << 2));
    else if (value == 3)
        _config = static_cast<uint8_t>(_config |  (1 << 2));
    else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("set_wires: value must be 2 or 3"));
        return false;
    }
    return _configure_port();
}

int RawWire::get_gpio_mode() const { return (_config & 0b1000) ? 1 : 0; }

bool RawWire::set_gpio_mode(int value)
{
    if (value == 0)
        _config = static_cast<uint8_t>(_config & ~(1 << 3));
    else if (value == 1)
        _config = static_cast<uint8_t>(_config |  (1 << 3));
    else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("set_gpio_mode: value must be 0 (Push-Pull) or 1 (Open-Drain)"));
        return false;
    }
    return _configure_port();
}

bool RawWire::_configure_port()
{
    uint8_t cmd = static_cast<uint8_t>(0b10000000 | (_config & 0x7F));
    _write_byte(cmd);

    if (!_ack("_configure_port")) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Error setting config"));
        return false;
    }
    return true;
}

} // namespace HydraHAL
