#ifndef UHEXDUMPUTILS_H
#define UHEXDUMPUTILS_H

#include "uFlagParser.hpp"

#include <cstdint>
#include <cstdio>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <span>
#include <array>
#include <algorithm>

/*--------------------------------------------------------------------------------------------------------*/
/**
 * @namespace hexutils
 * @brief Provides utility functions for hex representations and dumps.
 */
/*--------------------------------------------------------------------------------------------------------*/

namespace hexutils
{

/**
 * @brief Configuration for hexdump output colors and formatting
 */
struct HexDumpConfig
{
    bool useColors = true;
    bool showSpaces = true;
    bool showAscii = true;
    bool showOffset = true;
    bool decimalOffset = false;
    size_t bytesPerLine = 16;

    // Color codes
    static constexpr const char* OFFSET_COLOR = "\033[91m";  // Bright Red
    static constexpr const char* HEX_COLOR = "\033[93m";     // Bright Yellow
    static constexpr const char* ASCII_COLOR = "\033[94m";   // Bright Blue
    static constexpr const char* RESET_COLOR = "\033[0m";    // Reset

    /**
     * @brief Parse flags from a string (e.g., "SAOD")
     * S = Show Spaces, A = Show ASCII, O = Show Offset, D = Decimal Offset
     */
    static HexDumpConfig fromFlags(std::string_view flagString)
    {
        HexDumpConfig config;
        
        try {
            FlagParser flags{flagString};
            
            if (flagString.find_first_of("sS") != std::string_view::npos)
                config.showSpaces = flags.get_flag('S');
            if (flagString.find_first_of("aA") != std::string_view::npos)
                config.showAscii = flags.get_flag('A');
            if (flagString.find_first_of("oO") != std::string_view::npos)
                config.showOffset = flags.get_flag('O');
            if (flagString.find_first_of("dD") != std::string_view::npos)
                config.decimalOffset = flags.get_flag('D');
            if (flagString.find_first_of("cC") != std::string_view::npos)
                config.useColors = flags.get_flag('C');
        } catch (const std::exception&) {
            // Use defaults on error
        }
        
        return config;
    }
};

/*--------------------------------------------------------------------------------------------------------*/
/**
 * @namespace internal
 * @brief Internal helper functions for hexdump
 */
/*--------------------------------------------------------------------------------------------------------*/
namespace internal
{
    // Lookup table for hex digits
    constexpr char HEX_DIGITS[] = "0123456789ABCDEF";

    /**
     * @brief Fast hex byte to two-character conversion
     */
    inline void byteToHex(uint8_t byte, char* out) noexcept
    {
        out[0] = HEX_DIGITS[(byte >> 4) & 0xF];
        out[1] = HEX_DIGITS[byte & 0xF];
    }

    /**
     * @brief Build a single hexdump line efficiently
     */
    inline std::string buildHexdumpLine(std::span<const uint8_t> data,
                                       size_t lineStart,
                                       size_t lineLen,
                                       size_t bytesPerLine,
                                       size_t offset,
                                       const HexDumpConfig& config)
    {
        std::string result;
        result.reserve(256); // Pre-allocate reasonable size

        // Offset
        if (config.showOffset) {
            char offsetBuf[32];
            if (config.decimalOffset) {
                std::snprintf(offsetBuf, sizeof(offsetBuf), "%08zu | ", offset + lineStart);
            } else {
                std::snprintf(offsetBuf, sizeof(offsetBuf), "%08zX | ", offset + lineStart);
            }
            
            if (config.useColors) {
                result += HexDumpConfig::OFFSET_COLOR;
            }
            result += offsetBuf;
            if (config.useColors) {
                result += HexDumpConfig::RESET_COLOR;
            }
        }

        // Hex values
        if (config.useColors) {
            result += HexDumpConfig::HEX_COLOR;
        }
        
        char hexBuf[4] = {0}; // "XX " or "XX"
        for (size_t j = 0; j < bytesPerLine; ++j) {
            if (j < lineLen) {
                byteToHex(data[lineStart + j], hexBuf);
                hexBuf[2] = config.showSpaces ? ' ' : '\0';
                result.append(hexBuf, config.showSpaces ? 3 : 2);
            } else {
                result.append(config.showSpaces ? "   " : "  ");
            }
        }
        
        if (config.useColors) {
            result += HexDumpConfig::RESET_COLOR;
        }

        // ASCII characters
        if (config.showAscii) {
            result += " | ";
            
            if (config.useColors) {
                result += HexDumpConfig::ASCII_COLOR;
            }
            
            for (size_t j = 0; j < lineLen; ++j) {
                uint8_t ch = data[lineStart + j];
                result.push_back(std::isprint(ch) ? static_cast<char>(ch) : '.');
            }
            
            if (config.useColors) {
                result += HexDumpConfig::RESET_COLOR;
            }
        }

        return result;
    }

} // namespace internal


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Generate hexdump as a string (most flexible, modern API)
 * @param data Input data span
 * @param config Configuration options
 * @param offset Starting offset to display
 * @return Complete hexdump as string
 */
/*--------------------------------------------------------------------------------------------------------*/
[[nodiscard]] inline std::string hexdumpToString(std::span<const uint8_t> data,
                                                  const HexDumpConfig& config = HexDumpConfig(),
                                                  size_t offset = 0)
{
    if (data.empty()) {
        return "";
    }

    size_t bytesPerLine = std::min(config.bytesPerLine, size_t(96)); // Max 96 bytes per line
    size_t lines = data.size() / bytesPerLine;
    size_t lastLineLen = data.size() % bytesPerLine;
    
    if (lastLineLen != 0) {
        ++lines;
    }

    std::string result;
    result.reserve(lines * 128); // Estimate ~128 chars per line

    for (size_t i = 0; i < lines; ++i) {
        size_t lineStart = i * bytesPerLine;
        size_t lineLen = (i == lines - 1 && lastLineLen != 0) ? lastLineLen : bytesPerLine;
        
        result += internal::buildHexdumpLine(data, lineStart, lineLen, bytesPerLine, offset, config);
        result += '\n';
    }

    return result;
}

// Overload for pointer + size (backward compatible)
[[nodiscard]] inline std::string hexdumpToString(const uint8_t* pData,
                                                  size_t size,
                                                  const HexDumpConfig& config = HexDumpConfig(),
                                                  size_t offset = 0)
{
    return hexdumpToString(std::span<const uint8_t>(pData, size), config, offset);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Print hexdump directly to stdout (optimized, single printf call)
 * @param data Input data span
 * @param config Configuration options
 * @param offset Starting offset to display
 */
/*--------------------------------------------------------------------------------------------------------*/
inline void printHexdump(std::span<const uint8_t> data,
                        const HexDumpConfig& config = HexDumpConfig(),
                        size_t offset = 0)
{
    std::string dump = hexdumpToString(data, config, offset);
    std::fputs(dump.c_str(), stdout);
}

// Overload for pointer + size
inline void printHexdump(const uint8_t* pData,
                        size_t size,
                        const HexDumpConfig& config = HexDumpConfig(),
                        size_t offset = 0)
{
    printHexdump(std::span<const uint8_t>(pData, size), config, offset);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Legacy HexDump1 - Prints directly using printf (backward compatible)
 * @note This variant immediately prints characters using std::printf
 */
/*--------------------------------------------------------------------------------------------------------*/
inline void HexDump1(const uint8_t* pData,
                     size_t szDataSize,
                     size_t szBytesPerLine = 16,
                     bool bShowSpaces = true,
                     bool bShowAscii = true,
                     bool bShowOffset = true,
                     bool bDecimalOffset = true)
{
    HexDumpConfig config;
    config.bytesPerLine = szBytesPerLine;
    config.showSpaces = bShowSpaces;
    config.showAscii = bShowAscii;
    config.showOffset = bShowOffset;
    config.decimalOffset = bDecimalOffset;
    config.useColors = true;

    printHexdump(pData, szDataSize, config);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Legacy HexDump1S - With flag string parsing (backward compatible)
 */
/*--------------------------------------------------------------------------------------------------------*/
inline void HexDump1S(const uint8_t* pData,
                      size_t szDataSize,
                      size_t szBytesPerLine = 16,
                      const std::string& flagString = "SAOD")
{
    HexDumpConfig config = HexDumpConfig::fromFlags(flagString);
    config.bytesPerLine = szBytesPerLine;
    printHexdump(pData, szDataSize, config);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Legacy HexDump2 - Uses buffer accumulation (backward compatible)
 */
/*--------------------------------------------------------------------------------------------------------*/
inline void HexDump2(const uint8_t* pData,
                     size_t szDataSize,
                     size_t szBytesPerLine = 16,
                     bool bShowSpaces = true,
                     bool bShowAscii = true,
                     bool bShowOffset = true,
                     bool bDecimalOffset = true)
{
    HexDumpConfig config;
    config.bytesPerLine = std::min(szBytesPerLine, size_t(96));
    config.showSpaces = bShowSpaces;
    config.showAscii = bShowAscii;
    config.showOffset = bShowOffset;
    config.decimalOffset = bDecimalOffset;
    config.useColors = true;

    printHexdump(pData, szDataSize, config);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Legacy HexDump2S - With flag string parsing (backward compatible)
 */
/*--------------------------------------------------------------------------------------------------------*/
inline void HexDump2S(const uint8_t* pData,
                      size_t szDataSize,
                      size_t szBytesPerLine = 16,
                      const std::string& flagString = "SAOD")
{
    HexDumpConfig config = HexDumpConfig::fromFlags(flagString);
    config.bytesPerLine = std::min(szBytesPerLine, size_t(96));
    printHexdump(pData, szDataSize, config);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Legacy HexDump3 - Full C++ version (backward compatible)
 */
/*--------------------------------------------------------------------------------------------------------*/
inline void HexDump3(const uint8_t* pData,
                     size_t szDataSize,
                     size_t szBytesPerLine = 16,
                     bool bShowSpaces = true,
                     bool bShowAscii = true,
                     bool bShowOffset = true,
                     bool bDecimalOffset = true)
{
    HexDumpConfig config;
    config.bytesPerLine = szBytesPerLine;
    config.showSpaces = bShowSpaces;
    config.showAscii = bShowAscii;
    config.showOffset = bShowOffset;
    config.decimalOffset = bDecimalOffset;
    config.useColors = false; // HexDump3 didn't use colors in original

    printHexdump(pData, szDataSize, config);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Legacy HexDump3S - With flag string parsing (backward compatible)
 */
/*--------------------------------------------------------------------------------------------------------*/
inline void HexDump3S(const uint8_t* pData,
                      size_t szDataSize,
                      size_t szBytesPerLine = 16,
                      const std::string& flagString = "SAOD")
{
    HexDumpConfig config = HexDumpConfig::fromFlags(flagString);
    config.bytesPerLine = szBytesPerLine;
    config.useColors = false; // HexDump3 didn't use colors in original
    printHexdump(pData, szDataSize, config);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Modern hexdump with span and optional config
 */
/*--------------------------------------------------------------------------------------------------------*/
template<typename T>
    requires std::is_trivially_copyable_v<T>
[[nodiscard]] inline std::string dump(std::span<const T> data,
                                      const HexDumpConfig& config = HexDumpConfig())
{
    auto byteView = std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(data.data()),
        data.size() * sizeof(T)
    );
    return hexdumpToString(byteView, config);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Modern hexdump for containers (vector, array, etc.)
 */
/*--------------------------------------------------------------------------------------------------------*/
template<typename Container>
    requires requires(Container c) {
        { c.data() } -> std::convertible_to<const void*>;
        { c.size() } -> std::convertible_to<size_t>;
    }
[[nodiscard]] inline std::string dump(const Container& container,
                                      const HexDumpConfig& config = HexDumpConfig())
{
    using T = std::remove_const_t<std::remove_reference_t<decltype(*container.data())>>;
    static_assert(std::is_trivially_copyable_v<T>, "Container element type must be trivially copyable");
    
    auto byteView = std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(container.data()),
        container.size() * sizeof(T)
    );
    return hexdumpToString(byteView, config);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Quick hexdump to stdout with default settings
 */
/*--------------------------------------------------------------------------------------------------------*/
template<typename T>
inline void quickDump(const T* data, size_t count)
{
    auto byteView = std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(data),
        count * sizeof(T)
    );
    printHexdump(byteView);
}

// Overload for containers
template<typename Container>
inline void quickDump(const Container& container)
{
    using T = std::remove_const_t<std::remove_reference_t<decltype(*container.data())>>;
    auto byteView = std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(container.data()),
        container.size() * sizeof(T)
    );
    printHexdump(byteView);
}

} // namespace hexutils

#endif // UHEXDUMPUTILS_H
