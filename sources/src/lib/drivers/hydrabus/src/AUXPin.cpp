#include "AUXPin.hpp"
#include "Hydrabus.hpp"
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

#define LT_HDR     "HYDRA_AUXPIN|"
#define LOG_HDR    LOG_STRING(LT_HDR)


/////////////////////////////////////////////////////////////////////////////////
//                         NAMESPACE IMPLEMENTATION                            //
/////////////////////////////////////////////////////////////////////////////////

namespace HydraHAL {

AUXPin::AUXPin(int number, std::shared_ptr<Hydrabus> hydrabus)
    : _number(number)
    , _hydrabus(std::move(hydrabus))
{}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

uint8_t AUXPin::_get_config() const
{
    // CMD 0b11100000 → returns 1 byte:
    //   bits [7:4] = pullup enable for AUX[3:0]
    //   bits [3:0] = direction for AUX[3:0]
    constexpr uint8_t CMD = 0b11100000;
    _hydrabus->write_byte(CMD);
    auto resp = _hydrabus->read(1);
    return resp.empty() ? 0u : resp[0];
}

uint8_t AUXPin::_get_values() const
{
    // CMD 0b11000000 → returns 1 byte: bits [3:0] = logic levels for AUX[3:0]
    constexpr uint8_t CMD = 0b11000000;
    _hydrabus->write_byte(CMD);
    auto resp = _hydrabus->read(1);
    return resp.empty() ? 0u : resp[0];
}

// ---------------------------------------------------------------------------
// Value
// ---------------------------------------------------------------------------

int AUXPin::get_value() const
{
    return (_get_values() >> _number) & 0x01;
}

bool AUXPin::set_value(int value)
{
    // CMD 0b11010000 | new_values_byte
    uint8_t current = _get_values();
    uint8_t updated = set_bit(current, value, _number);
    uint8_t cmd = static_cast<uint8_t>(0b11010000 | updated);

    _hydrabus->write_byte(cmd);
    auto resp = _hydrabus->read(1);
    if (resp.empty() || resp[0] != 0x01) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Error setting auxiliary pin value"));
        return false;
    }
    return true;
}

bool AUXPin::toggle()
{
    return set_value(get_value() ^ 1);
}

// ---------------------------------------------------------------------------
// Direction
// ---------------------------------------------------------------------------

AUXPin::Direction AUXPin::get_direction() const
{
    uint8_t cfg = _get_config();
    return (( cfg >> _number) & 0x01) ? Direction::Input : Direction::Output;
}

bool AUXPin::set_direction(Direction dir)
{
    // CMD 0b11110000, then 1-byte parameter with the new config
    constexpr uint8_t CMD = 0b11110000;

    uint8_t cfg     = _get_config();
    int     bit_val = (dir == Direction::Input) ? 1 : 0;
    uint8_t param   = set_bit(cfg, bit_val, _number);

    _hydrabus->write_byte(CMD);
    _hydrabus->write_byte(param);

    auto resp = _hydrabus->read(1);
    if (resp.empty() || resp[0] != 0x01) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Error setting auxiliary pin direction"));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Pull-up
// ---------------------------------------------------------------------------

int AUXPin::get_pullup() const
{
    // Pullup bits live in the upper nibble of the config byte (bits [7:4])
    return (_get_config() >> (4 + _number)) & 0x01;
}

bool AUXPin::set_pullup(int enable)
{
    constexpr uint8_t CMD = 0b11110000;

    uint8_t cfg   = _get_config();
    uint8_t param = set_bit(cfg, enable ? 1 : 0, 4 + _number);

    _hydrabus->write_byte(CMD);
    _hydrabus->write_byte(param);

    auto resp = _hydrabus->read(1);
    if (resp.empty() || resp[0] != 0x01) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Error setting auxiliary pin pull-up"));
        return false;
    }
    return true;
}

} // namespace HydraHAL
