#include "ushell_core_utils.h"
#include "ushell_core_printout.h"

#include <cstddef>
#include <ctype.h>
#if defined(_WIN32)
#include <windows.h>
#endif

/*----------------------------------------------------------------------------*/
char *strtok_ex(char *str, const char *delim, char **saveptr) {
    if (!delim || (!str && !*saveptr)) return NULL;

    if (*delim == '\0') {
        *saveptr = NULL;
        return (str && *str) ? str : NULL;
    }

    // If str is NULL, continue tokenizing from previous position
    if (!str) str = *saveptr;

    // Skip initial delimiters manually
    while (*str) {
        bool isDelimiter = false;
        for (const char *d = delim; *d; ++d) {
            if (*str == *d) {
                isDelimiter = true;
                break;
            }
        }
        if (!isDelimiter) break; // Found start of a valid token
        ++str;
    }

    if (*str == '\0') return NULL; // No tokens left

    char *token = str;

    // Find end of the token manually
    while (*str) {
        bool isDelimiter = false;
        for (const char *d = delim; *d; ++d) {
            if (*str == *d) {
                isDelimiter = true;
                break;
            }
        }
        if (isDelimiter) break; // End of the token found
        ++str;
    }

    if (*str) {
        *str = '\0';
        *saveptr = str + 1; // Move pointer forward
    } else {
        *saveptr = NULL; // No more tokens
    }

    return token;
}


/*----------------------------------------------------------------------------*/
#if defined(BIGNUM_T)
bool asc2int(const char *s, BIGNUM_T *pNumber)
{
    BIGNUM_T numValue = 0;
    bool bRetVal = true;

    if(!s || *s == '\0') return false;

#if (1 == uSHELL_SUPPORTS_SIGNED_TYPES)
    bool bNegative = false;
    if(*s == '-') {
        bNegative = true;
        s++;
    }
#endif

    int multiplier = 10;
    if(*s == '0' && tolower(*(s + 1)) == 'x' && *(s + 2) != '\0') {
        s += 2;
        multiplier = 16;
    }

    while(*s) {
        if(isdigit(*s)) numValue = numValue * multiplier + (*s - '0');
        else if(isalpha(*s) && tolower(*s) >= 'a' && tolower(*s) <= 'f')
            numValue = numValue * multiplier + (tolower(*s) - 'a' + 10);
        else return false;
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

    if(!s || *s == '\0') return false;

    if(*s == '-') {
        bNegative = true;
        s++;
    }

    while(*s) {
        if(*s == '.') {
            if(bFraction || *(s + 1) == '\0') return false;
            bFraction = true;
        } else if(isdigit(*s)) {
            lValue = lValue * 10 + (*s - '0');
            if(bFraction) fptFraction *= 0.1;
        } else return false;
        s++;
    }

    *pFloatTypeVar = (bNegative ? -lValue : lValue) * (bFraction ? fptFraction : 1.0);
    return true;
}
#endif /* uSHELL_IMPLEMENTS_NUMBERS_FLOAT */


/*----------------------------------------------------------------------------*/
int dump(BIGNUM_T address, num32_t length, bool show_address)
{
#define uSHELL_DUMP_ELEM_PER_LINE (16U)

#if defined (__GNUC__) && defined(__AVR__)
    char *p = (char*)((int)address);
#else
    char *p = (char*)address;
#endif

    if(!p) return -1;

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

#undef uSHELL_DUMP_ELEM_PER_LINE
    return length;
}

