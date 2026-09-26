#include "ushell_core_utils.h"

#include "ushell_core_printout.h"

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*----------------------------------------------------------------------------*/
char *strtok_ex(char *pstrStr, const char *pstrDelim, char **ppstrSaveptr)
{
    if (!pstrDelim || (!pstrStr && !*ppstrSaveptr) || !*pstrDelim) {
        return nullptr;
    }

    if (!pstrStr) {
        pstrStr = *ppstrSaveptr;
    }

    // Skip leading delimiters
    while (*pstrStr) {
        const char *d = pstrDelim;
        while (*d && *pstrStr != *d) {
            ++d;
        }
        if (!*d) {
            break; // Not a delimiter
        }
        ++pstrStr;
    }

    if (!*pstrStr) {
        return nullptr;
    }

    char *ppstrToken = pstrStr;

    // Find end of ppstrToken
    while (*pstrStr) {
        const char *d = pstrDelim;
        while (*d && *pstrStr != *d) {
            ++d;
        }
        if (*d) {
            break; // Found delimiter
        }
        ++pstrStr;
    }

    if (*pstrStr) {
        *pstrStr     = '\0';
        *ppstrSaveptr = pstrStr + 1;
    } else {
        *ppstrSaveptr = nullptr;
    }

    return ppstrToken;
}

/*----------------------------------------------------------------------------*/
#if defined(BIGNUM_T)
bool asc2int(const char *pstrS, BIGNUM_T *pNumber)
{
    BIGNUM_T numValue = 0;
    bool bRetVal      = true;

    if (!pstrS || *pstrS == '\0') {
        return false;
    }

#if (1 == uSHELL_SUPPORTS_SIGNED_TYPES)
    bool bNegative = false;
    if (*pstrS == '-') {
        bNegative = true;
        pstrS++;
    }
#endif

    int base = 10;
    if (*pstrS == '0') {
        if (tolower(*(pstrS + 1)) == 'x') {
            base = 16;
            pstrS += 2;
        } else if (tolower(*(pstrS + 1)) == 'b') {
            base = 2;
            pstrS += 2;
        } else if (tolower(*(pstrS + 1)) == 'o') {
            base = 8;
            pstrS += 2;
        }
    }

    while (*pstrS) {
        char c = tolower(*pstrS);
        int digit;

        if (isdigit(c)) {
            digit = c - '0';
        } else if (isalpha(c)) {
            digit = c - 'a' + 10;
        } else {
            return false;
        }

        if (digit >= base) {
            return false;
        }

        numValue = numValue * base + digit;
        pstrS++;
    }

#if (1 == uSHELL_SUPPORTS_SIGNED_TYPES)
    *pNumber = (bNegative) ? -numValue : numValue;
#else
    *pNumber = numValue;
#endif

    return bRetVal;
}
#endif /* defined(BIGNUM_T) */

/*----------------------------------------------------------------------------*/
#ifdef uSHELL_IMPLEMENTS_NUMBERS_FLOAT
bool asc2float(const char *pstrS, numfp_t *pFloatTypeVar)
{
    bool bNegative = false, bFraction = false;
    long lValue         = 0;
    numfp_t fptFraction = 1.0;

    if (!pstrS || *pstrS == '\0') {
        return false;
    }

    if (*pstrS == '-') {
        bNegative = true;
        pstrS++;
    }

    while (*pstrS) {
        if (*pstrS == '.') {
            if (bFraction || *(pstrS + 1) == '\0') {
                return false;
            }
            bFraction = true;
        } else if (isdigit(*pstrS)) {
            lValue = lValue * 10 + (*pstrS - '0');
            if (bFraction) {
                fptFraction *= 0.1;
            }
        } else {
            return false;
        }
        pstrS++;
    }

    *pFloatTypeVar = (bNegative ? -lValue : lValue) * (bFraction ? fptFraction : 1.0);
    return true;
}
#endif /* uSHELL_IMPLEMENTS_NUMBERS_FLOAT */

/*----------------------------------------------------------------------------*/
int dump(BIGNUM_T address, num32_t length, bool bShow_address)
{
#define uSHELL_DUMP_ELEM_PER_LINE (16)

#if defined(__GNUC__) && defined(__AVR__)
    char *p = (char *)((int)address);
#else
    char *p = (char *)address;
#endif

    if (!p) {
        return 0;
    }

    int nr_lines      = length / uSHELL_DUMP_ELEM_PER_LINE;
    int last_line_len = length % uSHELL_DUMP_ELEM_PER_LINE;
    if (last_line_len) {
        nr_lines++;
    }

    for (int i = 0; i < nr_lines; ++i) {
        int index = i * uSHELL_DUMP_ELEM_PER_LINE;
        if (bShow_address) {
            uSHELL_PRINTF("%p | ", (p + index));
        }

        for (int k = 0; k < 2; ++k) {
            for (int j = 0; j < uSHELL_DUMP_ELEM_PER_LINE; ++j) {
                unsigned char crt_byte = *(p + index + j);
                if ((i == nr_lines - 1) && last_line_len && j >= last_line_len) {
                    uSHELL_PRINTF((k == 0) ? "   " : " ");
                } else {
                    uSHELL_PRINTF("%c", uSHELL_ISPRINT(crt_byte) ? crt_byte : '.');
                }
            }
            uSHELL_PRINTF(" | ");
        }
        uSHELL_PRINTF("\n");
    }
    return length;
}

#if (1 == uSHELL_IMPLEMENTS_HEXLIFY)
/*----------------------------------------------------------------------------*/
void hexlify(const uint8_t *pu8Bytes, size_t length, char *pstrOutput)
{
    const char hex_chars[] = "0123456789ABCDEF";
    for (size_t i = 0; i < length; ++i) {
        pstrOutput[i * 2]     = hex_chars[(pu8Bytes[i] >> 4) & 0x0F];
        pstrOutput[i * 2 + 1] = hex_chars[pu8Bytes[i] & 0x0F];
    }
    pstrOutput[length * 2] = '\0'; // Null-terminate the string
}

/*----------------------------------------------------------------------------*/
bool unhexlify(const char *pstrHexstr, uint8_t *pu8Output, size_t *pOut_len)
{
    size_t len = 0;

    // Must be even length
    for (const char *p = pstrHexstr; *p; ++p) {
        len++;
    }

    if (len % 2 != 0) {
        return false;
    }

    *pOut_len = len / 2;

    for (size_t i = 0; i < *pOut_len; ++i) {
        char high = toupper(pstrHexstr[i * 2]);
        char low  = toupper(pstrHexstr[i * 2 + 1]);

        if (!isxdigit(high) || !isxdigit(low)) {
            return false;
        }

        uint8_t high_val = (high >= 'A') ? (high - 'A' + 10) : (high - '0');
        uint8_t low_val  = (low >= 'A') ? (low - 'A' + 10) : (low - '0');

        pu8Output[i]        = (high_val << 4) | low_val;
    }

    return true;
}
#endif /* (1 == uSHELL_IMPLEMENTS_HEXLIFY) */

/*----------------------------------------------------------------------------*/
char *trim_whitespace_inplace(char *pstrStr)
{
    if (!pstrStr) {
        return pstrStr;
    }

    // Trim leading whitespace
    while (*pstrStr && isspace((unsigned char)*pstrStr)) {
        pstrStr++;
    }

    // Trim trailing whitespace (null terminate)
    char *end = pstrStr + strlen(pstrStr) - 1;
    while (end > pstrStr && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }

    return pstrStr;
}

/*----------------------------------------------------------------------------*/
bool strings_equal_trimmed(const char *pstrS1, const char *pstrS2)
{
    // Skip leading whitespace
    while (*pstrS1 && isspace((unsigned char)*pstrS1)) {
        pstrS1++;
    }
    while (*pstrS2 && isspace((unsigned char)*pstrS2)) {
        pstrS2++;
    }

    // Compare content
    while (*pstrS1 && *pstrS2) {
        if (*pstrS1 != *pstrS2) {
            return false;
        }
        pstrS1++;
        pstrS2++;
    }

    // Skip trailing whitespace
    while (*pstrS1 && isspace((unsigned char)*pstrS1)) {
        pstrS1++;
    }
    while (*pstrS2 && isspace((unsigned char)*pstrS2)) {
        pstrS2++;
    }

    return (*pstrS1 == '\0' && *pstrS2 == '\0');
}

/*----------------------------------------------------------------------------*/
void trim_whitespace(const char *pstrInput, char *pstrOutput, size_t output_size)
{
    if (output_size == 0) {
        return;
    }

    // Skip leading whitespace
    while (*pstrInput && isspace((unsigned char)*pstrInput)) {
        pstrInput++;
    }

    // Copy string
    size_t len = 0;
    while (*pstrInput && len < output_size - 1) {
        pstrOutput[len++] = *pstrInput++;
    }
    pstrOutput[len] = '\0';

    // Remove trailing whitespace
    while (len > 0 && isspace((unsigned char)pstrOutput[len - 1])) {
        pstrOutput[--len] = '\0';
    }
}