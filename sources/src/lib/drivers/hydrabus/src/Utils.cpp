#include "Utils.hpp"
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

#define LT_HDR     "HYDRA_UTILS |"
#define LOG_HDR    LOG_STRING(LT_HDR)


/////////////////////////////////////////////////////////////////////////////////
//                         NAMESPACE IMPLEMENTATION                            //
/////////////////////////////////////////////////////////////////////////////////

namespace HydraHAL {

Utils::Utils(std::shared_ptr<Hydrabus> hydrabus)
    : _hydrabus(std::move(hydrabus))
{
    _hydrabus->flush_input();
}

// ---------------------------------------------------------------------------
// ADC
// ---------------------------------------------------------------------------

uint16_t Utils::read_adc()
{
    _hydrabus->write_byte(0x14);
    auto resp = _hydrabus->read(2);
    if (resp.size() < 2) return 0;
    // Big-endian 16-bit value
    return static_cast<uint16_t>((resp[0] << 8) | resp[1]);
}

void Utils::continuous_adc(std::function<bool(uint16_t)> callback)
{
    _hydrabus->write_byte(0x15);
    while (true) {
        auto resp = _hydrabus->read(2);
        if (resp.size() < 2) break;
        uint16_t value = static_cast<uint16_t>((resp[0] << 8) | resp[1]);
        if (!callback(value)) {
            // Signal the firmware to stop by sending a null byte, then reset
            _hydrabus->write_byte(0x00);
            _hydrabus->reset_to_bbio();
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Frequency counter
// ---------------------------------------------------------------------------

std::pair<uint32_t, uint32_t> Utils::read_frequency()
{
    _hydrabus->write_byte(0x16);

    auto freq_bytes = _hydrabus->read(4);
    auto duty_bytes = _hydrabus->read(4);

    if (freq_bytes.size() < 4 || duty_bytes.size() < 4)
        return {0, 0};

    uint32_t freq = from_le32(freq_bytes);
    uint32_t duty = from_le32(duty_bytes);
    return {freq, duty};
}

// ---------------------------------------------------------------------------
// Session
// ---------------------------------------------------------------------------

void Utils::close()
{
    _hydrabus->exit_bbio();
}

} // namespace HydraHAL
