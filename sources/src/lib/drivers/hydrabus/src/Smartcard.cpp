#include "Smartcard.hpp"
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

#define LT_HDR     "HYB_SMARTCD|"
#define LOG_HDR    LOG_STRING(LT_HDR)


/////////////////////////////////////////////////////////////////////////////////
//                         NAMESPACE IMPLEMENTATION                            //
/////////////////////////////////////////////////////////////////////////////////

namespace HydraHAL {

Smartcard::Smartcard(std::shared_ptr<Hydrabus> hydrabus)
    : Protocol(std::move(hydrabus), "CRD1", "Smartcard", 0x0B)
{
    _configure_port();
}

// ---------------------------------------------------------------------------
// Data transfer
// ---------------------------------------------------------------------------

std::optional<std::vector<uint8_t>> Smartcard::write_read(
        std::span<const uint8_t> data, size_t read_len)
{
    _write_byte(0b00000100);
    _write_u16_be(static_cast<uint16_t>(data.size()));
    _write_u16_be(static_cast<uint16_t>(read_len));

    auto peek = _read_with_timeout(1, Hydrabus::ZERO_TIMEOUT_MS);
    if (!peek.empty() && peek[0] == 0x00) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("write_read: firmware rejected command"));
        return std::nullopt;
    }

    _write(data);

    auto status = _read(1);
    if (status.empty() || status[0] != 0x01) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("write_read: unknown error, aborting"));
        return std::nullopt;
    }

    if (read_len == 0) return std::vector<uint8_t>{};
    return _read(read_len);
}

bool Smartcard::write(std::span<const uint8_t> data)
{
    return write_read(data, 0).has_value();
}

std::vector<uint8_t> Smartcard::read(size_t length)
{
    auto result = write_read({}, length);
    return result.value_or(std::vector<uint8_t>{});
}

// ---------------------------------------------------------------------------
// ATR
// ---------------------------------------------------------------------------

std::vector<uint8_t> Smartcard::get_atr()
{
    _write_byte(0x08);
    uint8_t atr_len = _read_byte();
    return _read(atr_len);
}

// ---------------------------------------------------------------------------
// RST pin
// ---------------------------------------------------------------------------

int Smartcard::get_rst() const { return _rst; }

bool Smartcard::set_rst(int level)
{
    level = level & 1;
    uint8_t cmd = static_cast<uint8_t>(0b00000010 | level);
    _write_byte(cmd);

    if (!_ack("set_rst")) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Error setting RST pin"));
        return false;
    }
    _rst = level;
    return true;
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

uint32_t Smartcard::get_baud()      const { return _baud; }
uint8_t  Smartcard::get_prescaler() const { return _prescaler; }
uint8_t  Smartcard::get_guardtime() const { return _guardtime; }
bool     Smartcard::get_pullup()    const { return (_config & 0b100) != 0; }

bool Smartcard::set_baud(uint32_t baud)
{
    _write_byte(0b01100000);
    _write_u32_be(baud);
    if (!_ack("set_baud")) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Error setting baud"));
        return false;
    }
    _baud = baud;
    return true;
}

bool Smartcard::set_prescaler(uint8_t value)
{
    _write_byte(0b00000110);
    _write_byte(value);
    if (!_ack("set_prescaler")) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Error setting prescaler"));
        return false;
    }
    _prescaler = value;
    return true;
}

bool Smartcard::set_guardtime(uint8_t value)
{
    _write_byte(0b00000111);
    _write_byte(value);
    if (!_ack("set_guardtime")) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Error setting guard time"));
        return false;
    }
    _guardtime = value;
    return true;
}

bool Smartcard::set_pullup(bool enable)
{
    if (enable)
        _config = static_cast<uint8_t>(_config |  (1 << 2));
    else
        _config = static_cast<uint8_t>(_config & ~(1 << 2));
    return _configure_port();
}

bool Smartcard::_configure_port()
{
    uint8_t cmd = static_cast<uint8_t>(0b10000000 | (_config & 0x7F));
    _write_byte(cmd);
    if (!_ack("_configure_port")) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Error setting config"));
        return false;
    }
    set_rst(_rst);   // re-apply RST state after reconfigure
    return true;
}

} // namespace HydraHAL
