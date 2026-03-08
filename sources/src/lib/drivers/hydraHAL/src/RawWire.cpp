#include "RawWire.hpp"
#include "common.hpp"

#include <iostream>
#include <stdexcept>
#include <unordered_map>

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

uint8_t RawWire::read_bit()
{
    _write_byte(0b00000111);
    return _read_byte();
}

uint8_t RawWire::read_byte()
{
    _write_byte(0b00000110);
    return _read_byte();
}

bool RawWire::clock()
{
    _write_byte(0b00001001);
    if (!_ack("clock")) {
        std::cerr << "[Raw-Wire] Error sending clock tick\n";
        return false;
    }
    return true;
}

bool RawWire::bulk_ticks(size_t num)
{
    if (num < 1)  throw std::invalid_argument("RawWire::bulk_ticks: must send at least 1 tick");
    if (num > 16) throw std::invalid_argument("RawWire::bulk_ticks: maximum 16 ticks per call");

    uint8_t cmd = static_cast<uint8_t>(0b00100000 | (num - 1));
    _write_byte(cmd);

    if (!_ack("bulk_ticks")) {
        std::cerr << "[Raw-Wire] Error sending clock ticks\n";
        return false;
    }
    return true;
}

bool RawWire::clocks(size_t num)
{
    if (num < 1)
        throw std::invalid_argument("RawWire::clocks: must be positive");

    while (num > 16) {
        if (!bulk_ticks(16)) return false;
        num -= 16;
    }
    return bulk_ticks(num);
}

bool RawWire::write_bits(std::span<const uint8_t> data, size_t num_bits)
{
    size_t byte_idx = 0;
    size_t remaining = num_bits;

    while (remaining > 0) {
        size_t bits_this_call = std::min(remaining, size_t{8});

        // CMD 0b0011xxxx where xxxx = (bits_this_call - 1)
        uint8_t cmd = static_cast<uint8_t>(0b00110000 | (bits_this_call - 1));
        _write_byte(cmd);

        if (byte_idx < data.size())
            _write_byte(data[byte_idx]);
        else
            _write_byte(0x00);

        if (!_ack("write_bits")) {
            std::cerr << "[Raw-Wire] Error writing bits\n";
            return false;
        }

        remaining -= bits_this_call;
        ++byte_idx;
    }
    return true;
}

std::vector<uint8_t> RawWire::bulk_write(std::span<const uint8_t> data)
{
    if (data.empty())
        throw std::invalid_argument("RawWire::bulk_write: data must not be empty");
    if (data.size() > 16)
        throw std::invalid_argument("RawWire::bulk_write: maximum 16 bytes per call");

    uint8_t cmd = static_cast<uint8_t>(0b00010000 | (data.size() - 1));
    _write_byte(cmd);
    _write(data);

    if (!_ack("bulk_write")) {
        std::cerr << "[Raw-Wire] bulk_write: unknown error\n";
        return {};
    }

    return _read(data.size());
}

std::vector<uint8_t> RawWire::write(std::span<const uint8_t> data)
{
    auto chunks = split(std::vector<uint8_t>(data.begin(), data.end()), 16);
    std::vector<uint8_t> result;
    for (auto& chunk : chunks) {
        auto rx = bulk_write(chunk);
        result.insert(result.end(), rx.begin(), rx.end());
    }
    return result;
}

std::vector<uint8_t> RawWire::read(size_t length)
{
    std::vector<uint8_t> result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result.push_back(read_byte());
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
        std::cerr << "[Raw-Wire] Error setting CLK pin\n";
        return false;
    }
    _clk = level;
    return true;
}

int RawWire::get_sda() const
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
        std::cerr << "[Raw-Wire] Error setting SDA pin\n";
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
    if (it == kSpeedMap.end())
        throw std::invalid_argument(
            "RawWire::set_speed: valid values are 5000, 50000, 100000, 1000000");

    uint8_t cmd = static_cast<uint8_t>(0b01100000 | it->second);
    _write_byte(cmd);

    if (!_ack("set_speed")) {
        std::cerr << "[Raw-Wire] Error setting speed\n";
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
        std::cerr << "[Raw-Wire] set_polarity: value must be 0 or 1\n";
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
        std::cerr << "[Raw-Wire] set_wires: value must be 2 or 3\n";
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
        std::cerr << "[Raw-Wire] set_gpio_mode: value must be 0 (Push-Pull) or 1 (Open-Drain)\n";
        return false;
    }
    return _configure_port();
}

bool RawWire::_configure_port()
{
    uint8_t cmd = static_cast<uint8_t>(0b10000000 | (_config & 0x7F));
    _write_byte(cmd);

    if (!_ack("_configure_port")) {
        std::cerr << "[Raw-Wire] Error setting config\n";
        return false;
    }
    return true;
}

} // namespace HydraHAL
