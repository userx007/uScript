#pragma once

#include "Protocol.hpp"
#include <optional>
#include <vector>

namespace HydraHAL {

/**
 * @brief HydraBus I2C binary mode handler.
 *
 * Provides start/stop conditions, byte-level read/write, a bulk-write path
 * and the firmware-optimised write-then-read transaction.  Also includes
 * a bus scanner that probes all 7-bit addresses.
 *
 * @example
 * @code
 * // Read 64 bytes from an I2C EEPROM at address 0x50
 * auto hb = std::make_shared<HydraHAL::Hydrabus>(driver);
 * hb->enter_bbio();
 *
 * HydraHAL::I2C i2c(hb);
 * i2c.set_speed(HydraHAL::I2C::Speed::I2C_100K);
 * i2c.set_pullup(true);
 *
 * i2c.start();
 * i2c.bulk_write({0xA0, 0x00});   // address + register
 * auto data = i2c.write_read({0xA1}, 64);
 * i2c.stop();
 * @endcode
 */
class I2C : public Protocol {
public:

    enum class Speed : uint8_t {
        I2C_50K  = 0b00,
        I2C_100K = 0b01,
        I2C_400K = 0b10,
        I2C_1M   = 0b11,
    };

    explicit I2C(std::shared_ptr<Hydrabus> hydrabus);

    // -------------------------------------------------------------------------
    // Bus conditions
    // -------------------------------------------------------------------------

    /** @brief Send an I2C START condition. @return true on success. */
    bool start();

    /** @brief Send an I2C STOP condition.  @return true on success. */
    bool stop();

    // -------------------------------------------------------------------------
    // Byte-level primitives
    // -------------------------------------------------------------------------

    /**
     * @brief Read one byte from the bus (does NOT send ACK/NACK).
     *
     * Caller must follow with send_ack() or send_nack() as appropriate.
     * @return The received byte.
     */
    uint8_t read_byte();

    /** @brief Send an ACK after read_byte(). @return true on success. */
    bool send_ack();

    /** @brief Send a NACK after read_byte(). @return true on success. */
    bool send_nack();

    // -------------------------------------------------------------------------
    // Bulk operations
    // -------------------------------------------------------------------------

    /**
     * @brief Bulk-write up to 16 bytes (HydraFW 0b0001xxxx path).
     *
     * @param data 1–16 bytes.
     * @return Per-byte ACK flags (0x00 = ACK, 0x01 = NACK), empty on error.
     * @note Logs LOG_ERROR and returns empty if data is empty or > 16 bytes.
     */
    std::vector<uint8_t> bulk_write(std::span<const uint8_t> data);

    /**
     * @brief Firmware-optimised write-then-read (HydraFW 0b00001000).
     *
     * Sends a START, writes `data`, then reads `read_len` bytes and sends
     * a STOP — all in one firmware transaction.
     *
     * @param data     Bytes to write (may be empty).
     * @param read_len Number of bytes to read back.
     * @return Read bytes, or nullopt on error.
     */
    std::optional<std::vector<uint8_t>> write_read(
            std::span<const uint8_t> data,
            size_t                   read_len);

    /**
     * @brief Write bytes (uses write_read with read_len = 0).
     */
    bool write(std::span<const uint8_t> data);

    /**
     * @brief Read `length` bytes, issuing ACKs for all but the last byte.
     * @return Read bytes.
     */
    std::vector<uint8_t> read(size_t length);

    // -------------------------------------------------------------------------
    // Configuration
    // -------------------------------------------------------------------------

    /** @brief Set the I2C bus speed. @return true on success. */
    bool set_speed(Speed speed);

    /**
     * @brief Set the clock-stretching timeout in clock cycles.
     *
     * Pass 0 to disable clock stretching.
     * @return true on success.
     */
    bool set_clock_stretch(uint32_t clocks);

    // ---- Pull-up resistors --------------------------------------------------

    /** @return true if pull-up resistors are enabled. */
    bool get_pullup() const;

    /** @param enable true to enable, false to disable. @return true on success. */
    bool set_pullup(bool enable);

    // ---- Bus scanner --------------------------------------------------------

    /**
     * @brief Scan all 7-bit I2C addresses and return those that ACK.
     * @return Sorted list of responding 7-bit addresses.
     */
    std::vector<uint8_t> scan();

private:
    bool _configure_port();

    static constexpr uint8_t DEFAULT_CONFIG = 0b000;
    uint8_t _config{DEFAULT_CONFIG};
};

} // namespace HydraHAL
