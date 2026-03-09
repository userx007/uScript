#pragma once

#include "Protocol.hpp"
#include <optional>

namespace HydraHAL {

/**
 * @brief HydraBus 1-Wire binary mode handler.
 *
 * Covers standard 1-Wire master operations (reset, bulk byte transfer)
 * and the HydraFW SWIO extension for reading/writing ARM Serial Wire
 * debug registers directly over the 1-Wire physical layer.
 *
 * @example
 * @code
 * auto hb = std::make_shared<HydraHAL::Hydrabus>(driver);
 * hb->enter_bbio();
 *
 * HydraHAL::OneWire ow(hb);
 * ow.set_pullup(true);
 * ow.reset();
 * ow.write({0xCC, 0x44});   // SKIP ROM + CONVERT T
 * @endcode
 */
class OneWire : public Protocol {
public:

    explicit OneWire(std::shared_ptr<Hydrabus> hydrabus);

    // -------------------------------------------------------------------------
    // Bus operations
    // -------------------------------------------------------------------------

    /**
     * @brief Send a 1-Wire bus reset pulse.
     * @return true (firmware does not return a presence-detect flag in
     *         binary mode; see the HydraFW wiki for details).
     */
    bool reset();

    /**
     * @brief Read a single byte from the bus.
     * @return The received byte.
     */
    uint8_t read_byte();

    /**
     * @brief Bulk-write up to 16 bytes (HydraFW 0b0001xxxx).
     *
     * @param data 1–16 bytes.
     * @throws std::invalid_argument if data is empty or > 16 bytes.
     */
    bool bulk_write(std::span<const uint8_t> data);

    /**
     * @brief Write an arbitrary-length buffer (auto-chunked into ≤16-byte
     *        bulk_write calls).
     */
    bool write(std::span<const uint8_t> data);

    /**
     * @brief Read `length` bytes from the bus.
     */
    std::vector<uint8_t> read(size_t length);

    // -------------------------------------------------------------------------
    // Configuration
    // -------------------------------------------------------------------------

    /** @return true if internal pull-up is enabled. */
    bool get_pullup() const;

    /**
     * @brief Enable or disable the internal pull-up resistor.
     * @return true on success.
     */
    bool set_pullup(bool enable);

    // -------------------------------------------------------------------------
    // SWIO (Serial Wire debug over 1-Wire physical layer)
    // -------------------------------------------------------------------------

    /**
     * @brief Initialise the bus in SWIO mode.
     *
     * Reconfigures the port for ARM Serial Wire debug access.
     * Must be called before any swio_read_reg / swio_write_reg operation.
     */
    bool swio_init();

    /**
     * @brief Read a 32-bit SWIO/SWD debug register.
     *
     * @param address Register address (1 byte).
     * @return Register value (32-bit, little-endian).
     */
    uint32_t swio_read_reg(uint8_t address);

    /**
     * @brief Write a 32-bit SWIO/SWD debug register.
     *
     * @param address Register address (1 byte).
     * @param value   32-bit value to write (little-endian).
     * @return true on success.
     */
    bool swio_write_reg(uint8_t address, uint32_t value);

private:
    bool _configure_port();

    static constexpr uint8_t DEFAULT_CONFIG = 0b100;   ///< Pull-up enabled by default
    uint8_t _config{DEFAULT_CONFIG};
};

} // namespace HydraHAL
