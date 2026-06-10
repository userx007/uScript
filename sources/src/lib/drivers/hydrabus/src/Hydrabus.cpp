#include "Hydrabus.hpp"
#include "uLogger.hpp"

#include <chrono>
#include <algorithm>
#include <stdexcept>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "HYDRA_OPS   |"
#define LOG_HDR    LOG_STRING(LT_HDR)


/////////////////////////////////////////////////////////////////////////////////
//                         NAMESPACE IMPLEMENTATION                            //
/////////////////////////////////////////////////////////////////////////////////

namespace HydraHAL {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

Hydrabus::Hydrabus(std::shared_ptr<const ICommDriver> driver)
    : _driver(std::move(driver))
{
    if (!_driver) {
        throw std::invalid_argument("Hydrabus: driver pointer must not be null");
    }
}

// ---------------------------------------------------------------------------
// Raw I/O
// ---------------------------------------------------------------------------

bool Hydrabus::write(std::span<const uint8_t> data)
{
    if (!_driver->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("write: port is not open"));
        return false;
    }

    // [ADAPTED] tout_write now returns WriteResult{status, bytes_written}
    // instead of a plain bool / byte-count integer.
    ICommDriver::WriteResult result = _driver->tout_write(_timeout_ms, data);

    if (result.status != ICommDriver::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("write error:");
                  LOG_STRING(ICommDriver::to_string(result.status).c_str()));
        return false;
    }
    return true;
}

bool Hydrabus::write_byte(uint8_t byte)
{
    return write(std::span<const uint8_t>{&byte, 1});
}

std::vector<uint8_t> Hydrabus::read(size_t length)
{
    return read(length, _timeout_ms);
}

std::vector<uint8_t> Hydrabus::read(size_t length, uint32_t timeout_ms)
{
    if (!_driver->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("read: port is not open"));
        return {};
    }

    std::vector<uint8_t> buf(length);

    // [ADAPTED] Build a ReadOptions struct (replaces the old positional bool/
    // enum arguments that were passed directly to tout_read).
    // ReadMode::Exact fills the buffer up to buffer.size() bytes, which is
    // the behaviour the rest of HydraHAL has always depended on.
    ICommDriver::ReadOptions opts{};
    opts.mode       = ICommDriver::ReadMode::Exact;
    opts.use_buffer = false;   // no internal KMP buffering needed for raw reads

    // [ADAPTED] tout_read now returns ReadResult{status, bytes_read,
    // found_terminator} instead of a plain size_t / bool.
    ICommDriver::ReadResult result = _driver->tout_read(timeout_ms, buf, opts);

    // Shrink the vector to the number of bytes actually received so callers
    // always see a correctly-sized container even on a short read / timeout.
    buf.resize(result.bytes_read);

    if (result.status != ICommDriver::Status::SUCCESS &&
        result.status != ICommDriver::Status::READ_TIMEOUT)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("read error:");
                  LOG_STRING(ICommDriver::to_string(result.status).c_str()));
    }

    return buf;
}

void Hydrabus::flush_input()
{
    // Drain any stale bytes with a zero-timeout read; discard the data.
    std::vector<uint8_t> scratch(256);

    // [ADAPTED] Same ReadOptions construction as read(); zero-timeout means
    // the call returns immediately with whatever is already in the driver FIFO.
    ICommDriver::ReadOptions opts{};
    opts.mode       = ICommDriver::ReadMode::Exact;
    opts.use_buffer = false;

    // Return value intentionally ignored — this is a best-effort flush.
    _driver->tout_read(ZERO_TIMEOUT_MS, scratch, opts);
}

// ---------------------------------------------------------------------------
// BBIO control
// ---------------------------------------------------------------------------

bool Hydrabus::enter_bbio()
{
    // Send up to 20 null bytes; each time check the 5-byte response for
    // the "BBIO1" banner.
    static const std::string kBBIO1 = "BBIO1";

    for (int i = 0; i < 20; ++i) {
        write_byte(0x00);
        auto resp = read(5, SHORT_TIMEOUT_MS);
        std::string s(resp.begin(), resp.end());
        if (s.find(kBBIO1) != std::string::npos) {
            flush_input();
            return true;
        }
    }

    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Cannot enter BBIO mode"));
    return false;
}

bool Hydrabus::exit_bbio()
{
    if (!reset_to_bbio()) return false;

    // Send reset + CLI-exit sequence
    write_byte(0x00);
    const std::array<uint8_t, 2> cli_exit = {0x0F, '\n'};
    write(cli_exit);
    return true;
}

bool Hydrabus::reset_to_bbio()
{
    using Clock    = std::chrono::steady_clock;
    using Seconds  = std::chrono::seconds;
    auto deadline  = Clock::now() + Seconds{10};

    static const std::string kBBIO1 = "BBIO1";

    while (Clock::now() < deadline) {
        flush_input();
        write_byte(0x00);
        auto resp = read(5, RESET_TIMEOUT_MS);
        std::string s(resp.begin(), resp.end());
        if (s == kBBIO1) {
            return true;
        }
    }

    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unable to reset to BBIO mode"));
    return false;
}

std::string Hydrabus::identify()
{
    write_byte(0x01);
    auto resp = read(4);
    return std::string(resp.begin(), resp.end());
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

bool Hydrabus::is_open() const
{
    return _driver->is_open();
}

void Hydrabus::set_timeout(uint32_t timeout_ms)
{
    _timeout_ms = timeout_ms;
}

uint32_t Hydrabus::get_timeout() const
{
    return _timeout_ms;
}

} // namespace HydraHAL
