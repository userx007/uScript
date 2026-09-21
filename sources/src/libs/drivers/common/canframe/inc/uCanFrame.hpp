#ifndef UCANFRAME_HPP
#define UCANFRAME_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

/**
 * @brief Decoded CAN / CAN-FD receive frame.
 */
struct CanFrame
{
    bool is_extended = false;       ///< True → 29-bit extended ID
    bool is_remote   = false;       ///< True → RTR frame
    bool is_canfd    = false;       ///< True → CAN-FD frame
    bool brs         = false;       ///< True → BRS enabled (CAN-FD only)
    uint32_t id      = 0;           ///< CAN ID (11-bit or 29-bit)
    uint8_t dlc      = 0;           ///< DLC code (0-15 for CAN-FD, 0-8 for CAN)
    uint8_t len      = 0;           ///< Actual data byte count
    std::array<uint8_t, 64> data{}; ///< Payload bytes
};

namespace ucanframe {

// DLC ↔ length tables  (CAN-FD ISO 11898-1)
static constexpr std::array<uint8_t, 16> DLC_TO_LEN_TABLE = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};

/**
 * @brief Convert a CAN-FD DLC code to actual byte count.
 * @param dlc  DLC nibble (0x00 – 0x0F)
 * @return Byte count (0–64)
 */
inline uint8_t dlc_to_len(uint8_t dlc)
{
    if (dlc >= DLC_TO_LEN_TABLE.size()) {
        return 64;
    }
    return DLC_TO_LEN_TABLE[dlc];
}

/**
 * @brief Convert a byte count to the nearest valid CAN-FD DLC code.
 * @param len  Byte count (0–64)
 * @return DLC nibble
 */
inline uint8_t len_to_dlc(uint8_t len)
{
    if (len <= 8) {
        return len;
    }
    if (len <= 12) {
        return 9;
    }
    if (len <= 16) {
        return 10;
    }
    if (len <= 20) {
        return 11;
    }
    if (len <= 24) {
        return 12;
    }
    if (len <= 32) {
        return 13;
    }
    if (len <= 48) {
        return 14;
    }
    return 15;
}

}

#endif // UCANFRAME_HPP
