#ifndef UNUMERIC_UTILS_H
#define UNUMERIC_UTILS_H

#define UNUMERIC_USE_SSTREAM_FOR_FLOAT_CONVERSION 1U

#include "uLogger.hpp"

#include <charconv>
#include <string>
#include <string_view>
#include <algorithm>
#include <utility>
#include <sstream>
#include <iomanip>
#include <span>
#include <vector>
#include <cstdint>
#include <cctype>
#include <stdexcept>
#include <optional>
#include <type_traits>
#include <concepts>

#ifdef _MSC_VER
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#undef  LOG_HDR
#define LOG_HDR     LOG_STRING("NUMERIC    :");


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @namespace numeric
 * @brief Provides utility functions for numeric conversions from strings.
 */
/*--------------------------------------------------------------------------------------------------------*/

namespace numeric
{

/*--------------------------------------------------------------------------------------------------------*/
/**
 * @namespace concepts
 * @brief Type constraints for numeric conversions
 */
/*--------------------------------------------------------------------------------------------------------*/
namespace concepts
{
    template<typename T>
    concept SignedInteger = std::is_integral_v<T> && std::is_signed_v<T>;

    template<typename T>
    concept UnsignedInteger = std::is_integral_v<T> && std::is_unsigned_v<T>;

    template<typename T>
    concept FloatingPoint = std::is_floating_point_v<T>;

} // namespace concepts


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @namespace internal
 * @brief Contains internal helper functions for numeric utilities.
 */
/*--------------------------------------------------------------------------------------------------------*/
namespace internal
{

/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Removes leading and trailing whitespace from a string view (zero-copy).
 *
 * This internal utility function trims whitespace characters from both ends of the input string.
 * Returns a string_view for efficiency - no allocations.
 *
 * @param str The input string view to be trimmed.
 * @return A string_view with leading and trailing whitespace removed.
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] inline constexpr std::string_view trim(std::string_view str) noexcept
{
    // Find first non-whitespace character
    auto start = std::find_if_not(str.begin(), str.end(), 
        [](unsigned char c) { return std::isspace(c); });
    
    if (start == str.end()) {
        return std::string_view();
    }

    // Find last non-whitespace character
    auto end = std::find_if_not(str.rbegin(), str.rend(),
        [](unsigned char c) { return std::isspace(c); }).base();

    return str.substr(std::distance(str.begin(), start), 
                     std::distance(start, end));
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Detects the numeric base from a string prefix and returns the base along with the stripped string.
 *
 * This function examines the beginning of the input string to determine if it has a base prefix:
 * - "0x" or "0X" for hexadecimal (base 16)
 * - "0b" or "0B" for binary (base 2)
 * - A leading "0" followed by digits for octal (base 8)
 * If no prefix is found, base 10 is assumed.
 *
 * @param input The input string view potentially containing a base prefix.
 * @return A pair consisting of the detected base and the string view with the prefix removed.
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] inline constexpr std::pair<int, std::string_view> 
detect_base_and_strip_prefix(std::string_view input) noexcept
{
    int base = 10;
    std::string_view view = input;

    if (view.size() >= 2 && view[0] == '0') {
        char second = view[1];
        if (second == 'x' || second == 'X') {
            base = 16;
            view.remove_prefix(2);
        } else if (second == 'b' || second == 'B') {
            base = 2;
            view.remove_prefix(2);
        } else if (second >= '0' && second <= '9') {
            base = 8;
            view.remove_prefix(1);
        }
    }

    return {base, view};
}

} // namespace internal


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to a signed integer of type T with detailed error information.
 *
 * This function trims the input string, detects the numeric base from any prefix,
 * and attempts to convert the string to a signed integer using `std::from_chars`.
 *
 * @tparam T A signed integer type (e.g., int8_t, int32_t).
 * @param input The input string to convert.
 * @param output Reference to the variable where the result will be stored.
 * @return True if the conversion was successful, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/
template<concepts::SignedInteger T>
[[nodiscard]] bool string_to_signed(std::string_view input, T& output) noexcept
{
    std::string_view trimmed = internal::trim(input);
    if (trimmed.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Input is empty"));
        return false;
    }

    auto [base, view] = internal::detect_base_and_strip_prefix(trimmed);
    
    auto [ptr, ec] = std::from_chars(view.data(), view.data() + view.size(), output, base);
    
    if (ec == std::errc()) {
        return true;
    }

    if (ec == std::errc::invalid_argument) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid format:"); LOG_STRING(std::string(input)));
    } else if (ec == std::errc::result_out_of_range) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Value out of range:"); LOG_STRING(std::string(input)));
    } else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid number:"); LOG_STRING(std::string(input)));
    }

    return false;
}

// Overload for std::string for backward compatibility
template<concepts::SignedInteger T>
[[nodiscard]] inline bool string_to_signed(const std::string& input, T& output) noexcept
{
    return string_to_signed<T>(std::string_view(input), output);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to an unsigned integer of type T.
 *
 * This function trims the input string, detects the numeric base from any prefix
 * (e.g., "0x" for hex, "0b" for binary, "0" for octal), and attempts to convert
 * the string to an unsigned integer using `std::from_chars`.
 *
 * @tparam T An unsigned integer type (e.g., uint8_t, uint32_t).
 * @param input The input string to convert.
 * @param output Reference to the variable where the result will be stored.
 * @return True if the conversion was successful, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/
template<concepts::UnsignedInteger T>
[[nodiscard]] bool string_to_unsigned(std::string_view input, T& output) noexcept
{
    std::string_view trimmed = internal::trim(input);
    if (trimmed.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Input is empty"));
        return false;
    }

    auto [base, view] = internal::detect_base_and_strip_prefix(trimmed);
    
    auto [ptr, ec] = std::from_chars(view.data(), view.data() + view.size(), output, base);
    
    if (ec == std::errc()) {
        return true;
    }

    if (ec == std::errc::invalid_argument) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid format:"); LOG_STRING(std::string(input)));
    } else if (ec == std::errc::result_out_of_range) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Value out of range:"); LOG_STRING(std::string(input)));
    } else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid number:"); LOG_STRING(std::string(input)));
    }

    return false;
}

// Overload for std::string for backward compatibility
template<concepts::UnsignedInteger T>
[[nodiscard]] inline bool string_to_unsigned(const std::string& input, T& output) noexcept
{
    return string_to_unsigned<T>(std::string_view(input), output);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to a floating-point number of type T.
 *
 * This function trims the input string and attempts to convert it to a floating-point
 * value. It supports types like `float`, `double`, and `long double`.
 *
 * @tparam T A floating-point type (e.g., float, double, long double).
 * @param input The input string to convert.
 * @param output Reference to the variable where the result will be stored.
 * @return True if the conversion was successful, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/

#if (1 == UNUMERIC_USE_SSTREAM_FOR_FLOAT_CONVERSION)

template<concepts::FloatingPoint T>
[[nodiscard]] bool string_to_floating(std::string_view input, T& output) noexcept
{
    std::string_view trimmed = internal::trim(input);
    if (trimmed.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Input is empty"));
        return false;
    }

    // Need to create a string for istringstream
    std::string str(trimmed);
    std::istringstream iss(str);
    iss >> output;

    if (iss.fail() || !iss.eof()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid format or extra characters:"); LOG_STRING(str));
        return false;
    }

    return true;
}

#else

template<concepts::FloatingPoint T>
[[nodiscard]] bool string_to_floating(std::string_view input, T& output) noexcept
{
    std::string_view trimmed = internal::trim(input);
    if (trimmed.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Input is empty"));
        return false;
    }

    auto [ptr, ec] = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), output);
    
    if (ec == std::errc()) {
        return true;
    }

    if (ec == std::errc::invalid_argument) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid format:"); LOG_STRING(std::string(input)));
    } else if (ec == std::errc::result_out_of_range) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Value out of range:"); LOG_STRING(std::string(input)));
    } else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid number:"); LOG_STRING(std::string(input)));
    }

    return false;
}

#endif /* (1 == UNUMERIC_USE_SSTREAM_FOR_FLOAT_CONVERSION) */

// Overload for std::string for backward compatibility
template<concepts::FloatingPoint T>
[[nodiscard]] inline bool string_to_floating(const std::string& input, T& output) noexcept
{
    return string_to_floating<T>(std::string_view(input), output);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Alternative API: Converts string to value and returns std::optional
 * 
 * @tparam T Numeric type to convert to
 * @param input The input string to convert
 * @return std::optional<T> containing the value if successful, std::nullopt otherwise
 */
/*--------------------------------------------------------------------------------------------------------*/
template<typename T>
    requires concepts::SignedInteger<T> || concepts::UnsignedInteger<T> || concepts::FloatingPoint<T>
[[nodiscard]] std::optional<T> parse(std::string_view input) noexcept
{
    T result{};
    bool success = false;

    if constexpr (concepts::SignedInteger<T>) {
        success = string_to_signed<T>(input, result);
    } else if constexpr (concepts::UnsignedInteger<T>) {
        success = string_to_unsigned<T>(input, result);
    } else if constexpr (concepts::FloatingPoint<T>) {
        success = string_to_floating<T>(input, result);
    }

    return success ? std::optional<T>(result) : std::nullopt;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to an int8_t value.
 * @param s The input string.
 * @param out Reference to the output variable.
 * @return True if conversion succeeds, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] inline bool str2int8(std::string_view s, int8_t& out) noexcept
{
    return string_to_signed<int8_t>(s, out);
}

[[nodiscard]] inline bool str2int8(const std::string& s, int8_t& out) noexcept
{
    return string_to_signed<int8_t>(s, out);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to an int16_t value.
 * @param s The input string.
 * @param out Reference to the output variable.
 * @return True if conversion succeeds, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] inline bool str2int16(std::string_view s, int16_t& out) noexcept
{
    return string_to_signed<int16_t>(s, out);
}

[[nodiscard]] inline bool str2int16(const std::string& s, int16_t& out) noexcept
{
    return string_to_signed<int16_t>(s, out);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to an int32_t value.
 * @param s The input string.
 * @param out Reference to the output variable.
 * @return True if conversion succeeds, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] inline bool str2int32(std::string_view s, int32_t& out) noexcept
{
    return string_to_signed<int32_t>(s, out);
}

[[nodiscard]] inline bool str2int32(const std::string& s, int32_t& out) noexcept
{
    return string_to_signed<int32_t>(s, out);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to an int64_t value.
 * @param s The input string.
 * @param out Reference to the output variable.
 * @return True if conversion succeeds, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] inline bool str2int64(std::string_view s, int64_t& out) noexcept
{
    return string_to_signed<int64_t>(s, out);
}

[[nodiscard]] inline bool str2int64(const std::string& s, int64_t& out) noexcept
{
    return string_to_signed<int64_t>(s, out);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to an ssize_t value.
 * @param s The input string.
 * @param out Reference to the output variable.
 * @return True if conversion succeeds, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] inline bool str2ssize_t(std::string_view s, ssize_t& out) noexcept
{
    return string_to_signed<ssize_t>(s, out);
}

[[nodiscard]] inline bool str2ssize_t(const std::string& s, ssize_t& out) noexcept
{
    return string_to_signed<ssize_t>(s, out);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to a uint8_t value.
 * @param s The input string.
 * @param out Reference to the output variable.
 * @return True if conversion succeeds, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] inline bool str2uint8(std::string_view s, uint8_t& out) noexcept
{
    return string_to_unsigned<uint8_t>(s, out);
}

[[nodiscard]] inline bool str2uint8(const std::string& s, uint8_t& out) noexcept
{
    return string_to_unsigned<uint8_t>(s, out);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to a uint16_t value.
 * @param s The input string.
 * @param out Reference to the output variable.
 * @return True if conversion succeeds, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] inline bool str2uint16(std::string_view s, uint16_t& out) noexcept
{
    return string_to_unsigned<uint16_t>(s, out);
}

[[nodiscard]] inline bool str2uint16(const std::string& s, uint16_t& out) noexcept
{
    return string_to_unsigned<uint16_t>(s, out);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to a uint32_t value.
 * @param s The input string.
 * @param out Reference to the output variable.
 * @return True if conversion succeeds, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] inline bool str2uint32(std::string_view s, uint32_t& out) noexcept
{
    return string_to_unsigned<uint32_t>(s, out);
}

[[nodiscard]] inline bool str2uint32(const std::string& s, uint32_t& out) noexcept
{
    return string_to_unsigned<uint32_t>(s, out);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to a uint64_t value.
 * @param s The input string.
 * @param out Reference to the output variable.
 * @return True if conversion succeeds, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] inline bool str2uint64(std::string_view s, uint64_t& out) noexcept
{
    return string_to_unsigned<uint64_t>(s, out);
}

[[nodiscard]] inline bool str2uint64(const std::string& s, uint64_t& out) noexcept
{
    return string_to_unsigned<uint64_t>(s, out);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to an int value.
 * @param s The input string.
 * @param out Reference to the output variable.
 * @return True if conversion succeeds, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] inline bool str2int(std::string_view s, int& out) noexcept
{
    return string_to_signed<int>(s, out);
}

[[nodiscard]] inline bool str2int(const std::string& s, int& out) noexcept
{
    return string_to_signed<int>(s, out);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to a uint value.
 * @param s The input string.
 * @param out Reference to the output variable.
 * @return True if conversion succeeds, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] inline bool str2uint(std::string_view s, unsigned int& out) noexcept
{
    return string_to_unsigned<unsigned int>(s, out);
}

[[nodiscard]] inline bool str2uint(const std::string& s, unsigned int& out) noexcept
{
    return string_to_unsigned<unsigned int>(s, out);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to a size_t value.
 * @param s The input string.
 * @param out Reference to the output variable.
 * @return True if conversion succeeds, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] inline bool str2sizet(std::string_view s, size_t& out) noexcept
{
    return string_to_unsigned<size_t>(s, out);
}

[[nodiscard]] inline bool str2sizet(const std::string& s, size_t& out) noexcept
{
    return string_to_unsigned<size_t>(s, out);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to a float value.
 * @param s The input string.
 * @param out Reference to the output variable.
 * @return True if conversion succeeds, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] inline bool str2float(std::string_view s, float& out) noexcept
{
    return string_to_floating<float>(s, out);
}

[[nodiscard]] inline bool str2float(const std::string& s, float& out) noexcept
{
    return string_to_floating<float>(s, out);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to a double value.
 * @param s The input string.
 * @param out Reference to the output variable.
 * @return True if conversion succeeds, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] inline bool str2double(std::string_view s, double& out) noexcept
{
    return string_to_floating<double>(s, out);
}

[[nodiscard]] inline bool str2double(const std::string& s, double& out) noexcept
{
    return string_to_floating<double>(s, out);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to a long double value.
 * @param s The input string.
 * @param out Reference to the output variable.
 * @return True if conversion succeeds, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] inline bool str2long_double(std::string_view s, long double& out) noexcept
{
    return string_to_floating<long double>(s, out);
}

[[nodiscard]] inline bool str2long_double(const std::string& s, long double& out) noexcept
{
    return string_to_floating<long double>(s, out);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Convert ASCII character to hexadecimal value.
 * 
 * @param c ASCII character ('0'-'9', 'a'-'f', 'A'-'F')
 * @return uint16_t value (0-15) or 0xFFFF if invalid
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] constexpr uint16_t ascii2val(char c) noexcept
{
    if (c >= '0' && c <= '9')
        return static_cast<uint16_t>(c - '0');
    else if (c >= 'a' && c <= 'f')
        return static_cast<uint16_t>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F')
        return static_cast<uint16_t>(c - 'A' + 10);

    return 0xFFFF;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Compare two spans for equality up to a given count.
 * 
 * More flexible than the original vector-only version, works with any contiguous containers.
 * 
 * @tparam T Element type
 * @param a First span to compare
 * @param b Second span to compare
 * @param count Number of elements to compare
 * @return true if equal, false otherwise
 */
/*--------------------------------------------------------------------------------------------------------*/
template<typename T>
[[nodiscard]] bool compareSpans(std::span<const T> a, std::span<const T> b, size_t count) noexcept
{
    if (a.size() < count || b.size() < count) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Span size less than compare size"));
        return false;
    }
    
    return std::equal(a.begin(), a.begin() + count, b.begin());
}

// Backward compatibility: vector version
template<typename T>
[[nodiscard]] inline bool compareVectors(const std::vector<T>& a, const std::vector<T>& b, size_t count) noexcept
{
    return compareSpans(std::span<const T>(a), std::span<const T>(b), count);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Print binary data as hexadecimal string.
 * 
 * @param caption Description text to print before the hex data
 * @param dataSpan Span of bytes to print as hex
 */
/*--------------------------------------------------------------------------------------------------------*/
inline void printHexData(std::string_view caption, std::span<const uint8_t> dataSpan)
{
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    
    for (uint8_t byte : dataSpan) {
        oss << std::setw(2) << static_cast<unsigned int>(byte) << ' ';
    }
    
    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(std::string(caption)); LOG_STRING(oss.str()));
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Convert C-style array to span.
 * 
 * @tparam T Element type
 * @tparam N Array size
 * @param buffer Reference to array
 * @return std::span<T> wrapping the array
 */
/*--------------------------------------------------------------------------------------------------------*/
template <typename T, size_t N>
[[nodiscard]] constexpr std::span<T> byte2span(T (&buffer)[N]) noexcept
{
    return std::span<T>(buffer, N);
}

template <typename T>
[[nodiscard]] constexpr std::span<T> byte2span(T& value) noexcept
{
    return std::span<T>(&value, 1);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Convert byte buffer to uint8_t span.
 * 
 * @tparam ByteType Type that must be 1 byte in size
 * @tparam N Array size
 * @param buffer Reference to byte array
 * @return std::span<uint8_t> wrapping the buffer
 */
/*--------------------------------------------------------------------------------------------------------*/
template <typename ByteType, size_t N>
[[nodiscard]] constexpr std::span<uint8_t> buf2span(ByteType (&buffer)[N]) noexcept
{
    static_assert(sizeof(ByteType) == 1, "Buffer element type must be 1 byte");
    return std::span<uint8_t>(reinterpret_cast<uint8_t*>(buffer), N);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Create a span with bounds checking from buffer pointer and length.
 * 
 * @param buffer Pointer to buffer
 * @param bufferSize Total size of the buffer
 * @param length Desired span length
 * @return std::span<uint8_t> of the requested length
 * @throws std::out_of_range if length exceeds bufferSize
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] inline std::span<uint8_t> buflen2span(uint8_t* buffer, size_t bufferSize, size_t length)
{
    if (length > bufferSize) {
        throw std::out_of_range("Requested span length exceeds buffer size");
    }
    return std::span<uint8_t>{buffer, length};
}

[[nodiscard]] inline std::span<const uint8_t> buflen2span(const uint8_t* buffer, size_t bufferSize, size_t length)
{
    if (length > bufferSize) {
        throw std::out_of_range("Requested span length exceeds buffer size");
    }
    return std::span<const uint8_t>{buffer, length};
}

// Non-throwing version
[[nodiscard]] inline std::span<uint8_t> buflen2span_safe(uint8_t* buffer, size_t bufferSize, size_t length) noexcept
{
    length = std::min(length, bufferSize);
    return std::span<uint8_t>{buffer, length};
}

[[nodiscard]] inline std::span<const uint8_t> buflen2span_safe(const uint8_t* buffer, size_t bufferSize, size_t length) noexcept
{
    length = std::min(length, bufferSize);
    return std::span<const uint8_t>{buffer, length};
}


} // namespace numeric

#endif // UNUMERIC_UTILS_H
