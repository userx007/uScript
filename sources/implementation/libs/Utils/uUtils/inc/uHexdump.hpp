
#ifndef UHEXDUMPUTILS_H
#define UHEXDUMPUTILS_H

#include "uFlagParser.hpp"

#include <cstdint>
#include <cstdio>
#include <cctype>
#include <cstddef>
#include <string>
#include <sstream>
#include <iomanip>
#include <stdexcept>


/*--------------------------------------------------------------------------------------------------------*/
/**
 * @namespace hexutils
 * @brief Provides utility functions for hex representations.
 */
/*--------------------------------------------------------------------------------------------------------*/

namespace hexutils
{

#define uHEXDUMP_USE_COLORS   1U

#if( 1 == uHEXDUMP_USE_COLORS )
    #define uHEXDUMP_OFFSET_COLOR              "\033[91m"     // Bright Red
    #define uHEXDUMP_HEX_COLOR                 "\033[93m"     // Bright Yellow
    #define uHEXDUMP_ASCII_COLOR               "\033[94m"     // Bright Blue
    #define uHEXDUMP_RESET_COLOR               "\033[0m"      // Reset to default
#else
    #define uHEXDUMP_OFFSET_COLOR                ""
    #define uHEXDUMP_HEX_COLOR                 ""
    #define uHEXDUMP_ASCII_COLOR               ""
    #define uHEXDUMP_RESET_COLOR               ""
#endif

/**
 * @brief Concatenates two arguments and appends a reset color code for formatted output.
 */
#define FRMT(a, b) a b uHEXDUMP_RESET_COLOR



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Outputs a formatted hexadecimal dump of a block of memory.
 *
 * This function prints the contents of a memory block in a human-readable
 * hexadecimal format. It can optionally include ASCII representation,
 * byte offsets, and spacing between bytes.
 *
 * @param pData Pointer to the data buffer to be dumped.
 * @param szDataSize Size of the data buffer in bytes.
 * @param szBytesPerLine Number of bytes to display per line (default is 16).
 * @param bShowSpaces If true, inserts spaces between bytes for readability (default is true).
 * @param bShowAscii If true, appends ASCII representation of bytes to each line (default is true).
 * @param bShowOffset If true, displays the offset at the beginning of each line (default is true).
 * @param bDecimalOffset If true, displays the offset in decimal instead of hexadecimal (default is false).
 *
 * @note This variant immediately prints characters using std::printf, making it suitable for C language usage
 * and embedded systems with limited RAM, as it avoids internal buffering
 */
/*--------------------------------------------------------------------------------------------------------*/

inline void HexDump1 ( const uint8_t *pData, size_t szDataSize, size_t szBytesPerLine = 16, bool bShowSpaces = true, bool bShowAscii = true, bool bShowOffset = true, bool bDecimalOffset = true )
{
    size_t szOffset = 0;
    size_t szLines = szDataSize / szBytesPerLine;
    size_t szLastLineLen = szDataSize % szBytesPerLine;

    if (szLastLineLen != 0) ++szLines;

    for (size_t i = 0; i < szLines; ++i) {
        size_t szLineStart = i * szBytesPerLine;
        size_t szLineLen = (i == szLines - 1 && szLastLineLen != 0) ? szLastLineLen : szBytesPerLine;

        /* Offset */
        if (bShowOffset) {
            if (bDecimalOffset) {
                std::printf(FRMT(uHEXDUMP_OFFSET_COLOR, "%08zu | "), szOffset + szLineStart);
            } else {
                std::printf(FRMT(uHEXDUMP_OFFSET_COLOR, "%08zX | "), szOffset + szLineStart);
            }
        }

        /* Hex values */
        for (size_t j = 0; j < szBytesPerLine; ++j) {
            if (j < szLineLen) {
                std::printf(bShowSpaces ? FRMT(uHEXDUMP_HEX_COLOR, "%02X ") : FRMT(uHEXDUMP_HEX_COLOR, "%02X"), pData[szLineStart + j]);
            } else {
                std::printf(bShowSpaces ? "   " : "  ");
            }
        }

        /* ASCII characters */
        if (bShowAscii) {
            std::printf(" | ");
            for (size_t j = 0; j < szLineLen; ++j) {
                uint8_t ch = pData[szLineStart + j];
                std::printf(FRMT(uHEXDUMP_ASCII_COLOR, "%c"), std::isprint(ch) ? ch : '.');
            }
        }

        std::printf("\n");
    }

} /* HexDump1() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Outputs a formatted hexadecimal dump of a memory block using a flag string for customization.
 *
 * This variant of HexDump1 allows customization of the output format through a flag string.
 * Each character in the flag string can represent a specific formatting option (e.g., show ASCII, show offset, etc.).
 *
 * @param pData Pointer to the data buffer to be dumped.
 * @param szDataSize Size of the data buffer in bytes.
 * @param szBytesPerLine Number of bytes to display per line.
 * @param flagString A string containing formatting flags to control the output style.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline void HexDump1S(const uint8_t* pData, size_t szDataSize, size_t szBytesPerLine = 16, const std::string& flagString = std::string("SAOD"))
{
    bool bShowSpaces = true;
    bool bShowAscii = true;
    bool bShowOffset = true;
    bool bDecimalOffset = false;

    try {
        FlagParser flags(flagString);

        if (flagString.find_first_of("sS") != std::string::npos)
            bShowSpaces = flags.get_flag('S');
        if (flagString.find_first_of("aA") != std::string::npos)
            bShowAscii = flags.get_flag('A');
        if (flagString.find_first_of("oO") != std::string::npos)
            bShowOffset = flags.get_flag('O');
        if (flagString.find_first_of("dD") != std::string::npos)
            bDecimalOffset = flags.get_flag('D');
    } catch (const std::exception& e) {
        std::printf("Invalid flag string: %s\n", e.what());
        return;
    }

    HexDump1(pData, szDataSize, szBytesPerLine, bShowSpaces, bShowAscii, bShowOffset, bDecimalOffset);

} /* HexDump1() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Outputs a formatted hexadecimal dump of a block of memory.
 *
 * This function prints the contents of a memory block in a human-readable
 * hexadecimal format. It can optionally include ASCII representation,
 * byte offsets, and spacing between bytes.
 *
 * @param pData Pointer to the data buffer to be dumped.
 * @param szDataSize Size of the data buffer in bytes.
 * @param szBytesPerLine Number of bytes to display per line (default is 16).
 * @param bShowSpaces If true, inserts spaces between bytes for readability (default is true).
 * @param bShowAscii If true, appends ASCII representation of bytes to each line (default is true).
 * @param bShowOffset If true, displays the offset at the beginning of each line (default is true).
 * @param bDecimalOffset If true, displays the offset in decimal instead of hexadecimal (default is false).
 *
 * @note This variant accumulates data into a buffer and prints it in three sections,
 * making it compatible with C language usage as it avoids C++-specific features.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline void HexDump2(const uint8_t* pData, size_t szDataSize, size_t szBytesPerLine = 16, bool bShowSpaces = true, bool bShowAscii = true, bool bShowOffset = true, bool bDecimalOffset = true)
{
    if (szBytesPerLine > 96) {
        szBytesPerLine = 96;
    }

    size_t szOffset = 0;
    size_t szLines = szDataSize / szBytesPerLine;
    size_t szLastLineLen = szDataSize % szBytesPerLine;

    if (szLastLineLen != 0) ++szLines;

    char buffer[288];  // Shared buffer for all sections max 96

    for (size_t i = 0; i < szLines; ++i) {
        size_t szLineStart = i * szBytesPerLine;
        size_t szLineLen = (i == szLines - 1 && szLastLineLen != 0) ? szLastLineLen : szBytesPerLine;

        /*  Offset */
        if (bShowOffset) {
            if (bDecimalOffset) {
                snprintf(buffer, sizeof(buffer), "%08zu | ", szOffset + szLineStart);
            } else {
                snprintf(buffer, sizeof(buffer), "%08zX | ", szOffset + szLineStart);
            }
            std::printf(FRMT(uHEXDUMP_OFFSET_COLOR, "%s"), buffer);
        }

        /* Hex values */
        size_t pos = 0;
        for (size_t j = 0; j < szBytesPerLine; ++j) {
            if (j < szLineLen) {
                pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                                bShowSpaces ? "%02X " : "%02X", pData[szLineStart + j]);
            } else {
                pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                                bShowSpaces ? "   " : "  ");
            }
        }
        std::printf(FRMT(uHEXDUMP_HEX_COLOR, "%s"), buffer);

        /* ASCII characters */
        if (bShowAscii) {
            pos = 0;
            pos += snprintf(buffer + pos, sizeof(buffer) - pos, " | ");
            for (size_t j = 0; j < szLineLen; ++j) {
                uint8_t ch = pData[szLineStart + j];
                pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%c", isprint(ch) ? ch : '.');
            }
            std::printf(FRMT(uHEXDUMP_ASCII_COLOR, "%s"), buffer);
        }

        std::printf("\n");
    }

} /* HexDump2() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Outputs a formatted hexadecimal dump of a memory block using a flag string for customization.
 *
 * This variant of HexDump1 allows customization of the output format through a flag string.
 * Each character in the flag string can represent a specific formatting option (e.g., show ASCII, show offset, etc.).
 *
 * @param pData Pointer to the data buffer to be dumped.
 * @param szDataSize Size of the data buffer in bytes.
 * @param szBytesPerLine Number of bytes to display per line.
 * @param flagString A string containing formatting flags to control the output style.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline void HexDump2S(const uint8_t* pData, size_t szDataSize, size_t szBytesPerLine = 16, const std::string& flagString = std::string("SAOD"))
{
    bool bShowSpaces = true;
    bool bShowAscii = true;
    bool bShowOffset = true;
    bool bDecimalOffset = false;

    try {
        FlagParser flags(flagString);

        if (flagString.find_first_of("sS") != std::string::npos)
            bShowSpaces = flags.get_flag('S');
        if (flagString.find_first_of("aA") != std::string::npos)
            bShowAscii = flags.get_flag('A');
        if (flagString.find_first_of("oO") != std::string::npos)
            bShowOffset = flags.get_flag('O');
        if (flagString.find_first_of("dD") != std::string::npos)
            bDecimalOffset = flags.get_flag('D');
    } catch (const std::exception& e) {
        std::printf("Invalid flag string: %s\n", e.what());
        return;
    }

    HexDump2(pData, szDataSize, szBytesPerLine, bShowSpaces, bShowAscii, bShowOffset, bDecimalOffset);

} /* HexDump2() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Outputs a formatted hexadecimal dump of a block of memory.
 *
 * This function prints the contents of a memory block in a human-readable
 * hexadecimal format. It can optionally include ASCII representation,
 * byte offsets, and spacing between bytes.
 *
 * @param pData Pointer to the data buffer to be dumped.
 * @param szDataSize Size of the data buffer in bytes.
 * @param szBytesPerLine Number of bytes to display per line (default is 16).
 * @param bShowSpaces If true, inserts spaces between bytes for readability (default is true).
 * @param bShowAscii If true, appends ASCII representation of bytes to each line (default is true).
 * @param bShowOffset If true, displays the offset at the beginning of each line (default is true).
 * @param bDecimalOffset If true, displays the offset in decimal instead of hexadecimal (default is false).
 *
 * @note This C++ variant gathers all data before printing it in a single operation.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline void HexDump3 ( const uint8_t* pData, size_t szDataSize, size_t szBytesPerLine = 16, bool bShowSpaces = true, bool bShowAscii = true, bool bShowOffset = true, bool bDecimalOffset = true )
{
    size_t szOffset = 0;
    size_t szLines = szDataSize / szBytesPerLine;
    size_t szLastLineLen = szDataSize % szBytesPerLine;

    if (szLastLineLen != 0) ++szLines;

    for (size_t i = 0; i < szLines; ++i) {
        std::ostringstream ossline;
        size_t szLineStart = i * szBytesPerLine;
        size_t szLineLen = (i == szLines - 1 && szLastLineLen != 0) ? szLastLineLen : szBytesPerLine;

        /* Offset */

        if (bShowOffset) {
            if (bDecimalOffset) {
                ossline << std::dec << std::setw(8) << std::setfill('0') << (szOffset + szLineStart) << " | ";
            } else {
                ossline << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << (szOffset + szLineStart) << " | ";
            }
        }

        /* Hex values */
        for (size_t j = 0; j < szBytesPerLine; ++j) {
            if (j < szLineLen) {
                ossline << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(pData[szLineStart + j]);
                if (bShowSpaces) {
                    ossline << ' ';
                }
            } else {
                ossline << (bShowSpaces ? "   " : "  ");
            }
        }

        /* ASCII characters */
        if (bShowAscii) {
            ossline << " | ";
            for (size_t j = 0; j < szLineLen; ++j) {
                uint8_t ch = pData[szLineStart + j];
                ossline << (std::isprint(ch) ? static_cast<char>(ch) : '.');
            }
        }

        std::printf("%s\n", ossline.str().c_str());
    }

} /*HexDump3() */



/*--------------------------------------------------------------------------------------------------------*/
/**
 * @brief Outputs a formatted hexadecimal dump of a memory block using a flag string for customization.
 *
 * This variant of HexDump1 allows customization of the output format through a flag string.
 * Each character in the flag string can represent a specific formatting option (e.g., show ASCII, show offset, etc.).
 *
 * @param pData Pointer to the data buffer to be dumped.
 * @param szDataSize Size of the data buffer in bytes.
 * @param szBytesPerLine Number of bytes to display per line.
 * @param flagString A string containing formatting flags to control the output style.
 */
/*--------------------------------------------------------------------------------------------------------*/

inline void HexDump3S ( const uint8_t* pData, size_t szDataSize, size_t szBytesPerLine = 16, const std::string& flagString = std::string("SAOD"))
{
    bool bShowSpaces = true;
    bool bShowAscii = true;
    bool bShowOffset = true;
    bool bDecimalOffset = false;

    try {
        FlagParser flags(flagString);

        if (flagString.find_first_of("sS") != std::string::npos)
            bShowSpaces = flags.get_flag('S');
        if (flagString.find_first_of("aA") != std::string::npos)
            bShowAscii = flags.get_flag('A');
        if (flagString.find_first_of("oO") != std::string::npos)
            bShowOffset = flags.get_flag('O');
        if (flagString.find_first_of("dD") != std::string::npos)
            bDecimalOffset = flags.get_flag('D');
    } catch (const std::exception& e) {
        std::printf("Invalid flag string: %s\n", e.what());
        return;
    }

    HexDump3(pData, szDataSize, szBytesPerLine, bShowSpaces, bShowAscii, bShowOffset, bDecimalOffset);

} /* HexDump3() */

} // namespace hexutils

#endif // UHEXDUMPUTILS_H
