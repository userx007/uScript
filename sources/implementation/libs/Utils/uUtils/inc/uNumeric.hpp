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
 * @namespace numutils
 * @brief Provides utility functions for numeric conversions from strings.
 */
/*--------------------------------------------------------------------------------------------------------*/

namespace numeric
{

/*--------------------------------------------------------------------------------------------------------*/
/**
 * @namespace internal
 * @brief Contains internal helper functions for numutils.
 */
/*--------------------------------------------------------------------------------------------------------*/
namespace internal
{

/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Removes leading and trailing whitespace from a string.
 *
 * This internal utility function trims whitespace characters from both ends of the input string.
 * It is intended for internal use within the numutils namespace.
 *
 * @param str The input string to be trimmed.
 * @return A new string with leading and trailing whitespace removed.
 */
/*--------------------------------------------------------------------------------------------------------*/
inline std::string trim(const std::string& str)
{
    auto begin = std::find_if_not(str.begin(), str.end(), ::isspace);
    auto end = std::find_if_not(str.rbegin(), str.rend(), ::isspace).base();

    return (begin < end) ? std::string(begin, end) : std::string();

}/* trim() */


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
 * @param input The input string potentially containing a base prefix.
 * @return A pair consisting of the detected base and the string view with the prefix removed.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline std::pair<int, std::string_view> detect_base_and_strip_prefix(const std::string& input)
{
    std::string_view view = input;
    int base = 10;
    if (view.size() > 2 && view[0] == '0') {
        if (view[1] == 'x' || view[1] == 'X') {
            base = 16;
            view.remove_prefix(2);
        } else if (view[1] == 'b' || view[1] == 'B') {
            base = 2;
            view.remove_prefix(2);
        } else if (std::isdigit(view[1])) {
            base = 8;
            view.remove_prefix(1);
        }
    }

    return {base, view};

} /* detect_base_and_strip_prefix() */

} /* namespace internal */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to a signed integer of type T.
 *
 * This function trims the input string, detects the numeric base from any prefix,
 * and attempts to convert the string to a signed integer using `std::from_chars`.
 * It logs errors if the input is invalid or out of range.
 *
 * @tparam T A signed integer type (e.g., int8_t, int32_t).
 * @param input The input string to convert.
 * @param output Reference to the variable where the result will be stored.
 * @return True if the conversion was successful, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/

template<typename T>
bool string_to_signed(const std::string& input, T& output)
{
    bool bRetVal = false;

    // Single-pass loop for structured early exits
    do {
        std::string trimmed = internal::trim(input);
        if (trimmed.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Input is empty"));
            break;
        }

        auto [base, view] = internal::detect_base_and_strip_prefix(trimmed);
        auto [ptr, ec] = std::from_chars(view.data(), view.data() + view.size(), output, base);
        if (ec == std::errc()) {
            bRetVal = true;
            break;
        }

        if (ec == std::errc::invalid_argument) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid format:"); LOG_STRING(input));
            break;
        }

        if (ec == std::errc::result_out_of_range) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Value out of range:"); LOG_STRING(input));
            break;
        }

        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid number:"); LOG_STRING(input));

    } while (false);

    return bRetVal;

} /* string_to_signed() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to an unsigned integer of type T.
 *
 * This function trims the input string, detects the numeric base from any prefix
 * (e.g., "0x" for hex, "0b" for binary, "0" for octal), and attempts to convert
 * the string to an unsigned integer using `std::from_chars`.
 * It logs detailed error messages if the input is invalid or out of range.
 *
 * @tparam T An unsigned integer type (e.g., uint8_t, uint32_t).
 * @param input The input string to convert.
 * @param output Reference to the variable where the result will be stored.
 * @return True if the conversion was successful, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/

template<typename T>
bool string_to_unsigned(const std::string& input, T& output)
{
    bool bRetVal = false;

    // Single-pass loop for structured early exits
    do {
        std::string trimmed = internal::trim(input);
        if (trimmed.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Input is empty"));
            break;
        }

        auto [base, view] = internal::detect_base_and_strip_prefix(trimmed);
        auto [ptr, ec] = std::from_chars(view.data(), view.data() + view.size(), output, base);
        if (ec == std::errc()) {
            bRetVal = true;
            break;
        }

        if (ec == std::errc::invalid_argument) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid format:"); LOG_STRING(input));
            break;
        }

        if (ec == std::errc::result_out_of_range) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Value out of range:"); LOG_STRING(input));
            break;
        }

        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid number:"); LOG_STRING(input));

    } while (false);

    return bRetVal;
} /* string_to_unsigned() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to a floating-point number of type T.
 *
 * This function trims the input string and attempts to convert it to a floating-point
 * value using `std::from_chars`. It supports types like `float`, `double`, and `long double`.
 * Logs detailed error messages if the input is invalid or out of range.
 *
 * @tparam T A floating-point type (e.g., float, double, long double).
 * @param input The input string to convert.
 * @param output Reference to the variable where the result will be stored.
 * @return True if the conversion was successful, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/

#if (1 == UNUMERIC_USE_SSTREAM_FOR_FLOAT_CONVERSION)

template<typename T>
bool string_to_floating(const std::string& input, T& output)
{
    bool bRetVal = false;

    do {
        std::string trimmed = internal::trim(input);
        if (trimmed.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Input is empty"));
            break;
        }

        std::istringstream iss(trimmed);
        iss >> output;

        if (iss.fail() || !iss.eof()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid format or extra characters:"); LOG_STRING(input));
            break;
        }

        bRetVal = true;

    } while (false);

    return bRetVal;
}

#else

template<typename T>
bool string_to_floating(const std::string& input, T& output)
{
    bool bRetVal = false;

    // Single-pass loop for structured early exits
    do {
        std::string trimmed = internal::trim(input);
        if (trimmed.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Input is empty"));
            break;
        }

        auto [ptr, ec] = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), output);
        if (ec == std::errc()) {
            bRetVal = true;
            break;
        }

        if (ec == std::errc::invalid_argument) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid format:"); LOG_STRING(input));
            break;
        }

        if (ec == std::errc::result_out_of_range) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Value out of range:"); LOG_STRING(input));
            break;
        }

        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid number:"); LOG_STRING(input));

    } while (false);

    return bRetVal;

} /* string_to_floating() */

#endif /* (1 == UNUMERIC_USE_SSTREAM_FOR_FLOAT_CONVERSION) */

/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to an int8_t value.
 * @param s The input string.
 * @param out Reference to the output variable.
 * @return True if conversion succeeds, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline bool str2int8(const std::string& s, int8_t& out)
{
    return string_to_signed<int8_t>(s, out);

} /* str2int8() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to an int16_t value.
 * @param s The input string.
 * @param out Reference to the output variable.
 * @return True if conversion succeeds, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline bool str2int16(const std::string& s, int16_t& out)
{
    return string_to_signed<int16_t>(s, out);

} /* str2int16() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to an int32_t value.
 * @param s The input string.
 * @param out Reference to the output variable.
 * @return True if conversion succeeds, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline bool str2int32(const std::string& s, int32_t& out)
{
    return string_to_signed<int32_t>(s, out);

} /* str2int32() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to an int64_t value.
 * @param s The input string.
 * @param out Reference to the output variable.
 * @return True if conversion succeeds, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline bool str2int64(const std::string& s, int64_t& out)
{
    return string_to_signed<int64_t>(s, out);

} /* str2int64() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to an ssize_t value.
 * @param s The input string.
 * @param out Reference to the output variable.
 * @return True if conversion succeeds, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline bool str2ssize_t(const std::string& s, ssize_t& out)
{
    return string_to_signed<ssize_t>(s, out);

} /* str2ssize_t() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to a uint8_t value.
 * @param s The input string.
 * @param out Reference to the output variable.
 * @return True if conversion succeeds, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline bool str2uint8(const std::string& s, uint8_t& out)
{
    return string_to_unsigned<uint8_t>(s, out);

} /* str2uint8() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to a uint16_t value.
 * @param s The input string.
 * @param out Reference to the output variable.
 * @return True if conversion succeeds, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline bool str2uint16(const std::string& s, uint16_t& out)
{
    return string_to_unsigned<uint16_t>(s, out);

} /* str2uint16() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to a uint32_t value.
 * @param s The input string.
 * @param out Reference to the output variable.
 * @return True if conversion succeeds, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline bool str2uint32(const std::string& s, uint32_t& out)
{
    return string_to_unsigned<uint32_t>(s, out);

} /* str2uint32() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to a uint64_t value.
 * @param s The input string.
 * @param out Reference to the output variable.
 * @return True if conversion succeeds, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline bool str2uint64(const std::string& s, uint64_t& out)
{
    return string_to_unsigned<uint64_t>(s, out);

} /* str2uint64() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to a int value.
 * @param s The input string.
 * @param out Reference to the output variable.
 * @return True if conversion succeeds, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline bool str2int(const std::string& s, int& out)
{
    return string_to_signed<int>(s, out);

} /* str2int() */


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to a uint value.
 * @param s The input string.
 * @param out Reference to the output variable.
 * @return True if conversion succeeds, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline bool str2uint(const std::string& s, unsigned int& out)
{
    return string_to_unsigned<unsigned int>(s, out);

} /* str2uint() */


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to a size_t value.
 * @param s The input string.
 * @param out Reference to the output variable.
 * @return True if conversion succeeds, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline bool str2sizet(const std::string& s, size_t& out)
{
    return string_to_unsigned<size_t>(s, out);

} /* str2sizet() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to a float value.
 * @param s The input string.
 * @param out Reference to the output variable.
 * @return True if conversion succeeds, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline bool str2float(const std::string& s, float& out)
{
    return string_to_floating<float>(s, out);

} /* str2float() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to a double value.
 * @param s The input string.
 * @param out Reference to the output variable.
 * @return True if conversion succeeds, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline bool str2double(const std::string& s, double& out)
{
    return string_to_floating<double>(s, out);

} /* str2double() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Converts a string to a long double value.
 * @param s The input string.
 * @param out Reference to the output variable.
 * @return True if conversion succeeds, false otherwise.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline bool str2long_double(const std::string& s, long double& out)
{
    return string_to_floating<long double>(s, out);

} /* str2long_double() */



/*--------------------------------------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------------------------------------*/

inline uint16_t ascii2val (char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    else if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    return 0xFFFF;

} /* ascii2val() */


/*--------------------------------------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------------------------------------*/
template<typename T>
bool compareVectors(const std::vector<T>& a, const std::vector<T>& b, size_t count)
{
    if (a.size() < count || b.size() < count) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Vector size less than compare size"));
        return false; // Avoid out-of-bounds
    }
    return std::equal(a.begin(), a.begin() + count, b.begin());

} /* compareVectors() */


/*--------------------------------------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------------------------------------*/

inline void printHexData(const std::string& caption, const std::span<const uint8_t> dataSpan)
{
    std::ostringstream oss;

    for (auto byte : dataSpan) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << ' ';
    }
    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(caption); LOG_STRING(oss.str()));

} /* printHexData() */


/*--------------------------------------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------------------------------------*/

template <typename T, size_t N>
std::span<T> byte2span(T (&buffer)[N]) {
    return std::span<T>(buffer, N);
}

template <typename T>
std::span<T> byte2span(T& value) {
    return std::span<T>(&value, 1);
}


#if 0
template<typename ByteType>
std::span<const uint8_t> byte2span(ByteType& byte) {
    static_assert(sizeof(ByteType) == 1, "ByteType must be 1 byte");
    return std::span<const uint8_t>(&byte, 1);
}

#if 0
template <typename ByteType>
std::span<uint8_t> byte2span(ByteType& byte)
{
    static_assert(sizeof(ByteType) == 1, "ByteType must be 1 byte");
    return std::span<uint8_t>(reinterpret_cast<uint8_t*>(&byte), 1);
}
#endif
#endif


/*--------------------------------------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------------------------------------*/

template <typename ByteType, size_t N>
std::span<uint8_t> buf2span(ByteType (&buffer)[N])
{
    static_assert(sizeof(ByteType) == 1, "Buffer element type must be 1 byte");
    return std::span<uint8_t>(reinterpret_cast<uint8_t*>(buffer), N);
}


/*--------------------------------------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------------------------------------*/

inline std::span<uint8_t> buflen2span(uint8_t* buffer, size_t bufferSize, size_t length)
{
    if (length > bufferSize) {
        throw std::out_of_range("Requested span length exceeds buffer size");
    }
    return std::span<uint8_t>{ buffer, length };
}

/*--------------------------------------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------------------------------------*/

inline std::span<const uint8_t> buflen2span(const uint8_t* buffer, size_t bufferSize, size_t length)
{
    if (length > bufferSize) {
        throw std::out_of_range("Requested span length exceeds buffer size");
    }
    return std::span<const uint8_t>{ buffer, length };
}


// uint8_t vcBuf[17] = { 0 };
// auto span1 = make_span(vcBuf, sizeof(vcBuf), 5);           // mutable span
// auto span2 = make_span(static_cast<const uint8_t*>(vcBuf), sizeof(vcBuf), 3);  // const span


} /* namespace numeric */

#endif // UNUMERIC_UTILS_H
