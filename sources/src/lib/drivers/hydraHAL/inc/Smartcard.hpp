#pragma once

#include "Protocol.hpp"
#include <optional>

namespace HydraHAL {

/**
 * @brief HydraBus Smartcard (ISO 7816) binary mode handler.
 *
 * Provides RST pin control, baud/prescaler/guard-time configuration,
 * ATR retrieval, and a write-then-read primitive that covers both
 * pure writes and pure reads.
 *
 * @example
 * @code
 * auto hb = std::make_shared<HydraHAL::Hydrabus>(driver);
 * hb->enter_bbio();
 *
 * HydraHAL::Smartcard sc(hb);
 * sc.set_prescaler(12);
 * sc.set_baud(9600);
 * sc.set_rst(1); sc.set_rst(0);
 * auto atr = sc.get_atr();
 * @endcode
 */
class Smartcard : public Protocol {
public:

    explicit Smartcard(std::shared_ptr<Hydrabus> hydrabus);

    // -------------------------------------------------------------------------
    // Data transfer
    // -------------------------------------------------------------------------

    /**
     * @brief Write-then-read operation (HydraFW 0b00000100).
     * @param data     Bytes to transmit.
     * @param read_len Number of bytes to read back.
     * @return Read bytes, or nullopt on error.
     */
    std::optional<std::vector<uint8_t>> write_read(
            std::span<const uint8_t> data,
            size_t                   read_len);

    bool                 write(std::span<const uint8_t> data);
    std::vector<uint8_t> read(size_t length);

    // -------------------------------------------------------------------------
    // ATR
    // -------------------------------------------------------------------------

    /**
     * @brief Retrieve the card's Answer-To-Reset (ATR) byte string.
     * @return ATR bytes (variable length).
     */
    std::vector<uint8_t> get_atr();

    // -------------------------------------------------------------------------
    // RST pin
    // -------------------------------------------------------------------------

    /** @return Current RST pin level. */
    int  get_rst() const;
    /** @param level 0 or 1. @return true on success. */
    bool set_rst(int level);

    // -------------------------------------------------------------------------
    // Configuration
    // -------------------------------------------------------------------------

    uint32_t get_baud()       const;
    bool     set_baud(uint32_t baud);

    uint8_t  get_prescaler()  const;
    bool     set_prescaler(uint8_t value);

    uint8_t  get_guardtime()  const;
    bool     set_guardtime(uint8_t value);

    bool     get_pullup()     const;
    bool     set_pullup(bool enable);

private:
    bool _configure_port();

    uint8_t  _config    {0b0000};
    int      _rst       {1};
    uint32_t _baud      {9600};
    uint8_t  _prescaler {12};
    uint8_t  _guardtime {16};
};

} // namespace HydraHAL
