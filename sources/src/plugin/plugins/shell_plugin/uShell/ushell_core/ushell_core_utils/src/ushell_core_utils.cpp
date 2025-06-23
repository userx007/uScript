#include "ushell_core_utils.h"
#include "ushell_core_printout.h"

#include <stddef.h>
#include <ctype.h>
#include <stdint.h>


/*----------------------------------------------------------------------------*/
char *strtok_ex(char *str, const char *delim, char **saveptr)
{
    if (!delim || (!str && !*saveptr) || !*delim) {
        return NULL;
    }

    if (!str) {
        str = *saveptr;
    }

    // Skip leading delimiters
    while (*str) {
        const char *d = delim;
        while (*d && *str != *d) ++d;
        if (!*d) break; // Not a delimiter
        ++str;
    }

    if (!*str) {
        return NULL;
    }

    char *token = str;

    // Find end of token
    while (*str) {
        const char *d = delim;
        while (*d && *str != *d) ++d;
        if (*d) break; // Found delimiter
        ++str;
    }

    if (*str) {
        *str = '\0';
        *saveptr = str + 1;
    } else {
        *saveptr = NULL;
    }

    return token;
}


/*----------------------------------------------------------------------------*/
#if defined(BIGNUM_T)
bool asc2int(const char *s, BIGNUM_T *pNumber)
{
    BIGNUM_T numValue = 0;
    bool bRetVal = true;

    if (!s || *s == '\0') {
        return false;
    }

#if (1 == uSHELL_SUPPORTS_SIGNED_TYPES)
    bool bNegative = false;
    if (*s == '-') {
        bNegative = true;
        s++;
    }
#endif

    int base = 10;
    if (*s == '0') {
        if (tolower(*(s + 1)) == 'x') {
            base = 16;
            s += 2;
        } else if (tolower(*(s + 1)) == 'b') {
            base = 2;
            s += 2;
        } else if (tolower(*(s + 1)) == 'o') {
            base = 8;
            s += 2;
        }
    }

    while (*s) {
        char c = tolower(*s);
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
        s++;
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
bool asc2float(const char *s, numfp_t *pFloatTypeVar)
{
    bool bNegative = false, bFraction = false;
    long lValue = 0;
    numfp_t fptFraction = 1.0;

    if(!s || *s == '\0') {
        return false;
    }

    if(*s == '-') {
        bNegative = true;
        s++;
    }

    while(*s) {
        if(*s == '.') {
            if(bFraction || *(s + 1) == '\0') {
                return false;
            }
            bFraction = true;
        } else if(isdigit(*s)) {
            lValue = lValue * 10 + (*s - '0');
            if(bFraction) {
                fptFraction *= 0.1;
            }
        } else {
            return false;
        }
        s++;
    }

    *pFloatTypeVar = (bNegative ? -lValue : lValue) * (bFraction ? fptFraction : 1.0);
    return true;
}
#endif /* uSHELL_IMPLEMENTS_NUMBERS_FLOAT */


/*----------------------------------------------------------------------------*/
int dump(BIGNUM_T address, num32_t length, bool show_address)
{
#define uSHELL_DUMP_ELEM_PER_LINE (16)

#if defined (__GNUC__) && defined(__AVR__)
    char *p = (char*)((int)address);
#else
    char *p = (char*)address;
#endif

    if(!p) {
        return 0;
    }

    int nr_lines = length / uSHELL_DUMP_ELEM_PER_LINE;
    int last_line_len = length % uSHELL_DUMP_ELEM_PER_LINE;
    if(last_line_len) nr_lines++;

    for(int i = 0; i < nr_lines; ++i) {
        int index = i * uSHELL_DUMP_ELEM_PER_LINE;
        if(show_address) uSHELL_PRINTF("%p | ", (p + index));

        for(int k = 0; k < 2; ++k) {
            for(int j = 0; j < uSHELL_DUMP_ELEM_PER_LINE; ++j) {
                unsigned char crt_byte = *(p + index + j);
                if((i == nr_lines - 1) && last_line_len && j >= last_line_len)
                    uSHELL_PRINTF((k == 0) ? "   " : " ");
                else
                    uSHELL_PRINTF("%c", uSHELL_ISPRINT(crt_byte) ? crt_byte : '.');
            }
            uSHELL_PRINTF(" | ");
        }
        uSHELL_PRINTF("\n");
    }
    return length;
}


#if (1 == uSHELL_IMPLEMENTS_HEXLIFY)
/*----------------------------------------------------------------------------*/
void hexlify(const uint8_t *bytes, size_t length, char *output)
{
    const char hex_chars[] = "0123456789ABCDEF";
    for (size_t i = 0; i < length; ++i) {
        output[i * 2]     = hex_chars[(bytes[i] >> 4) & 0x0F];
        output[i * 2 + 1] = hex_chars[bytes[i] & 0x0F];
    }
    output[length * 2] = '\0'; // Null-terminate the string
}


/*----------------------------------------------------------------------------*/
bool unhexlify(const char *hexstr, uint8_t *output, size_t *out_len)
{
    size_t len = 0;

    // Must be even length
    for (const char *p = hexstr; *p; ++p) {
        len++;
    }

    if (len % 2 != 0) {
        return false;
    }

    *out_len = len / 2;

    for (size_t i = 0; i < *out_len; ++i) {
        char high = toupper(hexstr[i * 2]);
        char low  = toupper(hexstr[i * 2 + 1]);

        if (!isxdigit(high) || !isxdigit(low)) {
            return false;
        }

        uint8_t high_val = (high >= 'A') ? (high - 'A' + 10) : (high - '0');
        uint8_t low_val  = (low  >= 'A') ? (low  - 'A' + 10) : (low  - '0');

        output[i] = (high_val << 4) | low_val;
    }

    return true;
}
#endif /* (1 == uSHELL_IMPLEMENTS_HEXLIFY) */
