#ifndef HYDRABUS_HPP
#define HYDRABUS_HPP


#include <memory>
#include <vector>
#include <string>
#include <cstdint>
#include <span>
#include "ICommDriver.hpp"

namespace HydraHAL {

/**
 * @brief Core HydraBus communication wrapper.
 *
 * Wraps an ICommDriver to provide BBIO-level protocol framing.
 * All Protocol subclasses use this as their raw I/O primitive.
 *
 * Timeout conventions (mapped from Python pyserial):
 *  - DEFAULT_TIMEOUT_MS : normal blocking operations
 *  - SHORT_TIMEOUT_MS   : detection / probing loops  (Python: 0.01 s)
 *  - RESET_TIMEOUT_MS   : reset polling              (Python: 0.1  s)
 *  - ZERO_TIMEOUT_MS    : non-blocking peek           (Python: 0)
 *
 * @example
 * @code
 * auto driver = std::make_shared<MySerialDriver>("/dev/ttyACM0");
 * auto hb     = std::make_shared<HydraHAL::Hydrabus>(driver);
 * hb->enter_bbio();
 * @endcode
 */
class Hydrabus {
public:
    static constexpr uint32_t DEFAULT_TIMEOUT_MS = 1000u;
    static constexpr uint32_t SHORT_TIMEOUT_MS   =   10u;
    static constexpr uint32_t RESET_TIMEOUT_MS   =  100u;
    static constexpr uint32_t ZERO_TIMEOUT_MS    =    0u;

    /**
     * @brief Construct with an open ICommDriver.
     * @param driver Non-null shared pointer to a driver instance.
     * @throws std::invalid_argument if driver is null.
     */
    explicit Hydrabus(std::shared_ptr<const ICommDriver> driver);

    Hydrabus(const Hydrabus&)            = delete;
    Hydrabus& operator=(const Hydrabus&) = delete;
    Hydrabus(Hydrabus&&)                 = default;
    Hydrabus& operator=(Hydrabus&&)      = default;
    ~Hydrabus()                          = default;

    // -------------------------------------------------------------------------
    // Raw I/O
    // -------------------------------------------------------------------------

    /**
     * @brief Write a byte span to the device.
     */
    bool write(std::span<const uint8_t> data);

    /** @brief Write a single byte. Convenience wrapper. */
    bool write_byte(uint8_t byte);

    /**
     * @brief Read exactly `length` bytes using the current default timeout.
     */
    std::vector<uint8_t> read(size_t length);

    /**
     * @brief Read exactly `length` bytes with an explicit timeout override.
     */
    std::vector<uint8_t> read(size_t length, uint32_t timeout_ms);

    /**
     * @brief Drain any bytes waiting in the receive buffer.
     *
     * Implemented as a zero-timeout read; safe to call at any time.
     */
    void flush_input();

    // -------------------------------------------------------------------------
    // BBIO control
    // -------------------------------------------------------------------------

    /**
     * @brief Enter Binary Bitbang I/O mode.
     *
     * Sends up to 20 × 0x00 pulses and waits for the "BBIO1" banner.
     * Must be called once before entering any protocol mode.
     *
     * @return true on success.
     */
    bool enter_bbio();

    /**
     * @brief Exit BBIO mode and return Hydrabus to its interactive CLI.
     * @return true on success.
     */
    bool exit_bbio();

    /**
     * @brief Force a reset back to BBIO main mode (retries for up to 10 s).
     * @return true on success.
     */
    bool reset_to_bbio();

    /**
     * @brief Query the current mode identifier.
     * @return 4-character ASCII string (e.g. "SPI1").
     */
    std::string identify();

    // -------------------------------------------------------------------------
    // Accessors
    // -------------------------------------------------------------------------

    /** @brief True when the underlying port reports itself open. */
    bool is_open() const;

    /**
     * @brief Set the default timeout applied to all read()/write() calls
     *        that do not specify an explicit timeout.
     */
    void        set_timeout(uint32_t timeout_ms);
    uint32_t    get_timeout() const;

    /**
     * @brief Current protocol mode tag, set by Protocol::_enter().
     *        e.g. "SPI1", "I2C1", …
     */
    std::string mode;

private:
    std::shared_ptr<const ICommDriver> _driver;
    uint32_t                           _timeout_ms{DEFAULT_TIMEOUT_MS};
};

} // namespace HydraHAL

#endif // HYDRABUS_HPP
