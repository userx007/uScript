#ifndef UHEXLIFYUTILS_HPP
#define UHEXLIFYUTILS_HPP

#include <vector>
#include <string>
#include <stdexcept>
#include <cstring>
#include <type_traits>
#include <algorithm>


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @namespace hexutils
 * @brief Provides utility functions for hex representations.
 */
/*--------------------------------------------------------------------------------------------------------*/

namespace hexutils
{

/**
 * @brief Enumeration for specifying endianness.
 */
enum class Endianness {
    Little, /**< Little-endian format */
    Big     /**< Big-endian format */
};

/*--------------------------------------------------------------------------------------------------------*/
/**
 * @namespace internal
 * @brief Contains internal helper functions for hexutils.
 */
/*--------------------------------------------------------------------------------------------------------*/
namespace internal
{
constexpr char g_pstrHexDigits[] = "0123456789ABCDEF";



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Checks if the system is little-endian.
 * @return True if the system is little-endian, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/

bool is_system_little_endian()
{
    uint16_t test = 0x1;
    return *reinterpret_cast<uint8_t*>(&test) == 0x1;

} /* is_system_little_endian() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a hexadecimal character to its byte value.
 * @param c The hexadecimal character.
 * @return The byte value of the hexadecimal character.
 * @throws std::invalid_argument if the character is not a valid hexadecimal character.
 */
/*--------------------------------------------------------------------------------------------------------*/

uint8_t hex_char_to_byte(char c)
{
    if ('0' <= c && c <= '9') return c - '0';
    if ('A' <= c && c <= 'F') return c - 'A' + 10;
    if ('a' <= c && c <= 'f') return c - 'a' + 10;
    throw std::invalid_argument("Invalid hex character");

} /* hex_char_to_byte() */

}  /* namespace internal */


inline bool isHexlified(const std::string& input)
{
    static const std::regex pattern("^(?:[0-9A-Fa-f]{2})+$");
    return std::regex_match(input, pattern);

} /* isHexlified() */


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a buffer of bytes to a hexadecimal string.
 * @param InBuffer The input buffer of bytes.
 * @param szOffset The offset in the input buffer to start conversion.
 * @param szNrElems The number of elements to convert.
 * @param OutBuffer The output string to store the hexadecimal representation.
 * @return True if the conversion was successful, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline bool stringHexlify(const std::vector<uint8_t>& InBuffer, size_t szOffset, size_t szNrElems, std::string& OutBuffer)
{
    bool bRetVal = false;

    do {
        if (szOffset >= InBuffer.size()) {
            break;
        }

        szNrElems = std::min(szNrElems, InBuffer.size() - szOffset);
        OutBuffer.clear();
        OutBuffer.reserve(szNrElems * 2);

        for (size_t i = 0; i < szNrElems; ++i) {
            uint8_t byte = InBuffer[szOffset + i];
            OutBuffer.push_back(internal::g_pstrHexDigits[(byte >> 4) & 0xF]);
            OutBuffer.push_back(internal::g_pstrHexDigits[byte & 0xF]);
        }

        bRetVal = true;
    } while (false);

    return bRetVal;

} /* stringHexlify() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a hexadecimal string to a buffer of bytes.
 * @param hex The input hexadecimal string.
 * @param result The output buffer to store the bytes.
 * @return True if the conversion was successful, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline bool stringUnhexlify(const std::string& hex, std::vector<uint8_t>& result)
{
    bool bRetVal = false;

    do {
        if (hex.size() % 2 != 0) {
            break;
        }

        result.clear();
        result.reserve(hex.size() / 2);

        try {
            for (size_t i = 0; i < hex.size(); i += 2) {
                uint8_t high = internal::hex_char_to_byte(hex[i]);
                uint8_t low  = internal::hex_char_to_byte(hex[i + 1]);
                result.push_back((high << 4) | low);
            }
            bRetVal = true;
        } catch (const std::invalid_argument&) {
            break;
        }

    } while (false);

    return bRetVal;

} /* stringUnhexlify() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a buffer of any trivially copyable type to a hexadecimal string.
 * @tparam T The type of elements in the input buffer.
 * @param data The input buffer of elements.
 * @param out The output string to store the hexadecimal representation.
 * @param endian The endianness to use for the conversion.
 * @return True if the conversion was successful, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/

template<typename T>
bool stringHexlifyAny(const std::vector<T>& data, std::string& out, Endianness endian = Endianness::Little)
{
    static_assert(std::is_trivially_copyable<T>::value, "T must be trivially copyable");

    const uint8_t* bytePtr = reinterpret_cast<const uint8_t*>(data.data());
    size_t byteCount = data.size() * sizeof(T);
    out.clear();
    out.reserve(byteCount * 2 + 2);

    // Add endianness marker
    uint8_t marker = (endian == Endianness::Little) ? 0x4C : 0x42; // 'L' or 'B'
    out.push_back(internal::g_pstrHexDigits[(marker >> 4) & 0xF]);
    out.push_back(internal::g_pstrHexDigits[marker & 0xF]);

    bool systemIsLE = internal::is_system_little_endian();

    if ((endian == Endianness::Big && systemIsLE) || (endian == Endianness::Little && !systemIsLE)) {
        // Reverse byte order of each element
        size_t elemSize = sizeof(T);
        for (size_t i = 0; i < data.size(); ++i) {
            const uint8_t* elemPtr = reinterpret_cast<const uint8_t*>(&data[i]);
            for (size_t j = 0; j < elemSize; ++j) {
                uint8_t byte = elemPtr[elemSize - 1 - j];
                out.push_back(internal::g_pstrHexDigits[(byte >> 4) & 0xF]);
                out.push_back(internal::g_pstrHexDigits[byte & 0xF]);
            }
        }
    } else {
        // Native byte order
        for (size_t i = 0; i < byteCount; ++i) {
            uint8_t byte = bytePtr[i];
            out.push_back(internal::g_pstrHexDigits[(byte >> 4) & 0xF]);
            out.push_back(internal::g_pstrHexDigits[byte & 0xF]);
        }
    }

    return true;

} /* stringHexlifyAny() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a hexadecimal string to a buffer of any trivially copyable type.
 * @tparam T The type of elements in the output buffer.
 * @param hex The input hexadecimal string.
 * @param result The output buffer to store the elements.
 * @return True if the conversion was successful, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/

template<typename T>
bool stringUnhexlifyAny(const std::string& hex, std::vector<T>& result)
{
    static_assert(std::is_trivially_copyable<T>::value, "T must be trivially copyable");

    bool bRetVal = false;

    do {
        if (hex.size() < 2 || hex.size() % 2 != 0) {
            break;
        }

        uint8_t marker = (internal::hex_char_to_byte(hex[0]) << 4) | internal::hex_char_to_byte(hex[1]);
        Endianness targetEndian;
        if (marker == 0x4C) targetEndian = Endianness::Little;
        else if (marker == 0x42) targetEndian = Endianness::Big;
        else break;

        size_t byteCount = (hex.size() - 2) / 2;
        if (byteCount % sizeof(T) != 0) {
            break;
        }

        std::vector<uint8_t> bytes;
        bytes.reserve(byteCount);
        try {
            for (size_t i = 0; i < byteCount; ++i) {
                uint8_t high = internal::hex_char_to_byte(hex[2 + 2 * i]);
                uint8_t low  = internal::hex_char_to_byte(hex[2 + 2 * i + 1]);
                bytes.push_back((high << 4) | low);
            }
        } catch (const std::invalid_argument&) {
            break;
        }

        bool systemIsLE = internal::is_system_little_endian();
        if ((targetEndian == Endianness::Little && !systemIsLE) ||
                (targetEndian == Endianness::Big && systemIsLE)) {
            size_t elemSize = sizeof(T);
            for (size_t i = 0; i < byteCount; i += elemSize) {
                std::reverse(bytes.begin() + i, bytes.begin() + i + elemSize);
            }
        }

        result.clear();
        result.resize(byteCount / sizeof(T));
        std::memcpy(result.data(), bytes.data(), byteCount);
        bRetVal = true;

    } while (false);

    return bRetVal;

} /* stringUnhexlifyAny() */

} // namespace hexutils


#endif // UHEXLIFYUTILS_HPP
