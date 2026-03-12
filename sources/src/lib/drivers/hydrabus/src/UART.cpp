#include "UART.hpp"
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

#define LT_HDR     "HYDRA_UART  |"
#define LOG_HDR    LOG_STRING(LT_HDR)


/////////////////////////////////////////////////////////////////////////////////
//                         NAMESPACE IMPLEMENTATION                            //
/////////////////////////////////////////////////////////////////////////////////

namespace HydraHAL {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

UART::UART(std::shared_ptr<Hydrabus> hydrabus)
    : Protocol(std::move(hydrabus), "ART1", "UART", 0x03)
{}

// ---------------------------------------------------------------------------
// Data transfer
// ---------------------------------------------------------------------------

bool UART::bulk_write(std::span<const uint8_t> data)
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
    _write_byte(cmd);
    _write(data);

    // Firmware sends one status byte per transmitted byte (0x01 = ok)
    bool ok = true;
    for (size_t i = 0; i < data.size(); ++i) {
        uint8_t status = _read_byte();
        if (status != 0x01) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("bulk_write: transfer error at byte"); LOG_SIZET(i));
            ok = false;
        }
    }
    return ok;
}

bool UART::write(std::span<const uint8_t> data)
{
    const uint8_t* ptr = data.data();
    size_t         rem = data.size();

    while (rem > 0) {
        size_t chunk = std::min(rem, size_t{16});
        if (!bulk_write({ptr, chunk})) return false;
        ptr += chunk;
        rem -= chunk;
    }
    return true;
}

std::vector<uint8_t> UART::read(size_t length)
{
    return _read(length);
}

// ---------------------------------------------------------------------------
// Configuration — baud rate
// ---------------------------------------------------------------------------

uint32_t UART::get_baud() const
{
    return _baud;
}

bool UART::set_baud(uint32_t baud)
{
    _write_byte(0b00000111);
    _write_u32_be(baud);

    if (!_ack("set_baud")) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Error setting baud rate"));
        return false;
    }
    _baud = baud;
    return true;
}

// ---------------------------------------------------------------------------
// Configuration — parity
// ---------------------------------------------------------------------------

UART::Parity UART::get_parity() const
{
    return _parity;
}

bool UART::set_parity(Parity parity)
{
    // CMD 0b10000000 | (parity << 2)
    uint8_t cmd = static_cast<uint8_t>(0b10000000 | (static_cast<uint8_t>(parity) << 2));
    _write_byte(cmd);

    if (!_ack("set_parity")) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Error setting parity"));
        return false;
    }
    _parity = parity;
    return true;
}

// ---------------------------------------------------------------------------
// Configuration — echo
// ---------------------------------------------------------------------------

bool UART::get_echo() const
{
    return _echo;
}

bool UART::set_echo(bool enable)
{
    // CMD 0b0000001x : x=0 means echo ON (NOT inverted in firmware),
    //                  x=1 means echo OFF
    // Python: CMD = 0b00000010 | (not value)  → same as (enable ? 0x02 : 0x03)
    uint8_t cmd = static_cast<uint8_t>(enable ? 0x02 : 0x03);
    _write_byte(cmd);

    if (!_ack("set_echo")) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Error setting echo"));
        return false;
    }
    _echo = enable;
    return true;
}

// ---------------------------------------------------------------------------
// Bridge mode
// ---------------------------------------------------------------------------

void UART::enter_bridge()
{
    // CMD 0b00001111 — exits BBIO on the USB side; only UBTN can restore it
    _write_byte(0b00001111);
    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Bridge mode active — press UBTN on HydraBus to exit"));
}

} // namespace HydraHAL
