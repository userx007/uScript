#include "Protocol.hpp"

#include "AUXPin.hpp"
#include "Support.hpp"
#include "uLogger.hpp"

#include <stdexcept>
#include <utility>

/////////////////////////////////////////////////////////////////////////////////
//                            LOG DEFINITIONS                                  //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
#undef LT_HDR
#endif
#ifdef LOG_HDR
#undef LOG_HDR
#endif

#define LT_HDR  "HYDRA_PROTO |"
#define LOG_HDR LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                         NAMESPACE IMPLEMENTATION                            //
/////////////////////////////////////////////////////////////////////////////////

namespace HydraHAL {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

Protocol::Protocol(std::shared_ptr<Hydrabus> hydrabus,
                   std::string name,
                   std::string fname,
                   uint8_t mode_byte)
    : _hydrabus(std::move(hydrabus))
    , _name(std::move(name))
    , _fname(std::move(fname))
    , _mode_byte(mode_byte)
    // Initialise all 4 AUX pins with their index and a reference to Hydrabus
    , _aux_pins{AUXPin{0, _hydrabus},
                AUXPin{1, _hydrabus},
                AUXPin{2, _hydrabus},
                AUXPin{3, _hydrabus}}
{
    _enter();
    _hydrabus->flush_input();
}

// ---------------------------------------------------------------------------
// AUX pins
// ---------------------------------------------------------------------------

AUXPin &Protocol::aux(size_t index)
{
    if (index >= _aux_pins.size()) {
        throw std::out_of_range("AUX pin index out of range (0–3)");
    }
    return _aux_pins[index];
}

const AUXPin &Protocol::aux(size_t index) const
{
    if (index >= _aux_pins.size()) {
        throw std::out_of_range("AUX pin index out of range (0–3)");
    }
    return _aux_pins[index];
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

std::string Protocol::identify()
{
    return _hydrabus->identify();
}

void Protocol::close()
{
    _exit();
    // exit_bbio sends the CLI-exit sequence, then the underlying driver
    // can be closed by the owner if desired.
}

std::shared_ptr<Hydrabus> Protocol::hydrabus()
{
    return _hydrabus;
}

std::shared_ptr<const Hydrabus> Protocol::hydrabus() const
{
    return _hydrabus;
}

// ---------------------------------------------------------------------------
// I/O primitives
// ---------------------------------------------------------------------------

bool Protocol::_write(std::span<const uint8_t> data, std::stop_token stop_tok)
{
    return _hydrabus->write(data, stop_tok);
}

bool Protocol::_write_byte(uint8_t b, std::stop_token stop_tok)
{
    return _hydrabus->write_byte(b, stop_tok);
}

bool Protocol::_write_u16_be(uint16_t v, std::stop_token stop_tok)
{
    auto arr = u16_be(v);
    return _hydrabus->write(arr, stop_tok);
}

bool Protocol::_write_u32_be(uint32_t v, std::stop_token stop_tok)
{
    auto arr = u32_be(v);
    return _hydrabus->write(arr, stop_tok);
}

bool Protocol::_write_u32_le(uint32_t v, std::stop_token stop_tok)
{
    auto arr = u32_le(v);
    return _hydrabus->write(arr, stop_tok);
}

std::vector<uint8_t> Protocol::_read(size_t n, std::stop_token stop_tok)
{
    return _hydrabus->read(n, stop_tok);
}

std::vector<uint8_t> Protocol::_read_with_timeout(size_t n, uint32_t timeout_ms, std::stop_token stop_tok)
{
    return _hydrabus->read(n, timeout_ms, stop_tok);
}

uint8_t Protocol::_read_byte(std::stop_token stop_tok)
{
    auto resp = _hydrabus->read(1, stop_tok);
    return resp.empty() ? 0u : resp[0];
}

bool Protocol::_expect_byte(uint8_t expected, const char *context, std::stop_token stop_tok)
{
    uint8_t got = _read_byte(stop_tok);
    if (got != expected) {
        LOG_PRINT(LOG_ERROR, LOG_STRING(_fname.c_str()); if (context) { LOG_STRING(context); LOG_STRING(":"); } LOG_STRING("expected"); LOG_HEX8(expected); LOG_STRING("got"); LOG_HEX8(got));
        return false;
    }
    return true;
}

bool Protocol::_ack(const char *context, std::stop_token stop_tok)
{
    return _expect_byte(0x01, context, stop_tok);
}

// ---------------------------------------------------------------------------
// Mode management
// ---------------------------------------------------------------------------

bool Protocol::_enter()
{
    _hydrabus->write_byte(_mode_byte);

    auto resp = _hydrabus->read(4);
    std::string banner(resp.begin(), resp.end());

    if (banner == _name) {
        _hydrabus->mode = _name;
        return true;
    }

    LOG_PRINT(LOG_ERROR, LOG_HDR;
              LOG_STRING("Cannot enter"); LOG_STRING(_fname.c_str());
              LOG_STRING("mode (got:"); LOG_STRING(banner.c_str()); LOG_STRING(")"));
    return false;
}

bool Protocol::_exit()
{
    return _hydrabus->reset_to_bbio();
}

} // namespace HydraHAL
