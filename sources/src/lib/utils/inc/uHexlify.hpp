#ifndef UHEXLIFYUTILS_HPP
#define UHEXLIFYUTILS_HPP

#include <vector>
#include <string>
#include <string_view>
#include <stdexcept>
#include <cstring>
#include <type_traits>
#include <algorithm>
#include <optional>
#include <span>
#include <bit>

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
enum class Endianness : uint8_t {
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
// Lookup tables for fast hex conversion
constexpr char g_hexDigitsUpper[] = "0123456789ABCDEF";
constexpr char g_hexDigitsLower[] = "0123456789abcdef";

/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Checks if the system is little-endian at compile time.
 * @return True if the system is little-endian, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] constexpr bool is_system_little_endian() noexcept
{
    if constexpr (std::endian::native == std::endian::little) {
        return true;
    } else if constexpr (std::endian::native == std::endian::big) {
        return false;
    } else {
        // Mixed endian - fallback to runtime check
        return std::endian::native == std::endian::little;
    }
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a hexadecimal character to its byte value (constexpr).
 * @param c The hexadecimal character.
 * @return The byte value (0-15) or -1 if invalid.
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] constexpr int hex_char_to_nibble(char c) noexcept
{
    if ('0' <= c && c <= '9') return c - '0';
    if ('A' <= c && c <= 'F') return c - 'A' + 10;
    if ('a' <= c && c <= 'f') return c - 'a' + 10;
    return -1;
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a hexadecimal character to its byte value (throwing version).
 * @param c The hexadecimal character.
 * @return The byte value of the hexadecimal character.
 * @throws std::invalid_argument if the character is not a valid hexadecimal character.
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] inline uint8_t hex_char_to_byte(char c)
{
    int nibble = hex_char_to_nibble(c);
    if (nibble < 0) {
        throw std::invalid_argument("Invalid hex character");
    }
    return static_cast<uint8_t>(nibble);
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Check if a character is a valid hex digit (constexpr).
 * @param c Character to check
 * @return true if valid hex digit
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] constexpr bool is_hex_char(char c) noexcept
{
    return ('0' <= c && c <= '9') || ('A' <= c && c <= 'F') || ('a' <= c && c <= 'f');
}

}  /* namespace internal */


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Check if a string is a valid hexadecimal string (optimized, no regex).
 * @param input The input string to check.
 * @return True if the string is a valid hex string (even length, all hex digits).
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] inline bool isHexlified(std::string_view input) noexcept
{
    // Must have even length and not be empty
    if (input.empty() || (input.size() % 2) != 0) {
        return false;
    }

    // Check all characters are valid hex digits
    return std::all_of(input.begin(), input.end(), internal::is_hex_char);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a buffer of bytes to a hexadecimal string.
 * @param input The input span of bytes.
 * @param offset The offset in the input buffer to start conversion.
 * @param count The number of elements to convert.
 * @param uppercase Whether to use uppercase hex digits (default: true).
 * @return Hexadecimal string representation.
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] inline std::string stringHexlify(std::span<const uint8_t> input, 
                                               size_t offset = 0, 
                                               size_t count = std::string::npos,
                                               bool uppercase = true)
{
    if (offset >= input.size()) {
        return "";
    }

    count = std::min(count, input.size() - offset);
    std::string result;
    result.reserve(count * 2);

    const char* hexDigits = uppercase ? internal::g_hexDigitsUpper : internal::g_hexDigitsLower;

    for (size_t i = 0; i < count; ++i) {
        uint8_t byte = input[offset + i];
        result.push_back(hexDigits[(byte >> 4) & 0xF]);
        result.push_back(hexDigits[byte & 0xF]);
    }

    return result;
}

// Overload for vector
[[nodiscard]] inline std::string stringHexlify(const std::vector<uint8_t>& input, 
                                               size_t offset = 0, 
                                               size_t count = std::string::npos,
                                               bool uppercase = true)
{
    return stringHexlify(std::span<const uint8_t>(input), offset, count, uppercase);
}

// Legacy interface (backward compatible)
[[nodiscard]] inline bool stringHexlify(const std::vector<uint8_t>& InBuffer, 
                                        size_t szOffset, 
                                        size_t szNrElems, 
                                        std::string& OutBuffer)
{
    if (szOffset >= InBuffer.size()) {
        return false;
    }

    OutBuffer = stringHexlify(InBuffer, szOffset, szNrElems);
    return true;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a hexadecimal string to a buffer of bytes (optimized).
 * @param hex The input hexadecimal string.
 * @return Optional vector of bytes, nullopt if conversion failed.
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] inline std::optional<std::vector<uint8_t>> stringUnhexlifyOpt(std::string_view hex) noexcept
{
    // Must have even length
    if (hex.size() % 2 != 0) {
        return std::nullopt;
    }

    std::vector<uint8_t> result;
    result.reserve(hex.size() / 2);

    for (size_t i = 0; i < hex.size(); i += 2) {
        int high = internal::hex_char_to_nibble(hex[i]);
        int low = internal::hex_char_to_nibble(hex[i + 1]);
        
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        
        result.push_back(static_cast<uint8_t>((high << 4) | low));
    }

    return result;
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a hexadecimal string to a buffer of bytes (legacy interface).
 * @param hex The input hexadecimal string.
 * @param result The output buffer to store the bytes.
 * @return True if the conversion was successful, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] inline bool stringUnhexlify(std::string_view hex, std::vector<uint8_t>& result) noexcept
{
    auto opt = stringUnhexlifyOpt(hex);
    if (opt) {
        result = std::move(*opt);
        return true;
    }
    return false;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a hexadecimal string of type H"..." to a buffer of bytes.
 * @param input The input hexadecimal string.
 * @return Optional vector of bytes, nullopt if conversion failed.
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] inline std::optional<std::vector<uint8_t>> hexstringToVectorOpt(std::string_view input) noexcept
{
    std::string_view view = input;

    // Check for H"..." format
    if (view.size() >= 3 && view.starts_with("H\"") && view.back() == '"') {
        view.remove_prefix(2); // Remove H"
        view.remove_suffix(1); // Remove trailing "
    }

    return stringUnhexlifyOpt(view);
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a hexadecimal string of type H"..." to a buffer of bytes (legacy).
 * @param input The input hexadecimal string.
 * @param result The output buffer to store the bytes.
 * @return True if the conversion was successful, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] inline bool hexstringToVector(std::string_view input, std::vector<uint8_t>& result) noexcept
{
    auto opt = hexstringToVectorOpt(input);
    if (opt) {
        result = std::move(*opt);
        return true;
    }
    return false;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a buffer of any trivially copyable type to a hexadecimal string.
 * @tparam T The type of elements in the input buffer.
 * @param data The input span of elements.
 * @param endian The endianness to use for the conversion.
 * @param uppercase Whether to use uppercase hex digits (default: true).
 * @return Hexadecimal string with endianness marker.
 */
/*--------------------------------------------------------------------------------------------------------*/
template<typename T>
    requires std::is_trivially_copyable_v<T>
[[nodiscard]] std::string stringHexlifyAny(std::span<const T> data, 
                                           Endianness endian = Endianness::Little,
                                           bool uppercase = true)
{
    const uint8_t* bytePtr = reinterpret_cast<const uint8_t*>(data.data());
    size_t byteCount = data.size() * sizeof(T);
    
    std::string out;
    out.reserve(byteCount * 2 + 2);

    const char* hexDigits = uppercase ? internal::g_hexDigitsUpper : internal::g_hexDigitsLower;

    // Add endianness marker
    uint8_t marker = (endian == Endianness::Little) ? 0x4C : 0x42; // 'L' or 'B'
    out.push_back(hexDigits[(marker >> 4) & 0xF]);
    out.push_back(hexDigits[marker & 0xF]);

    constexpr bool systemIsLE = internal::is_system_little_endian();

    if ((endian == Endianness::Big && systemIsLE) || 
                  (endian == Endianness::Little && !systemIsLE)) {
        // Reverse byte order of each element
        constexpr size_t elemSize = sizeof(T);
        for (size_t i = 0; i < data.size(); ++i) {
            const uint8_t* elemPtr = reinterpret_cast<const uint8_t*>(&data[i]);
            for (size_t j = 0; j < elemSize; ++j) {
                uint8_t byte = elemPtr[elemSize - 1 - j];
                out.push_back(hexDigits[(byte >> 4) & 0xF]);
                out.push_back(hexDigits[byte & 0xF]);
            }
        }
    } else {
        // Native byte order - direct conversion
        for (size_t i = 0; i < byteCount; ++i) {
            uint8_t byte = bytePtr[i];
            out.push_back(hexDigits[(byte >> 4) & 0xF]);
            out.push_back(hexDigits[byte & 0xF]);
        }
    }

    return out;
}

// Overload for vector
template<typename T>
    requires std::is_trivially_copyable_v<T>
[[nodiscard]] inline std::string stringHexlifyAny(const std::vector<T>& data, 
                                                   Endianness endian = Endianness::Little,
                                                   bool uppercase = true)
{
    return stringHexlifyAny(std::span<const T>(data), endian, uppercase);
}

// Legacy interface
template<typename T>
    requires std::is_trivially_copyable_v<T>
[[nodiscard]] inline bool stringHexlifyAny(const std::vector<T>& data, 
                                           std::string& out, 
                                           Endianness endian = Endianness::Little)
{
    out = stringHexlifyAny(data, endian);
    return true;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a hexadecimal string to a buffer of any trivially copyable type.
 * @tparam T The type of elements in the output buffer.
 * @param hex The input hexadecimal string.
 * @return Optional vector of elements, nullopt if conversion failed.
 */
/*--------------------------------------------------------------------------------------------------------*/
template<typename T>
    requires std::is_trivially_copyable_v<T>
[[nodiscard]] std::optional<std::vector<T>> stringUnhexlifyAnyOpt(std::string_view hex) noexcept
{
    if (hex.size() < 2 || hex.size() % 2 != 0) {
        return std::nullopt;
    }

    // Parse endianness marker
    int markerHigh = internal::hex_char_to_nibble(hex[0]);
    int markerLow = internal::hex_char_to_nibble(hex[1]);
    
    if (markerHigh < 0 || markerLow < 0) {
        return std::nullopt;
    }

    uint8_t marker = static_cast<uint8_t>((markerHigh << 4) | markerLow);
    
    Endianness targetEndian;
    if (marker == 0x4C) {
        targetEndian = Endianness::Little;
    } else if (marker == 0x42) {
        targetEndian = Endianness::Big;
    } else {
        return std::nullopt;
    }

    size_t byteCount = (hex.size() - 2) / 2;
    if (byteCount % sizeof(T) != 0) {
        return std::nullopt;
    }

    // Convert hex to bytes
    std::vector<uint8_t> bytes;
    bytes.reserve(byteCount);
    
    for (size_t i = 0; i < byteCount; ++i) {
        int high = internal::hex_char_to_nibble(hex[2 + 2 * i]);
        int low = internal::hex_char_to_nibble(hex[2 + 2 * i + 1]);
        
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        
        bytes.push_back(static_cast<uint8_t>((high << 4) | low));
    }

    // Handle endianness conversion
    constexpr bool systemIsLE = internal::is_system_little_endian();
    if ((targetEndian == Endianness::Little && !systemIsLE) ||
        (targetEndian == Endianness::Big && systemIsLE)) {
        constexpr size_t elemSize = sizeof(T);
        for (size_t i = 0; i < byteCount; i += elemSize) {
            std::reverse(bytes.begin() + i, bytes.begin() + i + elemSize);
        }
    }

    // Convert bytes to result type
    std::vector<T> result;
    result.resize(byteCount / sizeof(T));
    std::memcpy(result.data(), bytes.data(), byteCount);
    
    return result;
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a hexadecimal string to a buffer of any trivially copyable type (legacy).
 * @tparam T The type of elements in the output buffer.
 * @param hex The input hexadecimal string.
 * @param result The output buffer to store the elements.
 * @return True if the conversion was successful, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/
template<typename T>
    requires std::is_trivially_copyable_v<T>
[[nodiscard]] inline bool stringUnhexlifyAny(std::string_view hex, std::vector<T>& result) noexcept
{
    auto opt = stringUnhexlifyAnyOpt<T>(hex);
    if (opt) {
        result = std::move(*opt);
        return true;
    }
    return false;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Convert bytes to hex string with custom separator
 * @param input Input bytes
 * @param separator Separator between hex pairs (e.g., ":", " ", "-")
 * @param uppercase Whether to use uppercase hex digits
 * @return Formatted hex string
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] inline std::string toHexString(std::span<const uint8_t> input,
                                             std::string_view separator = "",
                                             bool uppercase = true)
{
    if (input.empty()) {
        return "";
    }

    const char* hexDigits = uppercase ? internal::g_hexDigitsUpper : internal::g_hexDigitsLower;
    
    std::string result;
    result.reserve(input.size() * (2 + separator.size()) - separator.size());

    for (size_t i = 0; i < input.size(); ++i) {
        if (i > 0 && !separator.empty()) {
            result.append(separator);
        }
        uint8_t byte = input[i];
        result.push_back(hexDigits[(byte >> 4) & 0xF]);
        result.push_back(hexDigits[byte & 0xF]);
    }

    return result;
}

// Overload for vector
[[nodiscard]] inline std::string toHexString(const std::vector<uint8_t>& input,
                                             std::string_view separator = "",
                                             bool uppercase = true)
{
    return toHexString(std::span<const uint8_t>(input), separator, uppercase);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Convert single byte to two-character hex string
 * @param byte Input byte
 * @param uppercase Whether to use uppercase hex digits
 * @return Two-character hex string
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] constexpr std::array<char, 2> byteToHex(uint8_t byte, bool uppercase = true) noexcept
{
    const char* hexDigits = uppercase ? internal::g_hexDigitsUpper : internal::g_hexDigitsLower;
    return {hexDigits[(byte >> 4) & 0xF], hexDigits[byte & 0xF]};
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Parse two hex characters into a byte
 * @param high High nibble character
 * @param low Low nibble character
 * @return Optional byte value, nullopt if invalid
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] constexpr std::optional<uint8_t> hexToByte(char high, char low) noexcept
{
    int h = internal::hex_char_to_nibble(high);
    int l = internal::hex_char_to_nibble(low);
    
    if (h < 0 || l < 0) {
        return std::nullopt;
    }
    
    return static_cast<uint8_t>((h << 4) | l);
}

} // namespace hexutils

#endif // UHEXLIFYUTILS_HPP
