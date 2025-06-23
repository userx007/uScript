#include "ushell_core_utils.h"
#include "ushell_user_logger.h"
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>

/*
Note:
    The definitios of num8_t, num16_t num32_t, num64_t are declared in:
    ..sources\ushell_settings\inc\ushell_core_settings.h
    and can be extended or adapted e.g. to signed variants, according to the user's needs
*/

#define SHELLFCT_RETVAL_ERR 0xFFU


///////////////////////////////////////////////////////////////////
//                  USER'S FUNCTIONS                             //
///////////////////////////////////////////////////////////////////


/*---------------------------------------------------------------*/
int vtest(void)
{
    uSHELL_LOG(LOG_VERBOSE, "--> vtest()" );

    return 0;
}

/*---------------------------------------------------------------*/
int vhexlify(void)
{
    int iRetVal = SHELLFCT_RETVAL_ERR;

    uSHELL_LOG(LOG_VERBOSE, "--> vhexlify()" );

    #define TEST_LEN 16U
    const uint8_t pu8InBuf[TEST_LEN] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    char *pstrOutBuf = (char*)malloc(TEST_LEN*2 + 1);

    if (nullptr != pstrOutBuf) {
        for (int i = 0; i < TEST_LEN; ++i) {
            uSHELL_LOG(LOG_VERBOSE, "%d : %d (0x%02X)", i, pu8InBuf[i], pu8InBuf[i]);
        }

        hexlify(pu8InBuf, TEST_LEN, pstrOutBuf);
        uSHELL_LOG(LOG_VERBOSE, "result: [%s]", pstrOutBuf);
        free(pstrOutBuf);
        iRetVal = 0;
    } else {
        uSHELL_LOG(LOG_ERROR, "malloc failed");
    }

    return iRetVal;
}

/*---------------------------------------------------------------*/
int itest(uint32_t i)
{
    uSHELL_LOG(LOG_VERBOSE, "--> itest()" );
    uSHELL_LOG(LOG_INFO, "i = %u", i );

    return 0;
}

/*---------------------------------------------------------------*/
int stest(char *s)
{
    uSHELL_LOG(LOG_VERBOSE, "--> stest()" );
    uSHELL_LOG(LOG_INFO, "s = %s", s );

    return 0;
}

/*---------------------------------------------------------------*/
int sunhexlify(char *s)
{
    int iRetVal = SHELLFCT_RETVAL_ERR;

    uSHELL_LOG(LOG_VERBOSE, "--> sunhexlify()" );

    size_t szLen = strlen(s);
    if (0 != szLen) {
        uint8_t *pu8Buf = (uint8_t*)malloc(szLen/2 + 1);

        if (nullptr != pu8Buf) {
            size_t szOutLen = 0;

            if (unhexlify(s, pu8Buf, &szOutLen)) {
                for (int i = 0; i < szOutLen; ++i) {
                    uSHELL_LOG(LOG_VERBOSE, "%d : %d (0x%02X)", i, pu8Buf[i], pu8Buf[i]);
                }
                iRetVal = 0;
            } else {
                uSHELL_LOG(LOG_ERROR, "unhexlify failed (len || content)");
            }
            free(pu8Buf);
        } else {
            uSHELL_LOG(LOG_ERROR, "malloc failed");
        }
    } else {
        uSHELL_LOG(LOG_ERROR, "empty string");
    }

    return iRetVal;
}

/*---------------------------------------------------------------*/
int iitest(uint32_t i1, uint32_t i2)
{
    uSHELL_LOG(LOG_VERBOSE, "--> iitest()" );
    uSHELL_LOG(LOG_INFO, "i1 = %d", i1 );
    uSHELL_LOG(LOG_INFO, "i2 = %d", i2 );

    return 0;
}

/*---------------------------------------------------------------*/
int istest(uint32_t i, char *s)
{
    uSHELL_LOG(LOG_VERBOSE, "--> istest()" );
    uSHELL_LOG(LOG_INFO, "i = %d", i );
    uSHELL_LOG(LOG_INFO, "s = %s", s );

    return 0;
}

/*---------------------------------------------------------------*/
int sstest(char *s1, char *s2)
{
    uSHELL_LOG(LOG_VERBOSE, "--> sstest()" );
    uSHELL_LOG(LOG_INFO, "s1 = %s", s1 );
    uSHELL_LOG(LOG_INFO, "s2 = %s", s2 );

    return 0;
}

/*---------------------------------------------------------------*/
int liotest(uint64_t l, uint32_t i, bool o)
{
    uSHELL_LOG(LOG_VERBOSE, "--> liotest()" );
    uSHELL_LOG(LOG_INFO, "l = %ld", l );
    uSHELL_LOG(LOG_INFO, "i = %d", i );
    uSHELL_LOG(LOG_INFO, "o = %d", o );

    return 0;
}


///////////////////////////////////////////////////////////////////
//               USER SHORTCUTS HANDLERS                         //
///////////////////////////////////////////////////////////////////


#if (1 == uSHELL_IMPLEMENTS_USER_SHORTCUTS)

/*----------------------------------------------------------------------------*/
void uShellUserHandleShortcut_Slash( const char *pstrArgs )
{
    uSHELL_LOG(LOG_VERBOSE, "[/] shortcut handler | args [%s] called..", pstrArgs);
    uSHELL_LOG(LOG_WARNING, "Not implemented");

} /* uShellUserHandleShortcut_Dot() */


/*----------------------------------------------------------------------------*/
void uShellUserHandleShortcut_Dot( const char *pstrArgs )
{
    uSHELL_LOG(LOG_VERBOSE, "[.] shortcut handler | args [%s] called..", pstrArgs);
    uSHELL_LOG(LOG_WARNING, "Not implemented");

} /* uShellUserHandleShortcut_Slash() */

#endif /*(1 == uSHELL_IMPLEMENTS_USER_SHORTCUTS)*/
