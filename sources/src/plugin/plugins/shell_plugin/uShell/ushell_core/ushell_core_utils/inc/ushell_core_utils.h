#pragma once

#include "ushell_core_settings.h"
#include <stddef.h>

#define uSHELL_ISPRINT(c)  (((c) >= 0x20) && ((c) <= 0x7e))

char *strtok_ex( char *str, const char *delim, char **saveptr );

#if defined(BIGNUM_T)
    bool asc2int( const char *s, BIGNUM_T *pNumber );
    int  dump ( BIGNUM_T address, num32_t length, bool show_address );
#endif /* defined(BIGNUM_T) */

#ifdef uSHELL_IMPLEMENTS_NUMBERS_FLOAT
    bool asc2float( const char *s, numfp_t *pFloatTypeVar );
#endif /* uSHELL_IMPLEMENTS_NUMBERS_FLOAT*/

#if (1 == uSHELL_IMPLEMENTS_HEXLIFY)
    void hexlify( const uint8_t *bytes, size_t length, char *output);
    bool unhexlify( const char *hexstr, uint8_t *output, size_t *out_len);
#endif /* (1 == uSHELL_IMPLEMENTS_HEXLIFY) */
