#ifndef HYDRABUS_SWD_HPP
#define HYDRABUS_SWD_HPP

#include "RawWire.hpp"

namespace HydraHAL {

/**
 * @brief ARM Serial Wire Debug (SWD) handler built on top of RawWire.
 *
 * Provides DP/AP register access, bus initialisation (JTAG-to-SWD
 * sequence), multi-drop initialisation per ADIv6, and a full AP scanner.
 *
 * All transactions automatically retry on WAIT responses from the target.
 *
 * @example
 * @code
 * auto hb = std::make_shared<HydraHAL::Hydrabus>(driver);
 * hb->enter_bbio();
 *
 * HydraHAL::SWD swd(hb);
 * swd.bus_init();
 * uint32_t idcode = swd.read_dp(0x00);
 * swd.write_dp(0x04, 0x50000000);   // power-up system + debug
 * swd.scan_bus();
 * @endcode
 */

class SWD : public RawWire {

public:

    explicit SWD(std::shared_ptr<Hydrabus> hydrabus);

    // -------------------------------------------------------------------------
    // Bus initialisation
    // -------------------------------------------------------------------------

    /**
     * @brief Send the JTAG-to-SWD token sequence followed by sync clocks.
     *
     * Must be called once after construction before any DP/AP access.
     */
    void bus_init();

    /**
     * @brief Initialise a multi-drop SWD bus and select the DP at `addr`.
     *
     * Follows the ADIv6 dormant-to-active sequence.
     *
     * @param addr DP target address (use 0 for single-drop).
     */
    void multidrop_init(uint32_t addr = 0);

    // -------------------------------------------------------------------------
    // Debug Port (DP) access
    // -------------------------------------------------------------------------

    /**
     * @brief Read a 32-bit DP register.
     *
     * @param addr    DP register address (bits [3:2] used).
     * @param to_ap   Set to 1 to address the AP bank instead of the DP.
     * @return Register value.
     * @throws std::runtime_error on FAULT response.
     */
    uint32_t read_dp(uint8_t addr, int to_ap = 0);

    /**
     * @brief Write a 32-bit DP register.
     *
     * @param addr          DP register address.
     * @param value         Value to write.
     * @param to_ap         Set to 1 to address the AP bank.
     * @param ignore_status Skip ACK checking (useful during multi-drop init).
     * @throws std::runtime_error on FAULT response (unless ignore_status).
     */
    void write_dp(uint8_t addr, uint32_t value,
                  int  to_ap        = 0,
                  bool ignore_status = false);

    // -------------------------------------------------------------------------
    // Access Port (AP) access
    // -------------------------------------------------------------------------

    /**
     * @brief Read a 32-bit AP register.
     *
     * Configures the SELECT register automatically.
     *
     * @param ap_address AP address on the SWD bus (0–255).
     * @param bank       AP register bank address (e.g. 0xFC for IDR).
     * @return Register value.
     */
    uint32_t read_ap(uint8_t ap_address, uint8_t bank);

    /**
     * @brief Write a 32-bit AP register.
     *
     * Configures the SELECT register automatically.
     *
     * @param ap_address AP address on the SWD bus (0–255).
     * @param bank       AP register bank address.
     * @param value      Value to write.
     */
    void write_ap(uint8_t ap_address, uint8_t bank, uint32_t value);

    // -------------------------------------------------------------------------
    // Utilities
    // -------------------------------------------------------------------------

    /**
     * @brief Scan all 256 AP slots and print those with valid IDR values.
     */
    void scan_bus();

    /**
     * @brief Write to the DP ABORT register to abort a pending AP transaction.
     *
     * @param flags Bits to set in the ABORT register (default = all fault bits).
     */
    void abort(uint8_t flags = 0b11111);

private:

    /** @brief Apply odd parity to the request header byte. */
    uint8_t _apply_dp_parity(uint8_t value) const;

    /** @brief Send a sync byte (0x00) after a read/write transaction. */
    void _sync();
};

} // namespace HydraHAL

#endif //HYDRABUS_SWD_HPP
