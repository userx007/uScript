#include "SDIO.hpp"
#include "Support.hpp"
#include "uLogger.hpp"

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "HYB_SDIO   |"
#define LOG_HDR    LOG_STRING(LT_HDR)


/////////////////////////////////////////////////////////////////////////////////
//                         NAMESPACE IMPLEMENTATION                            //
/////////////////////////////////////////////////////////////////////////////////

namespace HydraHAL {

SDIO::SDIO(std::shared_ptr<Hydrabus> hydrabus)
    : Protocol(std::move(hydrabus), "SDI1", "SDIO", 0x0E)
{}

// ---------------------------------------------------------------------------
// Command variants
// ---------------------------------------------------------------------------

bool SDIO::send_no(uint8_t cmd_id, uint32_t cmd_arg)
{
    _write_byte(0b00000100);
    _write_byte(cmd_id);
    _write_u32_le(cmd_arg);
    return _read_byte() == 0x01;
}

std::optional<std::vector<uint8_t>> SDIO::send_short(uint8_t cmd_id, uint32_t cmd_arg)
{
    _write_byte(0b00000101);
    _write_byte(cmd_id);
    _write_u32_le(cmd_arg);

    if (_read_byte() != 0x01) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("send_short: error response"));
        return std::nullopt;
    }
    return _read(4);
}

std::optional<std::vector<uint8_t>> SDIO::send_long(uint8_t cmd_id, uint32_t cmd_arg)
{
    _write_byte(0b00000110);
    _write_byte(cmd_id);
    _write_u32_le(cmd_arg);

    if (_read_byte() != 0x01) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("send_long: error response"));
        return std::nullopt;
    }
    return _read(16);
}

// ---------------------------------------------------------------------------
// Data transfer
// ---------------------------------------------------------------------------

bool SDIO::write(uint8_t cmd_id, uint32_t cmd_arg, std::span<const uint8_t> data)
{
    if (data.size() != BLOCK_SIZE) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("write: data must be exactly 512 bytes"));
        return false;
    }

    _write_byte(0b00001001);
    _write_byte(cmd_id);
    _write_u32_le(cmd_arg);
    _write(data);

    return _read_byte() == 0x01;
}

std::vector<uint8_t> SDIO::read(uint8_t cmd_id, uint32_t cmd_arg)
{
    _write_byte(0b00001101);
    _write_byte(cmd_id);
    _write_u32_le(cmd_arg);

    if (_read_byte() != 0x01) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("read: error response"));
        return {};
    }
    return _read(BLOCK_SIZE);
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

int SDIO::get_bus_width() const { return (_config & 0b01) ? 4 : 1; }

bool SDIO::set_bus_width(int width)
{
    if (width == 1)
        _config = static_cast<uint8_t>(_config & ~0b01);
    else if (width == 4)
        _config = static_cast<uint8_t>(_config |  0b01);
    else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("set_bus_width: valid values are 1 or 4"));
        return false;
    }
    return _configure_port();
}

int SDIO::get_frequency() const 
{ 
    return (_config & 0b10) ? 1 : 0; 
}

bool SDIO::set_frequency(int freq)
{
    if (freq == 0)
        _config = static_cast<uint8_t>(_config & ~(1 << 1));
    else if (freq == 1)
        _config = static_cast<uint8_t>(_config |  (1 << 1));
    else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("set_frequency: valid values are 0 (slow) or 1 (fast)"));
        return false;
    }
    return _configure_port();
}

bool SDIO::_configure_port()
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
