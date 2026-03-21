#ifndef HYDRABUS_PROTOCOL_HPP
#define HYDRABUS_PROTOCOL_HPP

#include <memory>
#include <string>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "Hydrabus.hpp"
#include "AUXPin.hpp"

namespace HydraHAL {

/**
 * @brief Abstract base class for all HydraBus binary protocol modes.
 *
 * Manages the lifecycle of the underlying Hydrabus connection and exposes
 * the 4 AUX GPIO pins.  Concrete subclasses (SPI, I2C, UART, …) call the
 * protected helpers to send/receive bytes without repeating boilerplate.
 *
 * +-----------------+
 * | Ownership model |
 * +-----------------+
 * 
 * The caller is responsible for constructing both the ICommDriver and the
 * Hydrabus wrapper.  Passing the same Hydrabus instance to multiple Protocol
 * objects simultaneously is not supported — each Protocol instance owns its
 * BBIO session for the lifetime of the object.
 *
 * @code
 * auto driver = std::make_shared<MySerialDriver>("/dev/ttyACM0");
 * auto hb     = std::make_shared<HydraHAL::Hydrabus>(driver);
 * hb->enter_bbio();
 *
 * HydraHAL::SPI spi(hb);
 * spi.set_speed(SPI::Speed::SPI1_10M);
 * @endcode
 */

class Protocol {
public:
    /**
     * @param hydrabus  Open, BBIO-enabled Hydrabus instance.
     * @param name      4-byte mode identifier returned by the firmware
     *                  (e.g. "SPI1", "I2C1").
     * @param fname     Human-readable name used in log messages.
     * @param mode_byte Command byte sent to enter this mode.
     */
    Protocol(std::shared_ptr<Hydrabus> hydrabus,
             std::string               name,
             std::string               fname,
             uint8_t                   mode_byte);

    virtual ~Protocol() = default;

    Protocol(const Protocol&)            = delete;
    Protocol& operator=(const Protocol&) = delete;
    Protocol(Protocol&&)                 = default;
    Protocol& operator=(Protocol&&)      = default;

    // -------------------------------------------------------------------------
    // AUX GPIO
    // -------------------------------------------------------------------------

    /**
     * @brief Access one of the four AUX GPIO pins by index (0–3).
     * @throws std::out_of_range for index > 3.
     */
    AUXPin&       aux(size_t index);
    const AUXPin& aux(size_t index) const;

    // -------------------------------------------------------------------------
    // Utility
    // -------------------------------------------------------------------------

    /** @brief Identify the active firmware mode (wraps Hydrabus::identify). */
    std::string identify();

    /**
     * @brief Exit the current mode, reset to BBIO, and close the port.
     */
    void close();

    /** @brief Direct access to the underlying Hydrabus instance. */
    std::shared_ptr<Hydrabus>       hydrabus();
    std::shared_ptr<const Hydrabus> hydrabus() const;

protected:

    // -------------------------------------------------------------------------
    // I/O primitives (used by subclasses)
    // -------------------------------------------------------------------------

    bool                  _write(std::span<const uint8_t> data);
    bool                  _write_byte(uint8_t b);
    bool                  _write_u16_be(uint16_t v);
    bool                  _write_u32_be(uint32_t v);
    bool                  _write_u32_le(uint32_t v);

    std::vector<uint8_t>  _read(size_t n);
    std::vector<uint8_t>  _read_with_timeout(size_t n, uint32_t timeout_ms);
    uint8_t               _read_byte();

    /**
     * @brief Read one byte and return true if it equals `expected`.
     * Logs an error if the value differs.
     */
    bool _expect_byte(uint8_t expected, const char* context = nullptr);

    /** @brief Convenience: expect 0x01 (ACK). */
    bool _ack(const char* context = nullptr);

    // -------------------------------------------------------------------------
    // Mode management
    // -------------------------------------------------------------------------

    /**
     * @brief Send the mode-entry byte and verify the firmware echoes back
     *        the expected 4-byte mode name.
     * @return true on success.
     */
    bool _enter();

    /**
     * @brief Reset the firmware back to BBIO main mode.
     */
    bool _exit();

    std::shared_ptr<Hydrabus>   _hydrabus;
    std::string                 _name;       ///< e.g. "SPI1"
    std::string                 _fname;      ///< e.g. "SPI"
    uint8_t                     _mode_byte;

private:

    std::array<AUXPin, 4>       _aux_pins;
};

} // namespace HydraHAL

#endif //HYDRABUS_PROTOCOL_HPP