#pragma once

#include "ushell_core_settings.h"

#define uSHELL_ISPRINT(c)  (((c) >= 0x20) && ((c) <= 0x7e))

char *strtok_ex( char *str, const char *delim, char **saveptr );

#if defined(BIGNUM_T)
    bool asc2int( const char *s, BIGNUM_T *pNumber );
    int  dump ( BIGNUM_T address, num32_t length, bool show_address );
#endif /* defined(BIGNUM_T) */

#ifdef uSHELL_IMPLEMENTS_NUMBERS_FLOAT
    bool asc2float( const char *s, numfp_t *pFloatTypeVar );
#endif /* uSHELL_IMPLEMENTS_NUMBERS_FLOAT*/
