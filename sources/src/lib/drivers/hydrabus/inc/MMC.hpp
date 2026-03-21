#ifndef HYDRABUS_MMC_HPP
#define HYDRABUS_MMC_HPP

#include "Protocol.hpp"
#include <optional>

namespace HydraHAL {

/**
 * @brief HydraBus eMMC/MMC binary mode handler.
 *
 * Provides 512-byte block read/write and register access (CID, CSD, EXT_CSD).
 * Supports 1-bit and 4-bit bus widths.
 *
 * @example
 * @code
 * auto hb = std::make_shared<HydraHAL::Hydrabus>(driver);
 * hb->enter_bbio();
 *
 * HydraHAL::MMC mmc(hb);
 * auto cid = mmc.get_cid();
 * auto blk = mmc.read(0);    // read block 0
 * @endcode
 */
class MMC : public Protocol {
public:

    static constexpr size_t BLOCK_SIZE    = 512;
    static constexpr size_t REG_SIZE_STD  =  16;  ///< CID / CSD
    static constexpr size_t REG_SIZE_EXT  = 512;  ///< EXT_CSD

    explicit MMC(std::shared_ptr<Hydrabus> hydrabus);

    // -------------------------------------------------------------------------
    // Register access
    // -------------------------------------------------------------------------

    /** @brief Read the 16-byte CID register. */
    std::vector<uint8_t> get_cid();

    /** @brief Read the 16-byte CSD register. */
    std::vector<uint8_t> get_csd();

    /** @brief Read the 512-byte EXT_CSD register. */
    std::vector<uint8_t> get_ext_csd();

    // -------------------------------------------------------------------------
    // Block I/O
    // -------------------------------------------------------------------------

    /**
     * @brief Read a 512-byte block.
     * @param block_num Block address (0-based).
     * @return 512 bytes, or empty on error.
     */
    std::vector<uint8_t> read(uint32_t block_num);

    /**
     * @brief Write a 512-byte block.
     * @param data      Exactly 512 bytes.
     * @param block_num Block address (0-based).
     * @return true on success.
     */
    bool write(std::span<const uint8_t> data, uint32_t block_num);

    // -------------------------------------------------------------------------
    // Configuration
    // -------------------------------------------------------------------------

    /** @return 1 or 4 (bus width in bits). */
    int  get_bus_width() const;

    /**
     * @brief Set bus width.
     * @param width 1 or 4.
     */
    bool set_bus_width(int width);

private:
    bool _configure_port();

    static constexpr uint8_t DEFAULT_CONFIG = 0b0;
    uint8_t _config{DEFAULT_CONFIG};
};

} // namespace HydraHAL

#endif //HYDRABUS_MMC_HPP