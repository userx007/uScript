#ifndef USHELL_CORE_UTILS_H
#define USHELL_CORE_UTILS_H
#include "ushell_core_settings.h"

#include <stddef.h>
#include <stdint.h>

#define uSHELL_ISPRINT(c) (((c) >= 0x20) && ((c) <= 0x7e))

char *strtok_ex(char *pstrStr, const char *pstrDelim, char **ppstrSaveptr);

#if defined(BIGNUM_T)
bool asc2int(const char *pstrS, BIGNUM_T *pNumber);
int dump(BIGNUM_T address, num32_t length, bool bShow_address);
#endif /* defined(BIGNUM_T) */

#ifdef uSHELL_IMPLEMENTS_NUMBERS_FLOAT
bool asc2float(const char *pstrS, numfp_t *pFloatTypeVar);
#endif /* uSHELL_IMPLEMENTS_NUMBERS_FLOAT*/

#if (1 == uSHELL_IMPLEMENTS_HEXLIFY)
void hexlify(const uint8_t *pu8Bytes, size_t length, char *pstrOutput);
bool unhexlify(const char *pstrHexstr, uint8_t *pu8Output, size_t *pOut_len);
#endif /* (1 == uSHELL_IMPLEMENTS_HEXLIFY) */

char *trim_whitespace_inplace(char *pstrStr);
bool strings_equal_trimmed(const char *pstrS1, const char *pstrS2);

#endif /* USHELL_CORE_UTILS_H */