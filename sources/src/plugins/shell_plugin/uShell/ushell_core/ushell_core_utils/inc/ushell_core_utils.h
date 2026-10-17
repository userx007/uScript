#ifndef USHELL_CORE_UTILS_H
#define USHELL_CORE_UTILS_H
#include "ushell_core_settings.h"

#include <stddef.h>
#include <stdint.h>

#define uSHELL_ISPRINT(c) (((c) >= 0x20) && ((c) <= 0x7e))

/** Re-entrant strtok(): splits @p str at @p delim, keeping state in @p saveptr; returns nullptr when no tokens remain. */
char *strtok_ex(char *str, const char *delim, char **saveptr);

#if defined(BIGNUM_T)
/** Parses a decimal, hex or binary string (optionally signed) into @p pNumber; returns false if invalid. */
bool asc2int(const char *pstrS, BIGNUM_T *pNumber);
/** Hex-dumps @p length bytes at @p address, optionally prefixed with the address; returns 0 on success. */
int dump(BIGNUM_T address, num32_t length, bool bShow_address);
#endif /* defined(BIGNUM_T) */

#ifdef uSHELL_IMPLEMENTS_NUMBERS_FLOAT
/** Parses a decimal floating-point string into @p pFloatTypeVar; returns false if invalid. */
bool asc2float(const char *pstrS, numfp_t *pFloatTypeVar);
#endif /* uSHELL_IMPLEMENTS_NUMBERS_FLOAT*/

#if (1 == uSHELL_IMPLEMENTS_HEXLIFY)
/** Writes @p length bytes as upper-case hex text (NUL-terminated) into @p pstrOutput, which must hold 2*length+1 chars. */
void hexlify(const uint8_t *pu8Bytes, size_t length, char *pstrOutput);
/** Decodes an even-length hex string into @p pu8Output and its byte count into @p pOut_len; false on bad input. */
bool unhexlify(const char *pstrHexstr, uint8_t *pu8Output, size_t *pOut_len);
#endif /* (1 == uSHELL_IMPLEMENTS_HEXLIFY) */

/** Trims leading and trailing whitespace in place; returns the first non-blank character. */
char *trim_whitespace_inplace(char *str);
/** Compares two strings ignoring leading and trailing whitespace. */
bool strings_equal_trimmed(const char *pstrS1, const char *pstrS2);

#endif /* USHELL_CORE_UTILS_H */