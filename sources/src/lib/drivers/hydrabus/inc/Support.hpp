#ifndef HYDRABUS_SUPPORT_HPP
#define HYDRABUS_SUPPORT_HPP

#include <vector>
#include <cstdint>
#include <stdexcept>
#include <array>

namespace HydraHAL {

/**
 * @brief Split a byte vector into chunks of at most `chunk_size` bytes.
 *
 * Mirrors Python's common.split() used throughout the protocol layer.
 */
inline std::vector<std::vector<uint8_t>> split(const std::vector<uint8_t>& seq,
                                                size_t chunk_size)
{
    std::vector<std::vector<uint8_t>> result;
    result.reserve((seq.size() + chunk_size - 1) / chunk_size);
    for (size_t i = 0; i < seq.size(); i += chunk_size) {
        result.emplace_back(seq.begin() + static_cast<ptrdiff_t>(i),
                            seq.begin() + static_cast<ptrdiff_t>(
                                std::min(i + chunk_size, seq.size())));
    }
    return result;
}

/**
 * @brief Set or clear a single bit in a byte at the given position.
 *
 * @param byte_val  Input byte.
 * @param bit       1 to set, 0 to clear.
 * @param position  Bit index (0 = LSB).
 * @return Modified byte.
 * @throws std::invalid_argument if bit is not 0 or 1.
 */
inline uint8_t set_bit(uint8_t byte_val, int bit, int position)
{
    if (bit == 1) {
        return static_cast<uint8_t>(byte_val | (1u << position));
    } else if (bit == 0) {
        return static_cast<uint8_t>(byte_val & ~(1u << position));
    }
    throw std::invalid_argument("set_bit: bit must be 0 or 1");
}

/**
 * @brief Pack a uint16_t into a 2-byte big-endian array.
 */
inline std::array<uint8_t, 2> u16_be(uint16_t v) {
    return { static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v & 0xFF) };
}

/**
 * @brief Pack a uint32_t into a 4-byte big-endian array.
 */
inline std::array<uint8_t, 4> u32_be(uint32_t v) {
    return {
        static_cast<uint8_t>((v >> 24) & 0xFF),
        static_cast<uint8_t>((v >> 16) & 0xFF),
        static_cast<uint8_t>((v >>  8) & 0xFF),
        static_cast<uint8_t>( v        & 0xFF)
    };
}

/**
 * @brief Pack a uint32_t into a 4-byte little-endian array.
 */
inline std::array<uint8_t, 4> u32_le(uint32_t v) {
    return {
        static_cast<uint8_t>( v        & 0xFF),
        static_cast<uint8_t>((v >>  8) & 0xFF),
        static_cast<uint8_t>((v >> 16) & 0xFF),
        static_cast<uint8_t>((v >> 24) & 0xFF)
    };
}

/**
 * @brief Decode a 4-byte little-endian buffer to uint32_t.
 */
inline uint32_t from_le32(const std::vector<uint8_t>& buf, size_t offset = 0) {
    return static_cast<uint32_t>(buf[offset])
         | static_cast<uint32_t>(buf[offset + 1]) <<  8
         | static_cast<uint32_t>(buf[offset + 2]) << 16
         | static_cast<uint32_t>(buf[offset + 3]) << 24;
}

/**
 * @brief Decode a 4-byte big-endian buffer to uint32_t.
 */
inline uint32_t from_be32(const std::vector<uint8_t>& buf, size_t offset = 0) {
    return static_cast<uint32_t>(buf[offset])     << 24
         | static_cast<uint32_t>(buf[offset + 1]) << 16
         | static_cast<uint32_t>(buf[offset + 2]) <<  8
         | static_cast<uint32_t>(buf[offset + 3]);
}

} // namespace HydraHAL

#endif //HYDRABUS_SUPPORT_HPP