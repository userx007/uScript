#ifndef HYDRABUS_SPI_HPP
#define HYDRABUS_SPI_HPP

#include "Protocol.hpp"
#include <optional>

namespace HydraHAL {

/**
 * @brief HydraBus SPI binary mode handler.
 *
 * Mirrors pyHydrabus.SPI with full-duplex bulk transfers and a
 * write-then-read helper that leverages HydraFW's optimised path.
 *
 * @example
 * @code
 * auto hb = std::make_shared<HydraHAL::Hydrabus>(driver);
 * hb->enter_bbio();
 *
 * HydraHAL::SPI spi(hb);
 * spi.set_speed(HydraHAL::SPI::Speed::SPI1_10M);
 * spi.set_cs(0);                              // assert CS
 * spi.bulk_write({0x9F});                     // send JEDEC ID command
 * auto id = spi.read(3);                      // read 3 bytes
 * spi.set_cs(1);                              // deassert CS
 * @endcode
 */
class SPI : public Protocol {

public:

    // -------------------------------------------------------------------------
    // Speed constants (match HydraFW bit patterns)
    // -------------------------------------------------------------------------

    enum class Speed : uint8_t {
        // SPI1 speeds
        SPI1_320K = 0b000,
        SPI1_650K = 0b001,
        SPI1_1M   = 0b010,
        SPI1_2M   = 0b011,
        SPI1_5M   = 0b100,
        SPI1_10M  = 0b101,
        SPI1_21M  = 0b110,
        SPI1_42M  = 0b111,
        // SPI2 speeds (same encoding, different peripheral)
        SPI2_160K = 0b000,
        SPI2_320K = 0b001,
        SPI2_650K = 0b010,
        SPI2_1M   = 0b011,
        SPI2_2M   = 0b100,
        SPI2_5M   = 0b101,
        SPI2_10M  = 0b110,
        SPI2_21M  = 0b111,
    };

    /**
     * @param hydrabus Open, BBIO-enabled Hydrabus instance.
     */
    explicit SPI(std::shared_ptr<Hydrabus> hydrabus);

    // -------------------------------------------------------------------------
    // Chip-select
    // -------------------------------------------------------------------------

    /**
     * @brief Get the current CS pin state (0 = low / asserted, 1 = high).
     */
    int  get_cs() const;

    /**
     * @brief Set the CS pin state.
     * @param level 0 to assert (pull low), 1 to deassert (pull high).
     * @return true on success.
     */
    bool set_cs(int level);

    // -------------------------------------------------------------------------
    // Data transfer
    // -------------------------------------------------------------------------

    /**
     * @brief Full-duplex bulk transfer (1–16 bytes).
     *
     * Sends `data` and simultaneously captures the MISO bytes.
     * Uses HydraFW bulk SPI transfer (0b0001xxxx).
     *
     * @param data  1–16 bytes to transmit.
     * @return MISO bytes (same length as data), or empty on error.
     * @note Logs LOG_ERROR and returns empty if data is empty or > 16 bytes.
     */
    std::vector<uint8_t> bulk_write(std::span<const uint8_t> data);

    /**
     * @brief HydraFW-optimised write-then-read operation.
     *
     * Sends a start condition, writes `data`, then reads `read_len` bytes,
     * all in a single firmware transaction (0b00000100 / 0b00000101).
     *
     * @param data      Bytes to transmit (may be empty for a pure read).
     * @param read_len  Number of bytes to read back.
     * @param manual_cs false (default) = firmware drives CS automatically;
     *                  true           = caller manages CS.
     * @return Read bytes, or empty optional on error.
     */
    std::optional<std::vector<uint8_t>> write_read(
            std::span<const uint8_t> data,
            size_t                   read_len,
            bool                     manual_cs = false);

    /**
     * @brief Write bytes (discards any MISO data).
     * @param manual_cs See write_read().
     */
    bool write(std::span<const uint8_t> data, bool manual_cs = false);

    /**
     * @brief Read bytes by clocking out 0xFF on MOSI.
     *
     * Automatically asserts / deasserts CS unless manual_cs is true.
     *
     * @param read_len  Number of bytes to read.
     * @param manual_cs See write_read().
     * @return Read bytes.
     */
    std::vector<uint8_t> read(size_t read_len, bool manual_cs = false);

    // -------------------------------------------------------------------------
    // Configuration
    // -------------------------------------------------------------------------

    /**
     * @brief Set the SPI clock speed.
     * @param speed One of the Speed enum values.
     * @return true on success.
     */
    bool set_speed(Speed speed);

    // ---- Clock polarity (CPOL) -----------------------------------------------

    /** @return 0 = idle low, 1 = idle high. */
    int  get_polarity() const;

    /** @param value 0 or 1. @return true on success. */
    bool set_polarity(int value);

    // ---- Clock phase (CPHA) --------------------------------------------------

    /** @return 0 = first edge, 1 = second edge. */
    int  get_phase() const;

    /** @param value 0 or 1. @return true on success. */
    bool set_phase(int value);

    // ---- SPI peripheral selector --------------------------------------------

    /** @return 1 = SPI1 (faster, up to 42 MHz), 0 = SPI2. */
    int  get_device() const;

    /** @param value 0 = SPI2, 1 = SPI1. @return true on success. */
    bool set_device(int value);

private:

    bool _configure_port();

    static constexpr uint8_t DEFAULT_CONFIG = 0b011; ///< SPI1, CPOL=0, CPHA=1

    uint8_t _config{DEFAULT_CONFIG};
    int     _cs_val{1};                              ///< Cached CS state
};

} // namespace HydraHAL

#endif //HYDRABUS_SPI_HPP