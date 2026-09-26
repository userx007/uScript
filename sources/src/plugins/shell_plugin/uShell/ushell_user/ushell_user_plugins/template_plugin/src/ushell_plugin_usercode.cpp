#include "ushell_core_utils.h"
#include "ushell_user_logger.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
    uSHELL_LOG(ULOG_VERBOSE, "--> vtest()");

    return 0;
}

/*---------------------------------------------------------------*/
int vhexlify(void)
{
    int iRetVal = SHELLFCT_RETVAL_ERR;

    uSHELL_LOG(ULOG_VERBOSE, "--> vhexlify()");

#define TEST_LEN 16U
    const uint8_t pu8InBuf[TEST_LEN] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    char *pstrOutBuf                 = (char *)malloc(TEST_LEN * 2 + 1);

    if (nullptr != pstrOutBuf) {
        for (unsigned int i = 0; i < TEST_LEN; ++i) {
            uSHELL_LOG(ULOG_VERBOSE, "%d : %d (0x%02X)", i, pu8InBuf[i], pu8InBuf[i]);
        }

        hexlify(pu8InBuf, TEST_LEN, pstrOutBuf);
        uSHELL_LOG(ULOG_VERBOSE, "result: [%s]", pstrOutBuf);
        free(pstrOutBuf);
        iRetVal = 0;
    } else {
        uSHELL_LOG(ULOG_ERROR, "malloc failed");
    }

    return iRetVal;
}

/*---------------------------------------------------------------*/
int itest(uint32_t u32I)
{
    uSHELL_LOG(ULOG_VERBOSE, "--> itest()");
    uSHELL_LOG(ULOG_INFO, "u32I = %u", u32I);

    return 0;
}

/*---------------------------------------------------------------*/
int stest(char *pstrS)
{
    uSHELL_LOG(ULOG_VERBOSE, "--> stest()");
    uSHELL_LOG(ULOG_INFO, "pstrS = %pstrS", pstrS);

    return 0;
}

/*---------------------------------------------------------------*/
int sunhexlify(char *pstrS)
{
    int iRetVal = SHELLFCT_RETVAL_ERR;

    uSHELL_LOG(ULOG_VERBOSE, "--> sunhexlify()");

    size_t szLen = strlen(pstrS);
    if (0 != szLen) {
        uint8_t *pu8Buf = (uint8_t *)malloc(szLen / 2 + 1);

        if (nullptr != pu8Buf) {
            size_t szOutLen = 0;

            if (unhexlify(pstrS, pu8Buf, &szOutLen)) {
                for (int i = 0; i < szOutLen; ++i) {
                    uSHELL_LOG(ULOG_VERBOSE, "%d : %d (0x%02X)", i, pu8Buf[i], pu8Buf[i]);
                }
                iRetVal = 0;
            } else {
                uSHELL_LOG(ULOG_ERROR, "unhexlify failed (len || content)");
            }
            free(pu8Buf);
        } else {
            uSHELL_LOG(ULOG_ERROR, "malloc failed");
        }
    } else {
        uSHELL_LOG(ULOG_ERROR, "empty string");
    }

    return iRetVal;
}

/*---------------------------------------------------------------*/
int iitest(uint32_t u32I1, uint32_t u32I2)
{
    uSHELL_LOG(ULOG_VERBOSE, "--> iitest()");
    uSHELL_LOG(ULOG_INFO, "u32I1 = %d", u32I1);
    uSHELL_LOG(ULOG_INFO, "u32I2 = %d", u32I2);

    return 0;
}

/*---------------------------------------------------------------*/
int istest(uint32_t u32I, char *pstrS)
{
    uSHELL_LOG(ULOG_VERBOSE, "--> istest()");
    uSHELL_LOG(ULOG_INFO, "u32I = %d", u32I);
    uSHELL_LOG(ULOG_INFO, "pstrS = %pstrS", pstrS);

    return 0;
}

/*---------------------------------------------------------------*/
int sstest(char *pstrS1, char *pstrS2)
{
    uSHELL_LOG(ULOG_VERBOSE, "--> sstest()");
    uSHELL_LOG(ULOG_INFO, "pstrS1 = %s", pstrS1);
    uSHELL_LOG(ULOG_INFO, "pstrS2 = %s", pstrS2);

    return 0;
}

/*---------------------------------------------------------------*/
int liotest(uint64_t u64L, uint32_t u32I, bool bO)
{
    uSHELL_PRINTF("--> liotest()\n");
#if (defined(__MINGW32__) || defined(_MSC_VER))
    uSHELL_PRINTF("u64L = %lld\n", u64L);
#else
    uSHELL_PRINTF("u64L = %ld\n", u64L);
#endif
    uSHELL_PRINTF("u32I = %d\n", u32I);
    uSHELL_PRINTF("bO = %d\n", bO);

    return 0;
}

///////////////////////////////////////////////////////////////////
//               USER SHORTCUTS HANDLERS                         //
///////////////////////////////////////////////////////////////////

#if (1 == uSHELL_IMPLEMENTS_USER_SHORTCUTS)

/*----------------------------------------------------------------------------*/
void uShellUserHandleShortcut_Slash(const char *pstrArgs)
{
    uSHELL_LOG(ULOG_VERBOSE, "[/] shortcut handler | args [%s] called..", pstrArgs);
    uSHELL_LOG(ULOG_WARNING, "Not implemented");

} /* uShellUserHandleShortcut_Dot() */

/*----------------------------------------------------------------------------*/
void uShellUserHandleShortcut_Dot(const char *pstrArgs)
{
    uSHELL_LOG(ULOG_VERBOSE, "[.] shortcut handler | args [%s] called..", pstrArgs);
    uSHELL_LOG(ULOG_WARNING, "Not implemented");

} /* uShellUserHandleShortcut_Slash() */

#endif /*(1 == uSHELL_IMPLEMENTS_USER_SHORTCUTS)*/
