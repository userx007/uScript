#pragma once

#include "Protocol.hpp"
#include <optional>

namespace HydraHAL {

/**
 * @brief HydraBus SDIO binary mode handler.
 *
 * Covers SD/SDIO command transmission with no-response, short-response
 * (R1/R3/R7 — 4 bytes), and long-response (R2 — 16 bytes) variants,
 * as well as single-block data read and write operations.
 *
 * @example
 * @code
 * auto hb = std::make_shared<HydraHAL::Hydrabus>(driver);
 * hb->enter_bbio();
 *
 * HydraHAL::SDIO sdio(hb);
 * sdio.send_no(0, 0);                            // CMD0 – GO_IDLE_STATE
 * sdio.send_short(8, 0x000001AA);                // CMD8 – SEND_IF_COND
 * auto cid = sdio.send_long(2, 0);               // CMD2 – ALL_SEND_CID
 * auto block = sdio.read(17, 0x00000000);        // CMD17 – READ_SINGLE_BLOCK
 * @endcode
 */
class SDIO : public Protocol {
public:

    static constexpr size_t BLOCK_SIZE = 512;

    explicit SDIO(std::shared_ptr<Hydrabus> hydrabus);

    // -------------------------------------------------------------------------
    // Command variants
    // -------------------------------------------------------------------------

    /**
     * @brief Send a command with no response expected from the card.
     * @param cmd_id  SD command index (0–63).
     * @param cmd_arg 32-bit command argument.
     * @return true if firmware confirms transmission.
     */
    bool send_no(uint8_t cmd_id, uint32_t cmd_arg);

    /**
     * @brief Send a command and receive a short (4-byte) response.
     * @return 4 response bytes, or nullopt on error.
     */
    std::optional<std::vector<uint8_t>> send_short(uint8_t cmd_id, uint32_t cmd_arg);

    /**
     * @brief Send a command and receive a long (16-byte) response.
     * @return 16 response bytes, or nullopt on error.
     */
    std::optional<std::vector<uint8_t>> send_long(uint8_t cmd_id, uint32_t cmd_arg);

    // -------------------------------------------------------------------------
    // Data transfer (single block)
    // -------------------------------------------------------------------------

    /**
     * @brief Write a 512-byte block via a data-write command.
     * @param cmd_id  Command to use (typically CMD24 = WRITE_BLOCK).
     * @param cmd_arg Block address.
     * @param data    Exactly 512 bytes.
     * @return true on success.
     */
    bool write(uint8_t cmd_id, uint32_t cmd_arg, std::span<const uint8_t> data);

    /**
     * @brief Read a 512-byte block via a data-read command.
     * @param cmd_id  Command to use (typically CMD17 = READ_SINGLE_BLOCK).
     * @param cmd_arg Block address.
     * @return 512 bytes, or empty on error.
     */
    std::vector<uint8_t> read(uint8_t cmd_id, uint32_t cmd_arg);

    // -------------------------------------------------------------------------
    // Configuration
    // -------------------------------------------------------------------------

    /** @return 1 or 4 (bus width in bits). */
    int  get_bus_width() const;
    /** @param width 1 or 4. */
    bool set_bus_width(int width);

    /**
     * @brief Select clock frequency.
     * @return 0 = slow (~400 kHz), 1 = fast (~24 MHz).
     */
    int  get_frequency() const;
    /** @param freq 0 = slow, 1 = fast. */
    bool set_frequency(int freq);

private:
    bool _configure_port();

    static constexpr uint8_t DEFAULT_CONFIG = 0b00;
    uint8_t _config{DEFAULT_CONFIG};
};

} // namespace HydraHAL
