#include "MMC.hpp"
#include "common.hpp"
#include <iostream>
#include <stdexcept>

namespace HydraHAL {

MMC::MMC(std::shared_ptr<Hydrabus> hydrabus)
    : Protocol(std::move(hydrabus), "MMC1", "eMMC", 0x0D)
{}

// ---------------------------------------------------------------------------
// Register access
// ---------------------------------------------------------------------------

std::vector<uint8_t> MMC::get_cid()
{
    _write_byte(0b00000010);
    /* status byte */ _read_byte();
    return _read(REG_SIZE_STD);
}

std::vector<uint8_t> MMC::get_csd()
{
    _write_byte(0b00000011);
    /* status byte */ _read_byte();
    return _read(REG_SIZE_STD);
}

std::vector<uint8_t> MMC::get_ext_csd()
{
    _write_byte(0b00000110);
    /* status byte */ _read_byte();
    return _read(REG_SIZE_EXT);
}

// ---------------------------------------------------------------------------
// Block I/O
// ---------------------------------------------------------------------------

std::vector<uint8_t> MMC::read(uint32_t block_num)
{
    _write_byte(0b00000100);
    _write_u32_be(block_num);

    uint8_t status = _read_byte();
    if (status != 0x01) {
        std::cerr << "[eMMC] read: error status 0x"
                  << std::hex << static_cast<int>(status) << std::dec << '\n';
        return {};
    }
    return _read(BLOCK_SIZE);
}

bool MMC::write(std::span<const uint8_t> data, uint32_t block_num)
{
    if (data.size() != BLOCK_SIZE)
        throw std::invalid_argument("MMC::write: data must be exactly 512 bytes");

    _write_byte(0b00000101);
    _write_u32_be(block_num);
    _write(data);

    return _read_byte() == 0x01;
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

int MMC::get_bus_width() const
{
    return (_config & 0b1) ? 4 : 1;
}

bool MMC::set_bus_width(int width)
{
    if (width == 1)
        _config = static_cast<uint8_t>(_config & ~0b1);
    else if (width == 4)
        _config = static_cast<uint8_t>(_config | 0b1);
    else {
        std::cerr << "[eMMC] set_bus_width: valid values are 1 or 4\n";
        return false;
    }
    return _configure_port();
}

bool MMC::_configure_port()
{
    uint8_t cmd = static_cast<uint8_t>(0b10000000 | (_config & 0x7F));
    _write_byte(cmd);
    if (!_ack("_configure_port")) {
        std::cerr << "[eMMC] Error setting config\n";
        return false;
    }
    return true;
}

} // namespace HydraHAL
