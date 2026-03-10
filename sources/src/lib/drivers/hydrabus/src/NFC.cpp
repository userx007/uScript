#include "NFC.hpp"

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "HYB_NFC    |"
#define LOG_HDR    LOG_STRING(LT_HDR)


/////////////////////////////////////////////////////////////////////////////////
//                         NAMESPACE IMPLEMENTATION                            //
/////////////////////////////////////////////////////////////////////////////////

namespace HydraHAL {

NFC::NFC(std::shared_ptr<Hydrabus> hydrabus)
    : Protocol(std::move(hydrabus), "NFC1", "NFC-Reader", 0x0C)
{}

// ---------------------------------------------------------------------------
// RF field
// ---------------------------------------------------------------------------

bool NFC::get_rf() const { return _rf; }

void NFC::set_rf(bool on)
{
    uint8_t cmd = static_cast<uint8_t>(0b00000010 | (on ? 1 : 0));
    _write_byte(cmd);
    _rf = on;
}

// ---------------------------------------------------------------------------
// Mode
// ---------------------------------------------------------------------------

NFC::Mode NFC::get_mode() const { return _mode; }

void NFC::set_mode(Mode mode)
{
    uint8_t cmd = static_cast<uint8_t>(0b00000110 | static_cast<uint8_t>(mode));
    _write_byte(cmd);
    _mode = mode;
}

// ---------------------------------------------------------------------------
// Data transfer
// ---------------------------------------------------------------------------

std::vector<uint8_t> NFC::write(std::span<const uint8_t> data, bool append_crc)
{
    _write_byte(0b00000101);
    _write_byte(static_cast<uint8_t>(append_crc ? 1 : 0));
    _write_byte(static_cast<uint8_t>(data.size()));
    _write(data);

    uint8_t rx_len = _read_byte();
    return _read(rx_len);
}

std::vector<uint8_t> NFC::write_bits(uint8_t data, uint8_t num_bits)
{
    _write_byte(0b00000100);
    _write_byte(data);
    _write_byte(num_bits);

    uint8_t rx_len = _read_byte();
    return _read(rx_len);
}

} // namespace HydraHAL
