#include "OneWire.hpp"
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

#define LT_HDR     "HYDRA_ONEWIR|"
#define LOG_HDR    LOG_STRING(LT_HDR)


/////////////////////////////////////////////////////////////////////////////////
//                         NAMESPACE IMPLEMENTATION                            //
/////////////////////////////////////////////////////////////////////////////////


namespace HydraHAL {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

OneWire::OneWire(std::shared_ptr<Hydrabus> hydrabus)
    : Protocol(std::move(hydrabus), "1W01", "1-Wire", 0x04)
{
    _configure_port();
}

// ---------------------------------------------------------------------------
// Bus operations
// ---------------------------------------------------------------------------

bool OneWire::reset(std::stop_token stop_tok)
{
    _write_byte(0x02, stop_tok);
    return true;    // Firmware doesn't return a presence-detect byte in binary mode
}

uint8_t OneWire::read_byte(std::stop_token stop_tok)
{
    _write_byte(0x04, stop_tok);
    return _read_byte(stop_tok);
}

bool OneWire::bulk_write(std::span<const uint8_t> data, std::stop_token stop_tok)
{
    if (data.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("bulk_write: data must not be empty"));
        return false;
    }
    if (data.size() > 16) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("bulk_write: maximum 16 bytes per call"));
        return false;
    }

    uint8_t cmd = static_cast<uint8_t>(0b00010000 | (data.size() - 1));
    _write_byte(cmd, stop_tok);
    _write(data, stop_tok);

    if (!_ack("bulk_write", stop_tok)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("bulk_write: unknown error"));
        return false;
    }
    return true;
}

bool OneWire::write(std::span<const uint8_t> data, std::stop_token stop_tok)
{
    const uint8_t* ptr = data.data();
    size_t         rem = data.size();

    while (rem > 0) {
        size_t chunk = std::min(rem, size_t{16});
        if (!bulk_write({ptr, chunk}, stop_tok)) return false;
        ptr += chunk;
        rem -= chunk;
        if (stop_tok.stop_requested()) break;
    }
    return true;
}

std::vector<uint8_t> OneWire::read(size_t length, std::stop_token stop_tok)
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
// Configuration
// ---------------------------------------------------------------------------

bool OneWire::get_pullup() const
{
    return (_config & 0b100) != 0;
}

bool OneWire::set_pullup(bool enable)
{
    if (enable)
        _config = static_cast<uint8_t>(_config |  (1 << 2));
    else
        _config = static_cast<uint8_t>(_config & ~(1 << 2));
    return _configure_port();
}

bool OneWire::_configure_port()
{
    uint8_t cmd = static_cast<uint8_t>(0b01000000 | (_config & 0x3F));
    _write_byte(cmd);

    if (!_ack("_configure_port")) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Error setting config"));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// SWIO
// ---------------------------------------------------------------------------

bool OneWire::swio_init(std::stop_token /*stop_tok*/)
{
    _config = 0b1000;
    return _configure_port();
}

uint32_t OneWire::swio_read_reg(uint8_t address, std::stop_token stop_tok)
{
    _write_byte(0b00100000, stop_tok);
    _write_byte(address, stop_tok);           // little-endian 1-byte address

    auto resp = _read(4, stop_tok);
    if (resp.size() < 4) return 0;
    return from_le32(resp);
}

bool OneWire::swio_write_reg(uint8_t address, uint32_t value, std::stop_token stop_tok)
{
    _write_byte(0b00110000, stop_tok);
    _write_byte(address, stop_tok);           // little-endian 1-byte address
    _write_u32_le(value, stop_tok);           // 4-byte LE value

    if (!_ack("swio_write_reg", stop_tok)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SWIO write register: unknown error"));
        return false;
    }
    return true;
}

} // namespace HydraHAL
