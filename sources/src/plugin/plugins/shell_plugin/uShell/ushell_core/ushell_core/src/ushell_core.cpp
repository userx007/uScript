/*
MIT License Copyright (c) 2025, Victor Marian Popa ( victormarianpopa@gmail.com )

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
//#pragma warning(disable: 4514)
#endif

#include "ushell_core.h"
#include "ushell_core_keys.h"
#include "ushell_core_utils.h"
#include "ushell_core_printout.h"

#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cctype>
#if (1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)
#include <memory>
#endif /*(1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)*/

/*==============================================================================
                            LOCAL DEFINES
==============================================================================*/

/* read/check tilde in escape sequence*/
#if (defined(__MINGW32__) || defined(_MSC_VER)) /* i.e MinGW or Microsoft VisualStudio for Windows console */
#define SKIP_TILDE   true
#define SKIP_BRACKET true
#else
#define SKIP_TILDE   (uSHELL_KEY_TILDE        == uSHELL_GETCH())
#define SKIP_BRACKET (uSHELL_KEY_LEFT_BRACKET == uSHELL_GETCH())
#endif

/* concatenate strings */
#define FRMT(a,b)       a b uSHELL_RESET_COLOR

/* defines */
#define uSHELL_NEWLINE      "\n\r"
#define uSHELL_INVALID_VALUE (-1)

/*==============================================================================
                    PUBLIC INTERFACES IMPLEMENTATION
==============================================================================*/

/*----------------------------------------------------------------------------*/
#if (1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)
std::shared_ptr<Microshell> Microshell::getShellSharedPtr(uShellInst_s *psShellInst, const char *pstrPromptExt)
{
    return std::shared_ptr<Microshell>(new Microshell(psShellInst, pstrPromptExt));
}/* getShellSharedPtr() */
#endif // (1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)


/*----------------------------------------------------------------------------*/
Microshell *Microshell::getShellPtr(uShellInst_s *psShellInst, const char *pstrPromptExt)
{
    static Microshell uShellInstance(psShellInst, pstrPromptExt);
    return &uShellInstance;
} /* getShell() */


/*----------------------------------------------------------------------------*/
void Microshell::Run(void)
{
    while(m_Execute()) {}
#if (1 == uSHELL_IMPLEMENTS_SAVE_HISTORY)
    if(NULL != m_pInst->pfileHistory) {
        fclose(m_pInst->pfileHistory);
        m_pInst->pfileHistory = NULL;
    }
#endif /*(1 == uSHELL_IMPLEMENTS_SAVE_HISTORY)*/
#if (1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)
    if(0 == --m_iInstanceCounter) {
#endif /*(1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)*/
#if (1 == uSHELL_IMPLEMENTS_HISTORY)
        m_HistoryDeInit();
#endif /* (1 == uSHELL_IMPLEMENTS_HISTORY) */
        uSHELL_PRINTF(FRMT(uSHELL_INFO_LIST_COLOR,"uShell exit!\n\r"));
#if (1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)
    } else {
        m_pInst = m_pInstBackup;
#if (1 == uSHELL_IMPLEMENTS_SAVE_HISTORY)
        m_HistoryReload();
#endif /*(1 == uSHELL_IMPLEMENTS_SAVE_HISTORY) */
    }
#endif /*(1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)*/
} /* Run() */

/*==============================================================================
                    PRIVATE INTERFACES IMPLEMENTATION
==============================================================================*/

/*----------------------------------------------------------------------------*/
Microshell::Microshell(uShellInst_s *psShellInst, const char *pstrPromptExt)
{
#if (1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)
    m_pInstBackup = m_pInst;
#endif /*(1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)*/
    m_pInst = psShellInst;
    m_Init(pstrPromptExt);
} /* Microshell() */


/*----------------------------------------------------------------------------*/
void Microshell::m_Init(const char *pstrPromptExt)
{
    m_CoreSetPrompt(pstrPromptExt);
#if (1 == uSHELL_IMPLEMENTS_HISTORY)
#if (1 == uSHELL_IMPLEMENTS_SAVE_HISTORY)
    m_HistoryInit(pstrPromptExt);
#else
    m_HistoryInit();
#endif /*(1 == uSHELL_IMPLEMENTS_SAVE_HISTORY)*/
#endif /* (1 == uSHELL_IMPLEMENTS_HISTORY) */
#if (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)
    m_AutocomplInit();
#endif /* (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE) */
#if (1 == uSHELL_IMPLEMENTS_SMART_PROMPT)
#if (1 == uSHELL_IMPLEMENTS_EDITMODE)
#if defined(uSHELL_EDIT_MODE_DEFAULT_ACTIVE)
    m_CoreUpdatePrompt(uSHELL_PROMPTI_EDIT, true);
#else
    m_CoreUpdatePrompt(uSHELL_PROMPTI_EDIT, false);
#endif /*defined(uSHELL_EDIT_MODE_DEFAULT_ACTIVE)*/
#endif /*(1 == uSHELL_IMPLEMENTS_EDITMODE)*/
#endif /*(1 == uSHELL_IMPLEMENTS_SMART_PROMPT)*/
    m_pInst->psShortcutsArray[0] = { '#', m_CoreHandleShortcut_Hash };
    m_CoreResetInput(true);
#if (1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)
    if(0 == m_iInstanceCounter++) {
#endif /*(1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES) */
#if (1 == uSHELL_SCRIPT_MODE)
        uSHELL_PRINTF(FRMT(uSHELL_INFO_LIST_COLOR,"uShell v%s [script mode]\n"), uSHELL_VERSION);
#else
        uSHELL_PRINTF(FRMT(uSHELL_INFO_LIST_COLOR,"uShell v%s [info: ###]\n"), uSHELL_VERSION);
#endif /* (1 == uSHELL_SCRIPT_MODE) */
#if (1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)
    }
    m_pInst->bKeepRuning = true;
#endif /*(1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)*/
    m_CorePrintPrompt();
} /* m_Init() */


/*----------------------------------------------------------------------------*/
inline bool Microshell::m_Execute(void)
{
    m_CoreProcessKeyPress(uSHELL_GETCH());
#if (1 == uSHELL_IMPLEMENTS_SHELL_EXIT)
    return m_pInst->bKeepRuning;
#else
    return true;
#endif /*(1 == uSHELL_IMPLEMENTS_SHELL_EXIT)*/
} /* m_Execute() */


/*----------------------------------------------------------------------------*/
void Microshell::m_CoreParseExecuteCommand(void)
{
    int iRetVal = 0;
    if(uSHELL_ERR_OK == (iRetVal = m_CoreParseCommand())) {
        if((iRetVal = m_pInst->pfExec(&m_sCommand)) >= 0) {
            uSHELL_PRINTF(FRMT(uSHELL_SUCCESS_COLOR, "\r=> %d (0x%X)\n"), iRetVal, iRetVal);
        } else {
            m_CorePrintError(iRetVal);    /* execution errors */
        }
    } else {
        m_CorePrintError(iRetVal);    /* parsing errors */
    }
} /* m_CoreParseExecuteCommand() */


/*----------------------------------------------------------------------------*/
int Microshell::m_CoreParseCommand(void)
{
    int  iRetVal = uSHELL_ERR_OK;
    char *pstrRest = m_pstrInput;
    char *pstrToken = strtok_ex(pstrRest, m_pstrTokenSeparator, &pstrRest);
    m_sCommand.pstrFctName = pstrToken;
    if(uSHELL_ERR_FUNCTION_NOT_FOUND != (m_sCommand.iFctIndex = m_CoreSearchFunction(pstrToken))) {
        bool bIsVoidFct = ('v' == m_pInst->psFuncDefArray[m_sCommand.iFctIndex].pstrFuncParamDef[0]);
        bool bHasParams = (NULL != pstrRest);
        int iNrParamsExpected = (int)strlen(m_pInst->psFuncDefArray[m_sCommand.iFctIndex].pstrFuncParamDef);

        if((true == bHasParams) && (false == bIsVoidFct)) {
            int iNrParamsRead = 0;
#if defined(uSHELL_IMPLEMENTS_STRINGS)
#if (1 == uSHELL_SUPPORTS_SPACED_STRINGS)
            /* check if the first param is a bordered string */
            iRetVal = m_CoreHandleBorderedStrings(&pstrToken, &pstrRest, &iNrParamsRead);
#endif /*(1 == uSHELL_SUPPORTS_SPACED_STRINGS) */
#endif /*defined(uSHELL_IMPLEMENTS_STRINGS) */
            while((uSHELL_ERR_OK == iRetVal) && (NULL != (pstrToken = strtok_ex(pstrRest, m_pstrTokenSeparator, &pstrRest)))) {
                switch(m_pInst->psFuncDefArray[m_sCommand.iFctIndex].pstrFuncParamDef[(m_sCommand.iTypIndex)++]) {
#if defined(uSHELL_IMPLEMENTS_NUMBERS_64BIT)
                    case 'l': { /* [l]ong <==> 64 bit */
                            if(m_sCommand.iNrNums64 < uSHELL_MAX_PARAMS_NUM64) {
                                BIGNUM_T numVal = 0;
                                if(false == asc2int(pstrToken, &numVal)) {
                                    iRetVal = uSHELL_ERR_INVALID_NUMBER;
                                } else {
                                    if(numVal > uSHELL_MAX_VALUE_64BIT) {
                                        iRetVal = uSHELL_ERR_VALUE_TOO_BIG;
                                    } else {
                                        m_sCommand.vl[m_sCommand.iNrNums64++] = (num64_t)numVal;
                                        ++iNrParamsRead;
                                    }
                                }
                            } else {
                                iRetVal = uSHELL_ERR_TOO_MANY_ARGS;
                            }
                            if(uSHELL_ERR_OK != iRetVal) {
                                m_sCommand.eDataType = uSHELL_DATA_TYPE_64BIT;
                            }
                        }
                        break;
#endif /*defined(uSHELL_IMPLEMENTS_NUMBERS_64BIT)*/
#if defined(uSHELL_IMPLEMENTS_NUMBERS_32BIT)
                    case 'i': { /* [i]nteger <==> 32 bit */
                            if(m_sCommand.iNrNums32 < uSHELL_MAX_PARAMS_NUM32) {
                                BIGNUM_T numVal = 0;
                                if(false == asc2int(pstrToken, &numVal)) {
                                    iRetVal = uSHELL_ERR_INVALID_NUMBER;
                                } else {
                                    if(numVal > uSHELL_MAX_VALUE_32BIT) {
                                        iRetVal = uSHELL_ERR_VALUE_TOO_BIG;
                                    } else {
                                        m_sCommand.vi[m_sCommand.iNrNums32++] = (num32_t)numVal;
                                        ++iNrParamsRead;
                                    }
                                }
                            } else {
                                iRetVal = uSHELL_ERR_TOO_MANY_ARGS;
                            }
                            if(uSHELL_ERR_OK != iRetVal) {
                                m_sCommand.eDataType = uSHELL_DATA_TYPE_32BIT;
                            }
                        }
                        break;
#endif /*defined(uSHELL_IMPLEMENTS_NUMBERS_32BIT)*/
#if defined(uSHELL_IMPLEMENTS_NUMBERS_16BIT)
                    case 'w': { /* [w]ord <==> 16 bit */
                            if(m_sCommand.iNrNums16 < uSHELL_MAX_PARAMS_NUM16) {
                                BIGNUM_T numVal = 0;
                                if(false == asc2int(pstrToken, &numVal)) {
                                    iRetVal = uSHELL_ERR_INVALID_NUMBER;
                                } else {
                                    if(numVal > uSHELL_MAX_VALUE_16BIT) {
                                        iRetVal = uSHELL_ERR_VALUE_TOO_BIG;
                                    } else {
                                        m_sCommand.vw[m_sCommand.iNrNums16++] = (num16_t)numVal;
                                        ++iNrParamsRead;
                                    }
                                }
                            } else {
                                iRetVal = uSHELL_ERR_TOO_MANY_ARGS;
                            }
                            if(uSHELL_ERR_OK != iRetVal) {
                                m_sCommand.eDataType = uSHELL_DATA_TYPE_16BIT;
                            }
                        }
                        break;
#endif /*defined(uSHELL_IMPLEMENTS_NUMBERS_16BIT)*/
#if defined(uSHELL_IMPLEMENTS_NUMBERS_8BIT)
                    case 'b': { /* [b]yte <==> 8 bit */
                            if(m_sCommand.iNrNums8 < uSHELL_MAX_PARAMS_NUM8) {
                                BIGNUM_T numVal = 0;
                                if(false == asc2int(pstrToken, &numVal)) {
                                    iRetVal = uSHELL_ERR_INVALID_NUMBER;
                                } else {
                                    if(numVal > uSHELL_MAX_VALUE_8BIT) {
                                        iRetVal = uSHELL_ERR_VALUE_TOO_BIG;
                                    } else {
                                        m_sCommand.vb[m_sCommand.iNrNums8++] = (num8_t)numVal;
                                        ++iNrParamsRead;
                                    }
                                }
                            } else {
                                iRetVal = uSHELL_ERR_TOO_MANY_ARGS;
                            }
                            if(uSHELL_ERR_OK != iRetVal) {
                                m_sCommand.eDataType = uSHELL_DATA_TYPE_8BIT;
                            }
                        }
                        break;
#endif /*defined(uSHELL_IMPLEMENTS_NUMBERS_8BIT)*/
#if defined(uSHELL_IMPLEMENTS_NUMBERS_FLOAT)
                    case 'f': {
                            float numVal = 0;
                            if(m_sCommand.iNrNumsFloat < uSHELL_MAX_PARAMS_FLOAT) {
                                if(false == asc2float(pstrToken, &numVal)) {
                                    iRetVal = uSHELL_ERR_INVALID_NUMBER;
                                } else {
                                    m_sCommand.vf[m_sCommand.iNrNumsFloat++] = numVal;
                                    ++iNrParamsRead;
                                }
                            } else {
                                iRetVal = uSHELL_ERR_TOO_MANY_ARGS;
                            }
                            if(uSHELL_ERR_OK != iRetVal) {
                                m_sCommand.eDataType = uSHELL_DATA_TYPE_FLOAT;
                            }
                        }
                        break;
#endif /*defined(uSHELL_IMPLEMENTS_NUMBERS_FLOAT)*/
#if defined(uSHELL_IMPLEMENTS_STRINGS)
                    case 's': { /* [s]tring <==> (char*) */
                            if(m_sCommand.iNrStrings < uSHELL_MAX_PARAMS_STRING) {
                                m_sCommand.vs[m_sCommand.iNrStrings++] = pstrToken;
                                ++iNrParamsRead;
                            } else {
                                iRetVal = uSHELL_ERR_TOO_MANY_ARGS;
                            }
                            if(uSHELL_ERR_OK != iRetVal) {
                                m_sCommand.eDataType = uSHELL_DATA_TYPE_STRING;
                            }
                        }
                        break;
#endif /*defined(uSHELL_IMPLEMENTS_STRINGS)*/
#if defined(uSHELL_IMPLEMENTS_BOOLEAN)
                    case 'o': { /* b[o]ol <==> bool */
                            if(m_sCommand.iNrBools < uSHELL_MAX_PARAMS_BOOLEAN) {
                                BIGNUM_T numVal = 0;
                                if(false == asc2int(pstrToken, &numVal)) {
                                    iRetVal = uSHELL_ERR_INVALID_NUMBER;
                                } else {
                                    if(numVal > uSHELL_MAX_VALUE_BOOLEAN) {
                                        iRetVal = uSHELL_ERR_VALUE_TOO_BIG;
                                    } else {
                                        m_sCommand.vo[m_sCommand.iNrBools++] = (bool)numVal;
                                        ++iNrParamsRead;
                                    }
                                }
                            } else {
                                iRetVal = uSHELL_ERR_TOO_MANY_ARGS;
                            }
                            if(uSHELL_ERR_OK != iRetVal) {
                                m_sCommand.eDataType = uSHELL_DATA_TYPE_BOOL;
                            }
                        }
                        break;
#endif /*defined(uSHELL_IMPLEMENTS_BOOLEAN)*/
                    //default: {
                    default: { /* unsuported type or more params than defined */
                            if(m_sCommand.iTypIndex != iNrParamsExpected) {
                                iRetVal = uSHELL_ERR_WRONG_NUMBER_ARGS;
                            } else {
                                iRetVal = uSHELL_ERR_PARAM_TYPE_NOT_IMPLEM;
                            }
                        }
                        break;
                } /* switch(...)*/
#if defined(uSHELL_IMPLEMENTS_STRINGS)
#if (1 == uSHELL_SUPPORTS_SPACED_STRINGS)
                if(uSHELL_ERR_OK == iRetVal) {
                    iRetVal = m_CoreHandleBorderedStrings(&pstrToken, &pstrRest, &iNrParamsRead);
                }
#endif /* (1 == uSHELL_SUPPORTS_SPACED_STRINGS) */
#endif /* defined(uSHELL_IMPLEMENTS_STRINGS) */
            } /* while(...) */
            if(uSHELL_ERR_OK != iRetVal) {
                m_sCommand.iErrorInfo = m_sCommand.iTypIndex - 1;
            } else {
                if(m_sCommand.iTypIndex != iNrParamsExpected) {
                    iRetVal = uSHELL_ERR_WRONG_NUMBER_ARGS;
                }
            }
        } else {
            if(((true == bIsVoidFct) && (true == bHasParams)) || ((false == bIsVoidFct) && (false == bHasParams))) {
                iRetVal = uSHELL_ERR_WRONG_NUMBER_ARGS;
            }
        }
    } else {
        iRetVal = uSHELL_ERR_FUNCTION_NOT_FOUND;
    }
    return iRetVal;
} /* m_CoreParseCommand() */


/*----------------------------------------------------------------------------*/
void  Microshell::m_CorePrintError(const int iError)
{
    static const char *pstrErrorUnknown = "?";
    static const char *pstrErrorCaption = ": ";
    static const char *pstrErrorString = NULL;
    bool bIsTooManyArgsError = false;
    bool bIsInvalidNumError = false;
    bool bIsNumBigValueError = false;

    switch(iError) {
        case uSHELL_ERR_FUNCTION_NOT_FOUND       : { pstrErrorString = "command not found";}                 break;
        case uSHELL_ERR_WRONG_NUMBER_ARGS        : { pstrErrorString = "wrong number of arguments";}         break;
        case uSHELL_ERR_PARAM_TYPE_NOT_IMPLEM    : { pstrErrorString = "data type not implem/enabled";}      break;
        case uSHELL_ERR_PARAMS_PATTERN_NOT_IMPLEM: { pstrErrorString = "params pattern not implem/enabled";} break;
        case uSHELL_ERR_STRING_NOT_CLOSED        : { pstrErrorString = "string not closed"; }                break;
        case uSHELL_ERR_TOO_MANY_ARGS            : { bIsTooManyArgsError = true;} break;
        case uSHELL_ERR_INVALID_NUMBER           : { bIsInvalidNumError  = true;} break;
        case uSHELL_ERR_VALUE_TOO_BIG            : { bIsNumBigValueError = true;} break;
        default                                  : { pstrErrorString = pstrErrorUnknown;} break;
    }
    if((true == bIsInvalidNumError) || (true == bIsTooManyArgsError) || (true == bIsNumBigValueError)) {
        const char *pstrErrorDescription = bIsInvalidNumError  ? "invalid " :
                                           bIsTooManyArgsError ? "too many args of type " :
                                           bIsNumBigValueError ? "value too big for " : pstrErrorUnknown;
        const char *pstrErrorDetail = (m_sCommand.eDataType < uSHELL_DATA_TYPE_LAST) ? m_vstrTypeNames[m_sCommand.eDataType] : pstrErrorUnknown;

        uSHELL_PRINTF(FRMT(uSHELL_ERROR_COLOR, "\r%s%s%s (arg:%d) | %s:%s\n"), pstrErrorCaption, pstrErrorDescription, pstrErrorDetail, (m_sCommand.iErrorInfo + 1), m_sCommand.pstrFctName, m_pInst->psFuncDefArray[m_sCommand.iFctIndex].pstrFuncParamDef);
    } else {
        uSHELL_PRINTF(FRMT(uSHELL_ERROR_COLOR, "\r%s%s | %s:%s\n"), pstrErrorCaption, pstrErrorString, m_sCommand.pstrFctName, ((uSHELL_ERR_FUNCTION_NOT_FOUND == iError) ? pstrErrorUnknown : m_pInst->psFuncDefArray[m_sCommand.iFctIndex].pstrFuncParamDef));
    }
} /* m_CorePrintError() */


#if defined(uSHELL_IMPLEMENTS_STRINGS)
#if (1 == uSHELL_SUPPORTS_SPACED_STRINGS)
/*----------------------------------------------------------------------------*/
int Microshell::m_CoreHandleBorderedStrings(char **ppstrToken, char **ppstrRest, int *pIntArgCounter)
{
    int  iRetVal = uSHELL_ERR_OK;
    bool bFound  = false;

    while((NULL != *ppstrRest) && (*m_pstrTokenSeparator == **ppstrRest)) {
        (*ppstrRest)++;    /* cleanup the leading separators */
    }
    while((NULL != *ppstrRest) && (m_cStringBorderSymbol == **ppstrRest)) {
        *ppstrToken = *ppstrRest + 1;
        while('\0' != *((*ppstrRest)++)) {
            if(m_cStringBorderSymbol == **ppstrRest) {
                bFound = true;
                break;
            }
        }
        if(true == bFound) {
            bFound = false;
            **ppstrRest = '\0';
            while(*m_pstrTokenSeparator == *(++(*ppstrRest)));   /* cleanup the trailing separators */
            if('s' == m_pInst->psFuncDefArray[m_sCommand.iFctIndex].pstrFuncParamDef[(m_sCommand.iTypIndex)++]) {
                if(m_sCommand.iNrStrings < uSHELL_MAX_PARAMS_STRING) {
                    m_sCommand.vs[m_sCommand.iNrStrings++] = *ppstrToken;
                    ++(*pIntArgCounter);
                } else {
                    iRetVal = uSHELL_ERR_TOO_MANY_ARGS;
                }
            } else {
                iRetVal = uSHELL_ERR_WRONG_NUMBER_ARGS;
            }
        } else {
            iRetVal = uSHELL_ERR_STRING_NOT_CLOSED;
        }
    }
    if(uSHELL_ERR_OK != iRetVal) {
        m_sCommand.eDataType = uSHELL_DATA_TYPE_STRING;
    }
    return iRetVal;
} /* m_CoreHandleBorderedStrings() */


/*----------------------------------------------------------------------------*/
void Microshell::m_CoreSetStringBorder(const char *pstrStringBorder)
{
    int iLen = (int)strlen(pstrStringBorder);
    if(iLen > 1) {
        uSHELL_PRINTF(FRMT(uSHELL_ERROR_COLOR, ": only one symbol expected\n\r"));
    } else {
        m_cStringBorderSymbol = (0 == iLen) ? uSHELL_KEY_QUOTATION_MARK : *pstrStringBorder;
#if (1 == uSHELL_IMPLEMENTS_SMART_PROMPT)
        m_pInst->vstrPrompt[uSHELL_PROMPTI_LAST] = m_cStringBorderSymbol;
#endif /*(1 == uSHELL_IMPLEMENTS_SMART_PROMPT)*/
    }
} /* m_CoreSetStringBorder() */
#endif /* (1 == uSHELL_SUPPORTS_SPACED_STRINGS) */
#endif /*defined(uSHELL_IMPLEMENTS_STRINGS)*/


/*----------------------------------------------------------------------------*/
void Microshell::m_CoreResetInput(const bool bFull)
{
    memset(m_pstrInput, 0, sizeof(m_pstrInput));
    m_iInputPos = 0;
#if (1 == uSHELL_IMPLEMENTS_EDITMODE)
    m_iCursorPos  = 0;
#if !defined(uSHELL_EDIT_MODE_DEFAULT_ACTIVE)
    m_bEditMode = false;
#endif /*!defined(uSHELL_EDIT_MODE_DEFAULT_ACTIVE)*/
#endif /* (1 == uSHELL_IMPLEMENTS_EDITMODE) */
    if(true == bFull) {
        memset(&m_sCommand, 0, sizeof(m_sCommand));
    }
} /* m_CoreResetInput() */


/*----------------------------------------------------------------------------*/
inline void Microshell::m_CorePutString(const char *pstrArray)
{
    while(*pstrArray) {
        uSHELL_PUTCH(*pstrArray++);
    }
} /*m_CorePutString() */


/*----------------------------------------------------------------------------*/
inline void Microshell::m_CoreRemoveTrailingSpaces(void)
{
    while(uSHELL_KEY_SPACE == m_pstrInput[--m_iInputPos]);
    m_pstrInput[++m_iInputPos] = '\0';
} /* m_CoreRemoveTrailingSpaces() */


/*----------------------------------------------------------------------------*/
void Microshell::m_CoreCmdLineDelete(void)
{
    m_CoreResetInput(false);
    uSHELL_PRINTF("\r\033[%dC\033[K", m_pInst->iPromptLength);
#if (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)
    m_AutocomplReset(uSHELL_AUTOCOMPL_RELOAD);
#endif /* (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE) */
} /* m_CoreCmdLineDelete() */


/*----------------------------------------------------------------------------*/
#if (1 == uSHELL_IMPLEMENTS_SMART_PROMPT)
inline void Microshell::m_CoreUpdatePrompt(const prompti_e ePromptIndex, const bool bOnOff)
{
    m_pInst->vstrPrompt[ePromptIndex] = ((true == bOnOff) ? m_pstrPromptInfo[ePromptIndex] : tolower(m_pstrPromptInfo[ePromptIndex]));
}/* m_CoreUpdatePrompt() */
#endif /*(1 == uSHELL_IMPLEMENTS_SMART_PROMPT)*/

/*==============================================================================
                              KEY HANDLE
==============================================================================*/

/*----------------------------------------------------------------------------*/
void Microshell::m_CoreProcessKeyPress(const char cKeyPressed)
{
    m_CorePutString("\033[?25l"); /* hide cursor */
#if (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)
    if(true == m_sAutocomplete.bEnabled) {
        m_sAutocomplete.cCrtKey = cKeyPressed;
    }
#endif /*(1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)*/
    switch(cKeyPressed) {
        case uSHELL_KEY_ENTER: {
                m_CoreHandleKeyEnter();
            }
            break;
        case uSHELL_KEY_BACKSPACE: {
                m_CoreHandleKeyBackspace();
            }
            break;
#if (defined(__MINGW32__) || defined(_MSC_VER))
        case uSHELL_KEY_ESCAPESEQ1:  /* fall through (needed for _MSC_VER for INS/DEL on numeric pad*/
#endif /*(defined(__MINGW32__) || defined(_MSC_VER)) */
        case uSHELL_KEY_ESCAPESEQ: {
                m_CoreHandleKeyEscapeSeq();
            }
            break;
#if ( defined(SERIAL_TERMINAL) && !defined(__AVR__) )
        case uSHELL_KEY_DELETE: {
                m_CoreHandleKeyDelete();
            }
            break;
#if (1 == uSHELL_IMPLEMENTS_EDITMODE)
#if !defined(uSHELL_EDIT_MODE_DEFAULT_ACTIVE)
        case uSHELL_KEY_INSERT: {
                m_CoreHandleKeyInsert();
            }
            break;
#else
        case uSHELL_KEY_INSERT:
            break; /*ignore */
#endif /*!defined(uSHELL_EDIT_MODE_DEFAULT_ACTIVE)*/
#endif /* (1 == uSHELL_IMPLEMENTS_EDITMODE) */
#endif /* ( defined(SERIAL_TERMINAL) && !defined(__AVR__)) ) */
#if (1 == uSHELL_IMPLEMENTS_EDITMODE)
#if !defined(uSHELL_EDIT_MODE_DEFAULT_ACTIVE)
        case uSHELL_KEY_TAB: {
                m_CoreHandleKeyInsert();
            }
            break;
#else
        case uSHELL_KEY_TAB:
            break; /*ignore */
#endif /*!defined(uSHELL_EDIT_MODE_DEFAULT_ACTIVE)*/
#endif /* defined(uSHELL_IMPLEMENTS_EDITMODE) */
#if (1 == uSHELL_IMPLEMENTS_EDITMODE)
        case uSHELL_KEY_CTRL_U: {
                m_EditDeleteBackwardToHome();
            }
            break;
        case uSHELL_KEY_CTRL_K: {
                m_EditDeleteForwardToEnd();
            }
            break;
#endif /* (1 == uSHELL_IMPLEMENTS_EDITMODE) */
        default: {
                m_CoreHandleKeyDefault(cKeyPressed);
            }
            break;
    }
#if (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)
    if(true == m_sAutocomplete.bEnabled) {
        m_sAutocomplete.cPrevKey = cKeyPressed;
    }
#endif /*(1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)*/
    m_CorePutString("\033[?25h"); /* show cursor */

} /* m_CoreProcessKeyPress() */


/*----------------------------------------------------------------------------*/
void Microshell::m_CoreExecuteEnterKey(void)
{
    if(false == m_CoreHandleShortcuts()) {
#if (1 == uSHELL_IMPLEMENTS_HISTORY)
        m_HistoryWrite();
#endif /* (1 == uSHELL_IMPLEMENTS_HISTORY) */
        m_CoreParseExecuteCommand();
    }
} /*m_CoreExecuteEnterKey()*/


/*----------------------------------------------------------------------------*/
void Microshell::m_CoreHandleKeyEnter(void)
{
    m_CoreRemoveTrailingSpaces();
    if(m_iInputPos >= 0) {
        m_CorePutString(uSHELL_NEWLINE);
        if(m_iInputPos > 0) {
            m_CoreExecuteEnterKey();
            m_CoreResetInput(true);
#if (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)
            m_AutocomplReset(uSHELL_AUTOCOMPL_RELOAD);
        } else {
            m_AutocomplReInit();
#endif /* (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE) */
        }
    }
#if (1 == uSHELL_IMPLEMENTS_DISABLE_ECHO)
    if(true == m_bEchoOn) {
        m_CorePrintPrompt();
    }
#else
    m_CorePrintPrompt();
#endif /* (1 == uSHELL_IMPLEMENTS_DISABLE_ECHO) */
} /* m_CoreHandleKeyEnter() */


/*----------------------------------------------------------------------------*/
void Microshell::m_CoreHandleKeyDefault(const char cKeyPressed)
{
    if(true == uSHELL_ISPRINT(cKeyPressed)) {
#if (1 == uSHELL_IMPLEMENTS_EDITMODE)
        if((true == m_bEditMode) && (m_iCursorPos < m_iInputPos)) {
            m_EditInsertUnderCursor(cKeyPressed);
        } else {
#endif /* (1 == uSHELL_IMPLEMENTS_EDITMODE) */
            if(m_iInputPos < (int)(sizeof(m_pstrInput) - 1)) {
                *(m_pstrInput + m_iInputPos++) = cKeyPressed;
                *(m_pstrInput + m_iInputPos) = '\0';
#if (1 == uSHELL_IMPLEMENTS_EDITMODE)
                if(true == m_bEditMode) {
                    m_iCursorPos = m_iInputPos;
                }
#endif /* (1 == uSHELL_IMPLEMENTS_EDITMODE) */
#if (1 == uSHELL_IMPLEMENTS_DISABLE_ECHO)
                if(true == m_bEchoOn) {
                    uSHELL_PUTCH(cKeyPressed);
                }
#else
                uSHELL_PUTCH(cKeyPressed);
#endif /* (1 == uSHELL_IMPLEMENTS_DISABLE_ECHO) */
            } else {
                /* print ] and block the movement of the cursor and insertion of data in the input buffer */
                m_CorePutString(FRMT(uSHELL_ERROR_COLOR, "]\033[0m\033[D"));
            }
#if (1 == uSHELL_IMPLEMENTS_EDITMODE)
        }
#endif /* (1 == uSHELL_IMPLEMENTS_EDITMODE) */
#if (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)
        m_AutocomplGetCommon();
#endif /*(1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)*/
    }
}/* m_CoreHandleKeyDefault() */


/*----------------------------------------------------------------------------*/
void Microshell::m_CoreHandleKeyEscapeSeq(void)
{
    if(SKIP_BRACKET) {   /* skip the [ */
        switch(uSHELL_GETCH()) {  /* get the ecape sequence */
#if (1 == uSHELL_IMPLEMENTS_EDITMODE) || (1 == uSHELL_IMPLEMENTS_HISTORY)
            case uSHELL_KEY_ESCAPESEQ_ARROW_UP    : {
                    m_CoreHandleKeyArrowUpDown(uSHELL_DIR_FORWARD);
                }
                break;
            case uSHELL_KEY_ESCAPESEQ_ARROW_DOWN  : {
                    m_CoreHandleKeyArrowUpDown(uSHELL_DIR_BACKWARD);
                }
                break;
#endif /*(1 == uSHELL_IMPLEMENTS_EDITMODE) || (1 == uSHELL_IMPLEMENTS_HISTORY) */
#if (1 == uSHELL_IMPLEMENTS_EDITMODE) || (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)
            case uSHELL_KEY_ESCAPESEQ_ARROW_LEFT  : {
                    m_CoreHandleKeyArrowLeftRight(uSHELL_DIR_BACKWARD);
                }
                break;
            case uSHELL_KEY_ESCAPESEQ_ARROW_RIGHT : {
                    m_CoreHandleKeyArrowLeftRight(uSHELL_DIR_FORWARD);
                }
                break;
#endif /*(1 == uSHELL_IMPLEMENTS_EDITMODE) || defined(uSHELL_IMPLEMENTS_AUTOCOMPLETE) */
            case uSHELL_KEY_ESCAPESEQ1_DELETE     : {
                    if(SKIP_TILDE) {
                        m_CoreHandleKeyDelete();
                    }
                }
                break;
#if (1 == uSHELL_IMPLEMENTS_EDITMODE)
#if !(defined(__MINGW32__) || defined(_MSC_VER))
            case uSHELL_KEY_ESCAPESEQ_HOME        : {
                    m_EditMoveCursor(uSHELL_DIR_HOME);
                }
                break;
            case uSHELL_KEY_ESCAPESEQ_END         : {
                    m_EditMoveCursor(uSHELL_DIR_END);
                }
                break;
#endif /*!(defined(__MINGW32__) || defined(_MSC_VER))*/
            case uSHELL_KEY_ESCAPESEQ1_HOME       : {
                    if(SKIP_TILDE) {
                        m_EditMoveCursor(uSHELL_DIR_HOME);
                    }
                }
                break;
            case uSHELL_KEY_ESCAPESEQ1_END        : {
                    if(SKIP_TILDE) {
                        m_EditMoveCursor(uSHELL_DIR_END);
                    }
                }
                break;
#if !defined(uSHELL_EDIT_MODE_DEFAULT_ACTIVE)
            case uSHELL_KEY_ESCAPESEQ1_INSERT     : {
                    if(SKIP_TILDE) {
                        m_CoreHandleKeyInsert();
                    }
                }
                break;
#else
            case uSHELL_KEY_ESCAPESEQ1_INSERT     : {
                    if(SKIP_TILDE) {/*ignore*/}
                } break;
#endif /*!defined(uSHELL_EDIT_MODE_DEFAULT_ACTIVE)*/
#if !(defined(__MINGW32__) || defined(_MSC_VER))
            case uSHELL_KEY_TILDE                 : {
                    m_CoreHandleKeyDelete();
                }
                break; /*1B5B7E for INS DEL, etc is DEL */
#endif /*!(defined(__MINGW32__) || defined(_MSC_VER))*/
#else /* not (1 == uSHELL_IMPLEMENTS_EDITMODE), disable keys */
            case uSHELL_KEY_ESCAPESEQ_HOME        :
                break; /* disabled */
            case uSHELL_KEY_ESCAPESEQ_END         :
                break; /* disabled */
            case uSHELL_KEY_ESCAPESEQ1_HOME       : {
                    if(SKIP_TILDE) {}
                } break; /* disabled */
            case uSHELL_KEY_ESCAPESEQ1_END        : {
                    if(SKIP_TILDE) {}
                } break; /* disabled */
            case uSHELL_KEY_ESCAPESEQ1_INSERT     : {
                    if(SKIP_TILDE) {}
                } break; /* disabled */
#if !(defined(__MINGW32__) || defined(_MSC_VER))
            case uSHELL_KEY_TILDE                 :
                break; /* disabled */
#endif /*!(defined(__MINGW32__) || defined(_MSC_VER))*/
#endif /* (1 == uSHELL_IMPLEMENTS_EDITMODE) */
            case uSHELL_KEY_ESCAPESEQ1_PAGEUP     : {
                    if(SKIP_TILDE) {}
                } break; /* disabled */
            case uSHELL_KEY_ESCAPESEQ1_PAGEDOWN   : {
                    if(SKIP_TILDE) {}
                } break; /* disabled */
            default:
                break;
        } /* switch(uSHELL_GETCH()) */
    } /* SKIP_BRACKET */
} /* m_CoreHandleKeyEscapeSeq() */


/*----------------------------------------------------------------------------*/
#if (1 == uSHELL_IMPLEMENTS_EDITMODE) || (1 == uSHELL_IMPLEMENTS_HISTORY)
void Microshell::m_CoreHandleKeyArrowUpDown(const dir_e eDir)
{
#if (1 == uSHELL_IMPLEMENTS_EDITMODE)
    if(true == m_bEditMode) {
        switch(eDir) {
            case uSHELL_DIR_FORWARD : {
                    m_EditMoveCursor(uSHELL_DIR_HOME);
                }
                break; /* arrow up-  > home */
            case uSHELL_DIR_BACKWARD: {
                    m_EditMoveCursor(uSHELL_DIR_END);
                }
                break; /* arrow down-> end  */
            default:
                break;
        }
    } else
#endif /* (1 == uSHELL_IMPLEMENTS_EDITMODE) */
    {
#if (1 == uSHELL_IMPLEMENTS_HISTORY)
        m_HistoryRead(eDir);
#endif /* (1 == uSHELL_IMPLEMENTS_HISTORY) */
#if (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)
        m_AutocomplGetCommon();
#endif /* (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE) */
    }
} /* m_CoreHandleKeyArrowUpDown() */
#endif /* (1 == uSHELL_IMPLEMENTS_EDITMODE) || (1 == uSHELL_IMPLEMENTS_HISTORY) */


/*----------------------------------------------------------------------------*/
#if (1 == uSHELL_IMPLEMENTS_EDITMODE) || (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)
void Microshell::m_CoreHandleKeyArrowLeftRight(const dir_e eDir)
{
#if (1 == uSHELL_IMPLEMENTS_EDITMODE)
    if(false == m_EditMoveCursor(eDir)) {
#if (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)
        m_AutocomplRead(eDir);
        if(true == m_bEditMode) {
            m_iCursorPos = m_iInputPos;
        }
#endif /* (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE) */
    }
#else /* not (1 == uSHELL_IMPLEMENTS_EDITMODE) */
#if (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)
    m_AutocomplRead(eDir);
#endif /* (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE) */
#endif /* (1 == uSHELL_IMPLEMENTS_EDITMODE) */
} /* m_CoreHandleKeyArrowLeftRight() */
#endif /* (1 == uSHELL_IMPLEMENTS_EDITMODE) || (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE) */


#if (1 == uSHELL_IMPLEMENTS_EDITMODE)
#if !defined(uSHELL_EDIT_MODE_DEFAULT_ACTIVE)
/*----------------------------------------------------------------------------*/
void Microshell::m_CoreHandleKeyInsert(void)
{
    m_bEditMode = !m_bEditMode;
    if(m_iInputPos > 0) {
#if (1 == uSHELL_IMPLEMENTS_SMART_PROMPT)
        uSHELL_PRINTF(FRMT(uSHELL_PROMPT_COLOR, "\r%s\033[%dC"), (m_bEditMode ? m_pstrPromptInfoEditMode : m_pInst->vstrPrompt), m_iInputPos + (m_bEditMode ? (m_pInst->iPromptLength - ((int)(sizeof(m_pstrPromptInfo))) + 1) : 0));
#else /* (0 == uSHELL_IMPLEMENTS_SMART_PROMPT) */
        uSHELL_PRINTF(FRMT(uSHELL_PROMPT_COLOR, "\r%c\033[%dC"), (m_bEditMode ? 'E' : m_pInst->vstrPrompt[0]), (m_iInputPos + (m_pInst->iPromptLength - 1)));
#endif /* (1 == uSHELL_IMPLEMENTS_SMART_PROMPT) */
        if(true == m_bEditMode) {
            m_iCursorPos = m_iInputPos;
        }
    }
}/* m_CoreHandleKeyInsert() */
#endif /* !defined(uSHELL_EDIT_MODE_DEFAULT_ACTIVE) */
#endif /* (1 == uSHELL_IMPLEMENTS_EDITMODE) */


/*----------------------------------------------------------------------------*/
void Microshell::m_CoreHandleKeyDelete(void)
{
#if (1 == uSHELL_IMPLEMENTS_EDITMODE)
    if(true == m_bEditMode) {
        m_EditDeleteUnderCursor();
    } else {
#endif /* (1 == uSHELL_IMPLEMENTS_EDITMODE) */
        m_CoreCmdLineDelete();
#if (1 == uSHELL_IMPLEMENTS_EDITMODE)
    }
#endif /* (1 == uSHELL_IMPLEMENTS_EDITMODE) */
} /* m_CoreHandleKeyDelete() */


/*----------------------------------------------------------------------------*/
void Microshell::m_CoreHandleKeyBackspace(void)
{
#if (1 == uSHELL_IMPLEMENTS_EDITMODE)
    if(true == m_bEditMode) {
        m_EditDeleteBackward();
    } else {
#endif /* (1 == uSHELL_IMPLEMENTS_EDITMODE) */
        if(m_iInputPos > 0) {
            m_pstrInput[--m_iInputPos] = '\0';
            m_CorePutString("\033[D \033[D");
        }
#if (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)
        m_AutocomplReInit();
#endif /* (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE) */
#if (1 == uSHELL_IMPLEMENTS_EDITMODE)
    }
#endif /* (1 == uSHELL_IMPLEMENTS_EDITMODE) */
} /* m_CoreHandleKeyBackspace() */


/*----------------------------------------------------------------------------*/
inline bool Microshell::m_CoreIsShortcutSymbol(const char cKey)
{
    return (((cKey > 0x20) && (cKey < 0x30)) || ((cKey > 0x39) && (cKey < 0x41)) || ((cKey > 0x5A) && (cKey < 0x61)) || ((cKey > 0x7A) && (cKey < 0x7F)));
} /* m_CoreIsShortcutSymbol() */


/*----------------------------------------------------------------------------*/
bool Microshell::m_CoreHandleShortcuts(void)
{
    bool bRetVal = false;
    char cKey = *m_pstrInput;
    for(int i = 0; i < m_pInst->iNrShortcuts; ++i) {
        if(cKey == m_pInst->psShortcutsArray[i].cSymbol) {
            if(nullptr != m_pInst->psShortcutsArray[i].pfShortcut) {
                char *pstrArgs = m_pstrInput;
                while(uSHELL_KEY_SPACE == *(++pstrArgs));
#if (1 == uSHELL_IMPLEMENTS_HISTORY)
                if(i > 0) {
                    m_HistoryWrite();
                }
#endif /*(1 == uSHELL_IMPLEMENTS_HISTORY) */
                m_pInst->psShortcutsArray[i].pfShortcut(pstrArgs);
            } else {
                m_CorePrintMessage(4, 2);   /* callback not implemented */
            }
            bRetVal = true;
            break;
        }
    }
    if(false == bRetVal) {
        if(true == m_CoreIsShortcutSymbol(cKey)) {
            m_CorePrintMessage(5, 11); /* shortcut not registered*/
            bRetVal = true;
        }
    }
    return bRetVal;
} /* m_CoreHandleShortcuts() */


/*----------------------------------------------------------------------------*/
void Microshell::m_CoreHandleShortcut_Hash(const char *pstrArgs)
{
    int iError = 1;
    const char cKey = *pstrArgs;

    if('\0' != cKey) {
#if ((0 == uSHELL_IMPLEMENTS_COMMAND_HELP) || (1 == uSHELL_IMPLEMENTS_SHELL_EXIT) || (1 == uSHELL_IMPLEMENTS_KEY_DECODER) || (1 == uSHELL_IMPLEMENTS_DISABLE_ECHO) || (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE) || (1 == uSHELL_IMPLEMENTS_HISTORY))
        bool bNoParams = ('\0' == *(pstrArgs + 1));
#endif /*((0 == uSHELL_IMPLEMENTS_COMMAND_HELP) || (1 == uSHELL_IMPLEMENTS_SHELL_EXIT) || (1 == uSHELL_IMPLEMENTS_KEY_DECODER) || (1 == uSHELL_IMPLEMENTS_DISABLE_ECHO) || (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE) || (1 == uSHELL_IMPLEMENTS_HISTORY)) */
        switch(cKey) {
#if (1 == uSHELL_IMPLEMENTS_COMMAND_HELP) /* full info */
            case '#': {
                    if(bNoParams) {
                        m_CoreShowCmdsList(); /* ## prints the command list only */
                    } else if('#' == *(pstrArgs + 1)) {  /* ### prints the full list */
                        if('\0' == *(pstrArgs + 2)) {
                            m_CoreShowCmdsList();
                            m_CoreShowTypes();
                            m_CoreShowShortcuts();
                        } else {
                            m_CorePrintMessage(6, 2);    /* ### ... sub-shortcut not implemented */
                        }
                    } else {
                        m_CoreShowInfo(pstrArgs + 1); /* evaluates other options of the ## sub-shortcut */
                    }
                    iError = 0;
                }
                break; /* function's info*/
#else /* minimal info */
            case '#': {
                    if(bNoParams) {
                        m_CoreShowCmdsList();
                        iError = 0;
                    }
                }
                break; /* list commands */
#endif /*(1 == uSHELL_IMPLEMENTS_COMMAND_HELP) */
#if (1 == uSHELL_IMPLEMENTS_SHELL_EXIT)
            case 'q': {
                    if(bNoParams) {
                        m_CoreExit();
                        iError = 0;
                    }
                }
                break; /* exit shell */
#endif /*(1 == uSHELL_IMPLEMENTS_SHELL_EXIT) */
#if (1 == uSHELL_IMPLEMENTS_KEY_DECODER)
            case 'k': {
                    if(bNoParams) {
                        keydecoder();
                        iError = 0;
                    }
                }
                break; /* key decoder */
#endif /*(1 == uSHELL_IMPLEMENTS_KEY_DECODER)*/
#if (1 == uSHELL_IMPLEMENTS_DISABLE_ECHO)
            case 'E': {
                    if(bNoParams) {
                        m_bEchoOn = true;
                        m_CorePrintMessage(2, 1);
                        iError = 0;
                    }
                }
                break; /* echo on*/
            case 'e': {
                    if(bNoParams) {
                        m_bEchoOn = false;
                        m_CorePrintMessage(2, 0);
                        iError = 0;
                    }
                }
                break; /* echo off */
#endif /* (1 == uSHELL_IMPLEMENTS_DISABLE_ECHO) */
#if (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)
            case 'A': {
                    if(bNoParams) {
                        m_AutocomplEnable(true);
                        iError = 0;
                    }
                }
                break; /* autocomplete on */
            case 'a': {
                    if(bNoParams) {
                        m_AutocomplEnable(false);
                        iError = 0;
                    }
                }
                break; /* autocomplete off */
#endif /*(1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)*/
#if defined(uSHELL_IMPLEMENTS_STRINGS)
#if (1 == uSHELL_SUPPORTS_SPACED_STRINGS)
            case 's': {
                    m_CoreSetStringBorder(pstrArgs + 1);
                    iError = 0;
                }
                break; /* set string bordering character */
#endif /*defined(uSHELL_IMPLEMENTS_STRINGS)*/
#endif /*(1 == uSHELL_SUPPORTS_SPACED_STRINGS)*/
#if (1 == uSHELL_IMPLEMENTS_HISTORY)
            case 'H': {
                    if(bNoParams) {
                        m_HistoryEnable(true);
                        iError = 0;
                    }
                }
                break; /* history on */
            case 'h': {
                    if(bNoParams) {
                        m_HistoryEnable(false);
                        iError = 0;
                    }
                }
                break; /* history off */
            case 'l': {
                    if(bNoParams) {
                        m_HistoryList();
                        iError = 0;
                    }
                }
                break; /* view history */
#if (1 == uSHELL_IMPLEMENTS_SAVE_HISTORY)
            case 'L': {
                    if(bNoParams) {
                        m_HistoryReload();
                        iError = 0;
                    }
                }
                break; /* reload history */
#endif /*(1 == uSHELL_IMPLEMENTS_SAVE_HISTORY)*/
            case 'r': {
                    if(bNoParams) {
                        m_HistoryReset();
                        iError = 0;
                    }
                }
                break; /* reset history */
            default : {
                    m_HistoryExecuteEntry(pstrArgs);
                    iError = 0;
                }
                break; /* try execute history at index */
#else
            default : {
                    iError = 2;
                }
                break;
#endif /*(1 == uSHELL_IMPLEMENTS_HISTORY)*/
        } /*switch(...)*/
    } else {
        iError = 3;    /*if(..)*/
    }
    switch(iError) {
        case 1: {
                m_CorePrintMessage(8, 8); /* args unsupported */
            }
            break;
        case 2: {
                m_CorePrintMessage(6, 8); /* sub-shortcut unsupported */
            }
            break;
        case 3: {
                m_CorePrintMessage(6, 9); /* sub-shortcut missing */
            }
            break;
        default:
            break;
    }
} /* m_CoreHandleShortcut_Hash() */


/*----------------------------------------------------------------------------*/
int Microshell::m_CoreSearchFunction(const char *pstrFctName)
{
    for(int i = 0; i < m_pInst->iNrFunctions; ++i) {
        if(0 == strcmp(pstrFctName, m_pInst->psFuncDefArray[i].pstrFctName)) {
            return i;
        }
    }
    return uSHELL_ERR_FUNCTION_NOT_FOUND;
} /* m_CoreSearchFunction() */


/*----------------------------------------------------------------------------*/
inline void Microshell::m_CoreSetPrompt(const char *pstrPromptExt)
{
#if (1 == uSHELL_IMPLEMENTS_SMART_PROMPT)
#if (defined(uSHELL_IMPLEMENTS_STRINGS) && (1 == uSHELL_SUPPORTS_SPACED_STRINGS))
    uSHELL_SNPRINTF(m_pInst->vstrPrompt, sizeof(m_pInst->vstrPrompt), "%s%c:%s> ", m_pstrPrompt, m_cStringBorderSymbol, pstrPromptExt);
#else
    uSHELL_SNPRINTF(m_pInst->vstrPrompt, sizeof(m_pInst->vstrPrompt), "%s:%s> ", m_pstrPrompt, pstrPromptExt);
#endif
#else
    uSHELL_SNPRINTF(m_pInst->vstrPrompt, sizeof(m_pInst->vstrPrompt), "e:%s> ", pstrPromptExt);
#endif /* (1 == uSHELL_IMPLEMENTS_SMART_PROMPT) */
    m_pInst->iPromptLength = (int)strlen(m_pInst->vstrPrompt);
}

/*==============================================================================
                CORE: EXIT FEATURE IMPLEMENTATION
==============================================================================*/


/*----------------------------------------------------------------------------*/
#if (1 == uSHELL_IMPLEMENTS_CONFIRM_REQUEST)
bool Microshell::m_CoreConfirmRequest(void)
{
    bool bConfirmed = false;
    m_CorePutString("Are you sure? (y/n): ");
    do {
        char cRead = uSHELL_GETCH();
        if('y' == cRead) {
            uSHELL_PUTCH(cRead);
            bConfirmed = true;
            break;
        } else if('n' == cRead) {
            uSHELL_PUTCH(cRead);
            break;
        }
    } while(1);
    m_CorePutString(uSHELL_NEWLINE);
    return bConfirmed;
}/* m_CoreConfirmRequest() */
#endif /*(1 == uSHELL_IMPLEMENTS_CONFIRM_REQUEST)*/


/*----------------------------------------------------------------------------*/
#if (1 == uSHELL_IMPLEMENTS_SHELL_EXIT)
inline void Microshell::m_CoreExit(void)
{
#if (1 == uSHELL_IMPLEMENTS_CONFIRM_REQUEST)
    if(true == m_CoreConfirmRequest()) {
#endif /* (1 == uSHELL_IMPLEMENTS_CONFIRM_REQUEST)*/
        m_pInst->bKeepRuning = false;
#if (1 == uSHELL_IMPLEMENTS_CONFIRM_REQUEST)
    }
#endif /*(1 == uSHELL_IMPLEMENTS_CONFIRM_REQUEST)*/
} /* m_CoreExit() */
#endif /*(1 == uSHELL_IMPLEMENTS_SHELL_EXIT)*/


/*==============================================================================
                CORE: COMMANDS LIST / DETAILS
==============================================================================*/

#if (1 == uSHELL_IMPLEMENTS_COMMAND_HELP)
/*----------------------------------------------------------------------------*/
inline void  Microshell::m_CorePutChars(const char *pstrArray, int iNrChars, const bool bNewLine)
{
    while(0 < iNrChars--) {
        uSHELL_PUTCH(*pstrArray++);
    }
    if(true == bNewLine) {
        m_CorePutString(uSHELL_NEWLINE);
    }
} /* m_CorePutChars() */


/*----------------------------------------------------------------------------*/
inline void Microshell::m_CoreShowTypes(void)
{
    uSHELL_PRINTF(FRMT(uSHELL_INFO_HEADER_COLOR, "%s\n\r\t"), "DATATYPES");

    for(int i = 0; i < uSHELL_DATA_TYPE_LAST; ++i) {
        uSHELL_PRINTF(FRMT(uSHELL_INFO_BODY_COLOR, " %c-%s |"), m_vstrTypeMarks[i], m_vstrTypeNames[i]);
    }
    m_CorePutString(uSHELL_NEWLINE);
} /* m_CoreShowTypes() */


/*----------------------------------------------------------------------------*/
void Microshell::m_CoreShowShortcuts(void)
{

    uSHELL_PRINTF(FRMT(uSHELL_INFO_HEADER_COLOR, "%s\n\r"), "SHORTCUTS CORE");
    uSHELL_PRINTF(FRMT(uSHELL_INFO_BODY_COLOR, "%s"), m_pstrCoreShortcutCaption);

#if (1 == uSHELL_IMPLEMENTS_USER_SHORTCUTS)
    uSHELL_PRINTF(FRMT(uSHELL_INFO_HEADER_COLOR, "%s\n\r"), "SHORTCUTS USER");
    for(int i = 0; i < (m_pInst->iNrShortcuts - 1); ++i) {
        uSHELL_PRINTF(FRMT(uSHELL_INFO_BODY_COLOR, "%s"), m_pInst->ppstrShortcutsInfoArray[i]);
    }
#endif /*(1 == uSHELL_IMPLEMENTS_USER_SHORTCUTS)*/
} /* m_CoreShowShortcuts() */


/*----------------------------------------------------------------------------*/
/* disable warnings */
#if defined (__GNUC__) && defined(__AVR__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif /*defined (__GNUC__) && defined(__AVR__)*/

void Microshell::m_CoreShowCmdInfo(const int iFctIndex, const bool bParamInfo)
{
    if(false == bParamInfo) {
        uSHELL_PRINTF(FRMT(uSHELL_INFO_LIST_COLOR, "%3d %15s : %-15s | "), iFctIndex, m_pInst->psFuncDefArray[iFctIndex].pstrFctName, m_pInst->psFuncDefArray[iFctIndex].pstrFuncParamDef);
    } else {
        uSHELL_PRINTF(FRMT(uSHELL_INFO_LIST_COLOR, "%s "), m_pInst->psFuncDefArray[iFctIndex].pstrFctName);
    }
    const char *pstrParams = strchr(m_pInst->ppstrInfoArray[iFctIndex], '|');
    if(NULL != pstrParams) {
        m_CorePutString("| ");
        m_CorePutChars(m_pInst->ppstrInfoArray[iFctIndex], (int)(pstrParams - m_pInst->ppstrInfoArray[iFctIndex]), true);
    } else {
        uSHELL_PRINTF("| %s\n", m_pInst->ppstrInfoArray[iFctIndex]);
    }
    if(true == bParamInfo) {
        uSHELL_PRINTF("Params: [ %s ]\n%s\n", m_pInst->psFuncDefArray[iFctIndex].pstrFuncParamDef, ((NULL == pstrParams) ? "" : (pstrParams + 1)));
    }
} /* m_CoreShowCmdInfo() */

/* end of disable warnings */
#if defined (__GNUC__) && defined(__AVR__)
#pragma GCC diagnostic pop
#endif /*defined (__GNUC__) && defined(__AVR__)*/


/*----------------------------------------------------------------------------*/
void Microshell::m_CoreShowInfo(const char *pstrArgs)
{
    bool bFound = false;
    BIGNUM_T numVal = 0;

    while(' ' == *pstrArgs) {
        ++pstrArgs;
    };
    if(true == m_CoreIsShortcutSymbol(*pstrArgs)){
        m_CorePrintMessage(6, 2);    /* sub-shortcut unimplemented */
        bFound = true;
    } else if((true == asc2int(pstrArgs, &numVal)) && ((int)numVal < m_pInst->iNrFunctions)) {
        m_CoreShowCmdInfo((int)numVal, true);
        bFound = true;
    } else {
        int iIndex = m_CoreSearchFunction(pstrArgs);
        if(uSHELL_ERR_FUNCTION_NOT_FOUND != iIndex) {
            m_CoreShowCmdInfo(iIndex, true);
            bFound = true;
        } else {
            for(iIndex = 0; iIndex < m_pInst->iNrFunctions; ++iIndex) {
                if(NULL != strstr(m_pInst->psFuncDefArray[iIndex].pstrFctName, pstrArgs)) {
                    m_CoreShowCmdInfo(iIndex, false);
                    bFound = true;
                }
            }
        }
    }
    if(false == bFound) {
        m_CorePrintMessage(9, 11);    /* command not registered */
    }
} /* m_CoreShowInfo() */
#endif /* (1 == uSHELL_IMPLEMENTS_COMMAND_HELP) */

/*----------------------------------------------------------------------------*/
void Microshell::m_CoreShowCmdsList(void)
{
    uSHELL_PRINTF(FRMT(uSHELL_INFO_HEADER_COLOR, "%s\n"), "COMMANDS");
#if (1 == uSHELL_IMPLEMENTS_COMMAND_HELP)
    for(int i = 0; i < m_pInst->iNrFunctions; ++i) {
        uSHELL_PRINTF(FRMT(uSHELL_INFO_LIST_COLOR, "%3d %15s : %-15s | "), i, m_pInst->psFuncDefArray[i].pstrFctName, m_pInst->psFuncDefArray[i].pstrFuncParamDef);
        const char *pstrParams = strchr(m_pInst->ppstrInfoArray[i], '|');
        if(NULL != pstrParams) {
            m_CorePutChars(m_pInst->ppstrInfoArray[i], (int)(pstrParams - m_pInst->ppstrInfoArray[i]), true);
        } else {
            uSHELL_PRINTF("%s\n",m_pInst->ppstrInfoArray[i]);
        }
    }
#else // no function description
    for(int i = 0; i < m_pInst->iNrFunctions; ++i) {
        uSHELL_PRINTF(FRMT(uSHELL_INFO_LIST_COLOR, "%3d %15s : %-15s\n"), i, m_pInst->psFuncDefArray[i].pstrFctName, m_pInst->psFuncDefArray[i].pstrFuncParamDef);
    }
#endif /*(1 == uSHELL_IMPLEMENTS_COMMAND_HELP)*/

} /* m_CoreShowCmdsList() */


/*----------------------------------------------------------------------------*/
void Microshell::m_CorePrintMessage(const int iFeatIdx, const int iStatIdx)
{
    /*       index:                         0      1               2                 3          4           5           6               7                8              9           10         11              */
    static const char *pstrFeatArray[] = { "",    "autocomplete", "echo",            "history", "callback", "shortcut", "sub-shortcut", "malloc",        "args",        "command"                              };
    static const char *pstrStatArray[] = { "off", "on",           "not implemented", "noentry", "failed",   "empty",    "reset",        "uninitialized", "unsupported", "missing",  "nofile", "not registered" };
    uSHELL_PRINTF(FRMT(uSHELL_WARNING_COLOR, ": %s %s\n"), pstrFeatArray[iFeatIdx], pstrStatArray[iStatIdx]);
}/* m_CorePrintMessage() */



/*----------------------------------------------------------------------------*/
inline void Microshell::m_CorePrintPrompt(void)
{
    uSHELL_PRINTF(FRMT(uSHELL_PROMPT_COLOR, "%s"), m_pInst->vstrPrompt);
}/*m_CorePrintPrompt() */


/*==============================================================================
                   HISTORY: CIRCULAR BUFFER IMPLEMENTATION
==============================================================================*/


#if (1 == uSHELL_IMPLEMENTS_HISTORY)
/*----------------------------------------------------------------------------*/
bool Microshell::m_CircBufInit(void)
{
    bool bRetVal = true;
    int iMemDataSize = uSHELL_HISTORY_DEPTH * sizeof(void *);
    int iMemSizeSize = uSHELL_HISTORY_DEPTH * sizeof(size_t);

    m_sCircBuf.ppData    = (void**) malloc(iMemDataSize);
    m_sCircBuf.pDataSize = (size_t*)malloc(iMemSizeSize);

    if((NULL == m_sCircBuf.ppData) || (NULL == m_sCircBuf.pDataSize)) {
        m_CorePrintMessage(7, 4); /* malloc failed */
        free(m_sCircBuf.ppData);
        free(m_sCircBuf.pDataSize);
        m_sCircBuf.ppData = NULL;
        m_sCircBuf.pDataSize = NULL;
        bRetVal = false;
    } else {
        memset(m_sCircBuf.ppData,    0, iMemDataSize);
        memset(m_sCircBuf.pDataSize, 0, iMemSizeSize);
        m_CircBufFlagsReset();
    }
    return bRetVal;
} /* m_CircBufInit() */


/*----------------------------------------------------------------------------*/
void Microshell::m_CircBufFreeMem(const bool bFull)
{
    if(m_sCircBuf.ppData != NULL) {
        for(unsigned int i = 0; i < uSHELL_HISTORY_DEPTH; ++i) {
            if(m_sCircBuf.ppData[i] != NULL) {
                free(m_sCircBuf.ppData[i]);
                m_sCircBuf.ppData[i] = NULL;
            }
        }
        if(bFull) {
            free(m_sCircBuf.ppData);
            m_sCircBuf.ppData = NULL;
        }
    }

    if(bFull && (m_sCircBuf.pDataSize != NULL)) {
        free(m_sCircBuf.pDataSize);
        m_sCircBuf.pDataSize = NULL;
    }
}


/*----------------------------------------------------------------------------*/
void Microshell::m_CircBufDeinit(const deinit_e eTyp)
{
    switch(eTyp) {
        case uSHELL_DEINIT_RESET  : {
                m_CircBufFlagsReset();
            }
            break;
        case uSHELL_DEINIT_FULL   : {
                m_CircBufFlagsReset();
                m_CircBufFreeMem(true);
            }
            break;
        case uSHELL_DEINIT_PARTIAL: {
                m_CircBufFlagsReset();
                m_CircBufFreeMem(false);
            }
            break;
        default:
            break;
    }
}/* m_CircBufDeinit() */


/*----------------------------------------------------------------------------*/
bool Microshell::m_CircBufWrite(const void *pElem, const size_t szElemSize)
{
    bool bRetVal = true;

    if(false == m_CircBufItemExists(pElem)) {
        void *pBuf = NULL;

        if(NULL == m_sCircBuf.ppData[m_sCircBuf.iCrtPosWrite]) {
            pBuf = malloc(szElemSize);
            if(pBuf == NULL) {
                m_CorePrintMessage(7, 4); /* Memory allocation failed */
                return false;
            }
        } else if(szElemSize > m_sCircBuf.pDataSize[m_sCircBuf.iCrtPosWrite]) {
            void *pBufTemp = realloc(m_sCircBuf.ppData[m_sCircBuf.iCrtPosWrite], szElemSize);
            if(pBufTemp != NULL) {
                m_sCircBuf.ppData[m_sCircBuf.iCrtPosWrite] = pBufTemp;
                pBuf = pBufTemp;
            } else {
                m_CorePrintMessage(7, 4); /* Memory allocation failed */
                return false;
            }
        } else {
            pBuf = m_sCircBuf.ppData[m_sCircBuf.iCrtPosWrite];
        }

        if(pBuf != NULL) {
            if(szElemSize < m_sCircBuf.pDataSize[m_sCircBuf.iCrtPosWrite]) {
                memset((char*)pBuf + szElemSize, 0, (m_sCircBuf.pDataSize[m_sCircBuf.iCrtPosWrite] - szElemSize)); /* Clean unused buffer side */
            }
            memcpy(pBuf, pElem, szElemSize);
            m_sCircBuf.ppData[m_sCircBuf.iCrtPosWrite] = pBuf;
            m_sCircBuf.pDataSize[m_sCircBuf.iCrtPosWrite] = szElemSize;
            m_sCircBuf.iCrtPosRead = m_sCircBuf.iCrtPosWrite;
            m_sCircBuf.iCrtPosWrite = (m_sCircBuf.iCrtPosWrite + 1) % uSHELL_HISTORY_DEPTH;
            if(true == m_sCircBuf.bIsEmpty) {
                m_sCircBuf.bIsEmpty = false;
            }
            if(0 == m_sCircBuf.iCrtPosWrite) {
                m_sCircBuf.bIsFull  = true;
            }
        } else {
            m_CorePrintMessage(7, 4); /* Memory allocation failed */
            bRetVal = false;
        }
    } else {
        bRetVal = false;
    }

    return bRetVal;
} /* m_CircBufWrite() */


/*----------------------------------------------------------------------------*/
bool Microshell::m_CircBufRead(dir_e eDir, void *pElem, size_t *pszSize)
{
    bool bRetVal = false;

    if(false == m_sCircBuf.bIsEmpty) {   /* Ensure buffer is not empty */
        if(uSHELL_DIR_LAST != m_sCircBuf.ePrevDir) {   /* Check if a new entry was added */
            if(eDir != m_sCircBuf.ePrevDir) {   /* Direction changed */
                m_sCircBuf.iCrtPosRead += (uSHELL_DIR_BACKWARD == eDir) ? -2 : 2;
            }
        }

        /* Ensure valid read position */
        if(uSHELL_INVALID_VALUE == m_sCircBuf.iCrtPosRead) {
            if(uSHELL_DIR_BACKWARD == eDir) {
                m_sCircBuf.iCrtPosRead = m_sCircBuf.bIsFull ? uSHELL_HISTORY_DEPTH - 1 : m_sCircBuf.iCrtPosWrite - 1;
            } else {
                m_sCircBuf.iCrtPosRead = (m_sCircBuf.bIsFull) ? 0 : 1;
            }
        } else {
            m_sCircBuf.iCrtPosRead %= (m_sCircBuf.bIsFull ? uSHELL_HISTORY_DEPTH : m_sCircBuf.iCrtPosWrite);
        }

        /* Validate size before copying */
        if(m_sCircBuf.pDataSize[m_sCircBuf.iCrtPosRead] > 0) {
            *pszSize = m_sCircBuf.pDataSize[m_sCircBuf.iCrtPosRead];
            memcpy(pElem, m_sCircBuf.ppData[m_sCircBuf.iCrtPosRead], *pszSize);

            /* Move read position based on direction */
            m_sCircBuf.iCrtPosRead += (uSHELL_DIR_BACKWARD == eDir) ? -1 : 1;
            m_sCircBuf.ePrevDir = eDir;

            bRetVal = true;
        } else {
            m_CorePrintMessage(3, 3); /* Buffer entry is empty */
        }
    }

    return bRetVal;
} /* m_CircBufRead() */


/*----------------------------------------------------------------------------*/
int Microshell::m_CircBufShow(void)
{
    int iNrElems = 0;

    if(false == m_sCircBuf.bIsEmpty) {
        iNrElems = m_sCircBuf.bIsFull ? uSHELL_HISTORY_DEPTH : m_sCircBuf.iCrtPosWrite;

        for(int i = 0; i < iNrElems; ++i) {
            if(m_sCircBuf.ppData[i] != NULL) {
                uSHELL_PRINTF(FRMT(uSHELL_INFO_LIST_COLOR, "%d: %s\n"), i, (char*)m_sCircBuf.ppData[i]);
            } else {
                uSHELL_PRINTF(FRMT(uSHELL_WARNING_COLOR, "%d: [no entry]\n"), i);  /* Handle NULL case */
            }
        }
    } else {
        m_CorePrintMessage(3, 5); /* history empty */
    }

    return iNrElems;
} /* m_CircBufShow() */


/*----------------------------------------------------------------------------*/
char* Microshell::m_CircBufGetItem(const int iIndex)
{
    char *pstrItem = NULL;

    if(false == m_sCircBuf.bIsEmpty) {
        int iNrElems = m_sCircBuf.bIsFull ? uSHELL_HISTORY_DEPTH : m_sCircBuf.iCrtPosWrite;

        if(iIndex >= 0 && iIndex < iNrElems) {
            pstrItem = (char*)m_sCircBuf.ppData[iIndex];
        }
    }

    return pstrItem;
} /* m_CircBufGetItem() */


/*----------------------------------------------------------------------------*/
void Microshell::m_CircBufFlagsReset(void)
{
    m_sCircBuf.iCrtPosWrite = 0;
    m_sCircBuf.iCrtPosRead  = uSHELL_INVALID_VALUE;
    m_sCircBuf.bIsFull      = false;
    m_sCircBuf.bIsEmpty     = true;
    m_sCircBuf.ePrevDir     = uSHELL_DIR_LAST;
} /* m_CircBufFlagsReset() */


/*----------------------------------------------------------------------------*/
bool Microshell::m_CircBufItemExists(const void* pElem)
{
    bool bRetVal = false;

    if(pElem == NULL) {
        return false;    /* Prevent null pointer dereference */
    }

    int iNrElems = m_sCircBuf.bIsFull ? uSHELL_HISTORY_DEPTH : m_sCircBuf.iCrtPosWrite;

    for(int i = 0; i < iNrElems; ++i) {
        const char* pStoredItem = (const char*)m_sCircBuf.ppData[i];

        if(pStoredItem != NULL && strcmp(pStoredItem, (const char*)pElem) == 0) {
            bRetVal = true;
            break;
        }
    }

    return bRetVal;
} /* m_CircBufItemExists() */


/*==============================================================================
                          HISTORY IMPLEMENTATION
==============================================================================*/


/*----------------------------------------------------------------------------*/
#if (1 == uSHELL_IMPLEMENTS_SAVE_HISTORY)
void Microshell::m_HistoryInit(const char *pstrFileName)
#else
void Microshell::m_HistoryInit(void)
#endif /*(1 == uSHELL_IMPLEMENTS_SAVE_HISTORY)*/
{
    if(true == (m_sHistory.bInitialized = m_CircBufInit())) {
        m_sHistory.bEnabled = uSHELL_INIT_HISTORY_MODE;
#if (1 == uSHELL_IMPLEMENTS_SMART_PROMPT)
        m_CoreUpdatePrompt(uSHELL_PROMPTI_HISTORY, m_sHistory.bEnabled);
#endif /*(1 == uSHELL_IMPLEMENTS_SMART_PROMPT)*/
#if (1 == uSHELL_IMPLEMENTS_SAVE_HISTORY)
        uSHELL_SNPRINTF(m_pstrInput, sizeof(m_pstrInput), ".hist_%s", pstrFileName);
        m_pInst->pfileHistory = fopen(m_pstrInput, "a+");
        m_HistoryLoadFromFile();
#endif /*(1 == uSHELL_IMPLEMENTS_SAVE_HISTORY)*/
    }
} /* m_HistoryInit() */


/*----------------------------------------------------------------------------*/
void Microshell::m_HistoryDeInit(void)
{
    if(true == m_sHistory.bInitialized) {
        m_CircBufDeinit(uSHELL_DEINIT_FULL);
        m_sHistory.bInitialized = false;
        m_sHistory.bEnabled     = false;
    }
} /* m_HistoryDeInit() */


/*----------------------------------------------------------------------------*/
void Microshell::m_HistoryRead(const dir_e eDir)
{
    if((true == m_sHistory.bEnabled) && (false == m_sCircBuf.bIsEmpty)) {
        size_t szReadLen = 0;
        m_CoreCmdLineDelete();
        if(true == m_CircBufRead(eDir, m_pstrInput, &szReadLen)) {
            m_iInputPos = (int)(--szReadLen);
            uSHELL_PRINTF("\r\033[%dC\033[K%s", m_pInst->iPromptLength, m_pstrInput);
        }
    }
} /* m_HistoryRead() */


/*----------------------------------------------------------------------------*/
char* Microshell::m_HistoryGetEntry(const int iIndex)
{
    char *pstrItem = NULL;
    if((true == m_sHistory.bInitialized) && (true == m_sHistory.bEnabled)) {
        pstrItem = m_CircBufGetItem(iIndex);
    }
    return pstrItem;
} /* m_HistoryGetEntry() */


/*----------------------------------------------------------------------------*/
void Microshell::m_HistoryExecuteEntry(const char *pstrIndex)
{
    BIGNUM_T iIndex = 0;
    if(true == asc2int((pstrIndex), &iIndex)) {
        m_CoreCmdLineDelete();
        uSHELL_PUTCH('\r');
        char *pstrHistItem = m_HistoryGetEntry((int)iIndex);
        if(NULL != pstrHistItem) {
#if (defined(__MINGW32__) || defined(_MSC_VER))
            strncpy_s(m_pstrInput, sizeof(m_pstrInput), pstrHistItem, strlen(pstrHistItem));
#else
            strncpy(m_pstrInput, pstrHistItem, (sizeof(m_pstrInput) - 1));
            m_pstrInput[sizeof(m_pstrInput) - 1] = '\0';
            uSHELL_PRINTF("> %s\n", m_pstrInput);
#endif /* (defined(__MINGW32__) || defined(_MSC_VER)) */
            m_CoreExecuteEnterKey();
        } else {
            m_CorePrintMessage(3, 3);    /* history noentry or invalid index */
        }
    } else {
        m_CorePrintMessage(6, 2);    /* sub-shortcut unimplemented */
    }
} /* m_HistoryExecuteEntry() */


/*----------------------------------------------------------------------------*/
void Microshell::m_HistoryWrite(void)
{
    if(true == m_sHistory.bEnabled) {
        if(true == m_CircBufWrite(m_pstrInput, (m_iInputPos + 1))) {
#if (1 == uSHELL_IMPLEMENTS_SAVE_HISTORY)
            if(NULL !=  m_pInst->pfileHistory) {
                fwrite(m_pstrInput, sizeof(char), m_iInputPos, m_pInst->pfileHistory);
                fwrite("\n", sizeof(char), 1, m_pInst->pfileHistory);
            } else {
                m_CorePrintMessage(3, 10);    /* history nofile*/
            }
#endif /*(1 == uSHELL_IMPLEMENTS_SAVE_HISTORY) */
        }
    }
} /* m_HistoryWrite() */


/*----------------------------------------------------------------------------*/
void Microshell::m_HistoryReset(void)
{
    if(true == m_sHistory.bInitialized) {
#if (1 == uSHELL_IMPLEMENTS_CONFIRM_REQUEST)
        if(true == m_CoreConfirmRequest()) {
#endif /*(1 == uSHELL_IMPLEMENTS_CONFIRM_REQUEST) */
            m_CircBufDeinit(uSHELL_DEINIT_RESET);
            m_CorePrintMessage(3, 6); /* history reset */
        }
#if (1 == uSHELL_IMPLEMENTS_CONFIRM_REQUEST)
    }
#endif /*(1 == uSHELL_IMPLEMENTS_CONFIRM_REQUEST) */
} /* m_HistoryReset() */


/*----------------------------------------------------------------------------*/
void Microshell::m_HistoryList(void)
{
    if(true == m_sHistory.bInitialized) {
        if(true == m_sHistory.bEnabled) {
            int iNrElems = m_CircBufShow();
            if(iNrElems > 0) {
                uSHELL_PRINTF(FRMT(uSHELL_SUCCESS_COLOR, "Used %d of %d entries\n"), iNrElems, uSHELL_HISTORY_DEPTH);
            }
        } else {
            m_CorePrintMessage(3, 0);   /* history off */
        }
    } else {
        m_CorePrintMessage(3, 7);    /* history uninitialized */
    }
} /* m_HistoryList() */


/*----------------------------------------------------------------------------*/
void Microshell::m_HistoryEnable(bool bEnable)
{
    if(true == m_sHistory.bInitialized) {
        m_sHistory.bEnabled = bEnable;
        m_CorePrintMessage(3, (int)bEnable); /* history on/off*/
#if (1 == uSHELL_IMPLEMENTS_SMART_PROMPT)
        m_CoreUpdatePrompt(uSHELL_PROMPTI_HISTORY, bEnable);
#endif /* (1 == uSHELL_IMPLEMENTS_SMART_PROMPT) */
    }
}/* m_HistoryEnable() */


#if (1 == uSHELL_IMPLEMENTS_SAVE_HISTORY)

/*----------------------------------------------------------------------------*/
void Microshell::m_HistoryLoadFromFile(void)
{
    size_t szLen = 0;
    if(NULL != m_pInst->pfileHistory) {
        rewind(m_pInst->pfileHistory);
        while(fgets(m_pstrInput, sizeof(m_pstrInput), m_pInst->pfileHistory)) {
            szLen = strcspn(m_pstrInput, "\n");
            m_pstrInput[szLen] = 0;
            m_CircBufWrite(m_pstrInput, (szLen + 1));
        }
    } else {
        m_CorePrintMessage(3, 10);    /* history nofile */
    }
}/* m_HistoryLoadFromFile() */


/*----------------------------------------------------------------------------*/
inline void Microshell::m_HistoryReload(void)
{
    if(true == m_sHistory.bInitialized) {
        m_CircBufDeinit(uSHELL_DEINIT_PARTIAL);
        m_HistoryLoadFromFile();
    }
}/* m_HistoryReload() */
#endif /*(1 == uSHELL_IMPLEMENTS_SAVE_HISTORY) */
#endif /* (1 == uSHELL_IMPLEMENTS_HISTORY) */


/*==============================================================================
                    AUTOCOMPLETE IMPLEMENTATION
==============================================================================*/


#if (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)
void Microshell::m_AutocomplInit(void)
{
    m_sAutocomplete.bEnabled = uSHELL_INIT_AUTOCOMPL_MODE;
#if (1 == uSHELL_IMPLEMENTS_SMART_PROMPT)
    m_CoreUpdatePrompt(uSHELL_PROMPTI_AUTOCOMPLETE, m_sAutocomplete.bEnabled);
#endif /*(1 == uSHELL_IMPLEMENTS_SMART_PROMPT)*/
    m_AutocomplFill(uSHELL_AUTOCOMPL_RELOAD);
} /* m_AutocomplInit() */


/*----------------------------------------------------------------------------*/
inline void Microshell::m_AutocomplReInit(void)
{
    if(true == m_sAutocomplete.bEnabled) {
        m_sAutocomplete.bFirstFilter = true;
    }
} /* m_AutocomplReInit() */


/*----------------------------------------------------------------------------*/
void Microshell::m_AutocomplReset(bool bReinit)
{
    memset(m_pInst->piAutocompleteIndexArray, uSHELL_INVALID_VALUE, m_pInst->iNrFunctions);
    m_sAutocomplete.iSearchPos       = 0;
    m_sAutocomplete.iSavedSearchPos  = 0;
    m_sAutocomplete.iSearchIndex     = 0;
    m_sAutocomplete.bFirstFilter     = true;
    m_sAutocomplete.bFoundExactMatch = false;
    m_sAutocomplete.iNrCrtElems      = 0;
    m_AutocomplFill(bReinit);
} /* m_AutocomplReset() */


/*----------------------------------------------------------------------------*/
void Microshell::m_AutocomplGetCommon(void)
{
    if(true == m_sAutocomplete.bEnabled) {
        int  iCount = 0;
        bool bFound = false;
        const char *pstrRef = NULL, *pstrCrt = NULL;
        char cRef = '\0', cCrt = '\0';

        m_AutocomplFilter();
        if(m_sAutocomplete.iNrCrtElems > 0) {
            if(m_sAutocomplete.iNrCrtElems > 1) {
                while(false == bFound) {
                    iCount = 0;
                    pstrRef = m_pInst->psFuncDefArray[m_pInst->piAutocompleteIndexArray[0]].pstrFctName;
                    for(int i = 1; i < m_sAutocomplete.iNrCrtElems; ++i) {
                        pstrCrt = m_pInst->psFuncDefArray[m_pInst->piAutocompleteIndexArray[i]].pstrFctName;
                        if((cRef = pstrRef[m_sAutocomplete.iSearchPos]) == (cCrt = pstrCrt[m_sAutocomplete.iSearchPos])) {
                            ++iCount;
                        }
                        if(('\0' == cRef) || ('\0' == cCrt)) {
                            m_sAutocomplete.bFoundExactMatch = true;
                        }
                    }
                    if(++iCount == m_sAutocomplete.iNrCrtElems) {
                        ++(m_sAutocomplete.iSearchPos);
                    } else {
                        bFound = true;
                    }
                }
            } else { /*1 == m_sAutocomplete.iNrCrtElems */
                m_sAutocomplete.iSearchPos = (int)strlen(m_pInst->psFuncDefArray[m_pInst->piAutocompleteIndexArray[0]].pstrFctName);
                m_sAutocomplete.bFoundExactMatch = true;
            }
            for(int i = m_sAutocomplete.iSavedSearchPos; i < m_sAutocomplete.iSearchPos; ++i) {
                char cCrtChar = (m_pInst->psFuncDefArray[m_pInst->piAutocompleteIndexArray[0]].pstrFctName)[i];
                m_pstrInput[i] = cCrtChar;
                ++m_iInputPos;
                uSHELL_PUTCH(cCrtChar);
            }
            if(1 == m_sAutocomplete.iNrCrtElems) {
                m_AutocomplInsEndSpace();
            }
        }
    }
} /* m_AutocomplGetCommon() */


/*----------------------------------------------------------------------------*/
void Microshell::m_AutocomplRead(const dir_e eDir)
{
    if(true == m_sAutocomplete.bEnabled) {
        if(uSHELL_KEY_BACKSPACE == m_sAutocomplete.cPrevKey) {
            m_AutocomplReset(false);
        }
        if(m_sAutocomplete.iNrCrtElems != 0) {
            switch(eDir) {
                case uSHELL_DIR_FORWARD  : {
                        if(true == m_sAutocomplete.bFoundExactMatch) {
                            m_sAutocomplete.iSearchIndex++;
                        }
                    }
                    break;
                case uSHELL_DIR_BACKWARD : {
                        m_sAutocomplete.iSearchIndex--;
                    }
                    break;
                default:
                    break;
            }
            if(false == m_sAutocomplete.bFoundExactMatch) {
                m_sAutocomplete.bFoundExactMatch = true;
            }
            m_sAutocomplete.iSearchIndex = (uSHELL_INVALID_VALUE == m_sAutocomplete.iSearchIndex) ? (m_sAutocomplete.iNrCrtElems - 1) : m_sAutocomplete.iSearchIndex;
            m_sAutocomplete.iSearchIndex %= m_sAutocomplete.iNrCrtElems;
            uSHELL_PRINTF("\r\033[%dC\033[K", m_pInst->iPromptLength);
#if (defined(__MINGW32__) || defined(_MSC_VER))
            strncpy_s(m_pstrInput, sizeof(m_pstrInput), m_pInst->psFuncDefArray[m_pInst->piAutocompleteIndexArray[m_sAutocomplete.iSearchIndex]].pstrFctName, sizeof(m_pstrInput));
#else
            strncpy(m_pstrInput, m_pInst->psFuncDefArray[m_pInst->piAutocompleteIndexArray[m_sAutocomplete.iSearchIndex]].pstrFctName, sizeof(m_pstrInput));
#endif /*(defined(__MINGW32__) || defined(_MSC_VER))*/
            m_iInputPos = (int)strlen(m_pstrInput);
            m_AutocomplInsEndSpace();
            uSHELL_PRINTF("\r\033[%dC\033[K%s", m_pInst->iPromptLength, m_pstrInput);
        }
    }
} /* m_AutocomplRead() */


/*----------------------------------------------------------------------------*/
void Microshell::m_AutocomplFilter(void)
{
    int iCount = 0, iIndex = 0;
    int iLimit = (true == m_sAutocomplete.bFirstFilter) ? m_pInst->iNrFunctions : m_sAutocomplete.iNrCrtElems;
    const char *pstrCrtItem = NULL;

    m_sAutocomplete.iSavedSearchPos = (int)strlen(m_pstrInput);
    for(int i = 0; i < iLimit; ++i) {
        iIndex = (true == m_sAutocomplete.bFirstFilter) ? i : m_pInst->piAutocompleteIndexArray[i];
        pstrCrtItem = m_pInst->psFuncDefArray[iIndex].pstrFctName;
        if(0 == strncmp(pstrCrtItem, m_pstrInput, m_sAutocomplete.iSavedSearchPos)) {
            m_pInst->piAutocompleteIndexArray[iCount++] = iIndex;
        }
    }
    if(true == m_sAutocomplete.bFirstFilter) {
        m_sAutocomplete.bFirstFilter = false;
    }
    m_sAutocomplete.iNrCrtElems = iCount;
} /* m_AutocomplFilter() */


/*----------------------------------------------------------------------------*/
void Microshell::m_AutocomplFill(const bool bFull)
{
    if(true == bFull) {
        for(int i = 0; i < m_pInst->iNrFunctions; ++i) {
            m_pInst->piAutocompleteIndexArray[i] = i;
        }
        m_sAutocomplete.iNrCrtElems = m_pInst->iNrFunctions;
    } else {
        m_AutocomplGetCommon();
    }
} /* m_AutocomplFill() */


/*----------------------------------------------------------------------------*/
void Microshell::m_AutocomplInsEndSpace(void)
{
    if((true == m_sAutocomplete.bFoundExactMatch)) {
        m_pstrInput[m_iInputPos++] = uSHELL_KEY_SPACE;
        m_pstrInput[m_iInputPos] = '\0';
        uSHELL_PUTCH(uSHELL_KEY_SPACE);
    }
} /* m_AutocomplInsEndSpace() */


/*----------------------------------------------------------------------------*/
void Microshell::m_AutocomplEnable(const bool bEnable)
{
    m_sAutocomplete.bEnabled = bEnable;
    m_CorePrintMessage(1, (int)bEnable); /* autocomplete on/off */
#if (1 == uSHELL_IMPLEMENTS_SMART_PROMPT)
    m_CoreUpdatePrompt(uSHELL_PROMPTI_AUTOCOMPLETE, bEnable);
#endif /* (1 == uSHELL_IMPLEMENTS_SMART_PROMPT) */
}/* m_AutocomplEnable() */
#endif /*(1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)*/


/*==============================================================================
                           EDITMODE IMPLEMENTATION
==============================================================================*/


#if (1 == uSHELL_IMPLEMENTS_EDITMODE)
/*----------------------------------------------------------------------------*/
void Microshell::m_EditMoveCursorDirSteps(const dir_e eDir, const int iSteps)
{
    if(iSteps > 1) {
        uSHELL_PRINTF("\033[%d%c", iSteps, ((uSHELL_DIR_FORWARD == eDir) ? 'C' : 'D'));
    } else {
        uSHELL_PRINTF("\033[%c", ((uSHELL_DIR_FORWARD == eDir) ? 'C' : 'D'));
    }
} /*m_EditMoveCursorDirSteps() */


/*----------------------------------------------------------------------------*/
bool Microshell::m_EditMoveCursor(const dir_e eDir)
{
    if(true == m_bEditMode) {
        switch(eDir) {
            case uSHELL_DIR_FORWARD : {
                    if((m_iInputPos <= ((int)(sizeof(m_pstrInput) - 1))) && (m_iCursorPos < m_iInputPos)) {
                        m_EditMoveCursorDirSteps(uSHELL_DIR_FORWARD, 1);
                        ++m_iCursorPos;
                    } else {
                        m_CorePutString("\033[D\033[C");
                    }
                }
                break;
            case uSHELL_DIR_BACKWARD: {
                    if(m_iCursorPos > 0) {
                        m_EditMoveCursorDirSteps(uSHELL_DIR_BACKWARD, 1);
                        --m_iCursorPos;
                    }
                }
                break;
            case uSHELL_DIR_HOME    : {
                    if(m_iCursorPos > 0) {
                        m_EditMoveCursorDirSteps(uSHELL_DIR_BACKWARD, m_iCursorPos);
                        m_iCursorPos = 0;
                    }
                }
                break;
            case uSHELL_DIR_END     : {
                    if(m_iCursorPos < m_iInputPos) {
                        m_EditMoveCursorDirSteps(uSHELL_DIR_FORWARD, (m_iInputPos - m_iCursorPos));
                        m_iCursorPos = m_iInputPos;
                    }
                }
                break;
            default:
                break;
        }
        return true;
    }
    return false;
} /*m_EditMoveCursor()*/


/*----------------------------------------------------------------------------*/
void Microshell::m_EditDeleteUnderCursor(void)
{
    if((m_iInputPos > 0) && (m_iInputPos > m_iCursorPos)) {
        for(int i = 0; i < (m_iInputPos - m_iCursorPos); ++i) {
            *(m_pstrInput + (m_iCursorPos + i)) = *(m_pstrInput + (m_iCursorPos + i + 1));
        }
        m_iInputPos--;
        if(m_iInputPos - m_iCursorPos > 0) {
            uSHELL_PRINTF("\033[K%s\033[%dD", (m_pstrInput + m_iCursorPos), (m_iInputPos - m_iCursorPos));
        } else {
            uSHELL_PRINTF("\033[K%s", (m_pstrInput + m_iCursorPos));
        }
    }
} /* m_EditDeleteUnderCursor() */


/*----------------------------------------------------------------------------*/
void Microshell::m_EditDeleteBackward(void)
{
    if(m_iCursorPos > 0) {
        for(int i = 0; i < (m_iInputPos - m_iCursorPos + 1); ++i) {
            *(m_pstrInput + (m_iCursorPos + i - 1)) = *(m_pstrInput + (m_iCursorPos + i));
        }
        --m_iInputPos;
        --m_iCursorPos;
        uSHELL_PRINTF("\033[D \033[D\033[K%s", (m_pstrInput + m_iCursorPos));
        if(m_iInputPos > m_iCursorPos) {
            m_EditMoveCursorDirSteps(uSHELL_DIR_BACKWARD, (m_iInputPos - m_iCursorPos));
        }
    }
} /* m_EditDeleteBackward() */


/*----------------------------------------------------------------------------*/
void Microshell::m_EditInsertUnderCursor(const char cKeyPressed)
{
    if(m_iInputPos < ((int)(sizeof(m_pstrInput) - 1))) {
        int iCrtMovePos = m_iInputPos;
        while(iCrtMovePos >= m_iCursorPos) {
            *(m_pstrInput + iCrtMovePos + 1) = *(m_pstrInput + iCrtMovePos);
            --iCrtMovePos;
        }
        *(m_pstrInput + m_iCursorPos++) = cKeyPressed;
        *(m_pstrInput + ++m_iInputPos) = '\0';
        uSHELL_PRINTF("%s\33[%dD", (m_pstrInput + m_iCursorPos - 1), (m_iInputPos - m_iCursorPos));
    }
} /* m_EditInsertUnderCursor() */


/*----------------------------------------------------------------------------*/
void Microshell::m_EditDeleteBackwardToHome(void)
{
    if(m_iCursorPos > 0) {
        int iLen = m_iInputPos - m_iCursorPos;
        if(iLen > 0) {
            for(int i = 0; i < iLen; ++i) {
                m_pstrInput[i] = m_pstrInput[i + m_iCursorPos];
            }
            memset(&m_pstrInput[iLen], 0, m_iCursorPos);
            m_iCursorPos = 0;
            m_iInputPos = iLen;
            uSHELL_PRINTF("\r\033[%dC\033[K%s\033[%dD", m_pInst->iPromptLength, m_pstrInput, iLen);
        } else {
            m_CoreCmdLineDelete();
        }
#if (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)
        m_AutocomplReset((0 == m_iInputPos));
#endif /* (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE) */
    }
} /* m_EditDeleteBackwardToHome() */


/*----------------------------------------------------------------------------*/
void Microshell::m_EditDeleteForwardToEnd(void)
{
    int iLen = m_iInputPos - m_iCursorPos;
    if(iLen > 0) {
        if(m_iCursorPos > 0) {
            memset(&m_pstrInput[m_iCursorPos], 0, iLen);
            m_iInputPos = m_iCursorPos;
            m_CorePutString("\033[K");
        } else {
            m_CoreCmdLineDelete();
        }
#if (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)
        m_AutocomplReset((0 == m_iInputPos));
#endif /* (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE) */
    }
} /* m_EditDeleteForwardToEnd() */
#endif /* (1 == uSHELL_IMPLEMENTS_EDITMODE) */


/*----------------------------------------------------------------------------*/
#if (1 == uSHELL_IMPLEMENTS_KEY_DECODER)
void Microshell::keydecoder(void)
{
    char cRead;
    m_CorePutString(":exit:$\n\r");
    do {
        if(uSHELL_KEY_ENTER == (cRead = (char)uSHELL_GETCH())) {
            uSHELL_PRINTF("%02X\n", uSHELL_KEY_ENTER);
        } else {
            uSHELL_PRINTF("%02X|%c ", (unsigned char)cRead, (true == uSHELL_ISPRINT(cRead)) ? cRead : ' ');
        }
    } while('$' != cRead);
    m_CorePutString(uSHELL_NEWLINE);
}
#endif /*(1 == uSHELL_IMPLEMENTS_KEY_DECODER)*/


/*==============================================================================
                    PRIVATE VARIABLES INITIALIZATION
==============================================================================*/


uShellInst_s *Microshell::m_pInst = nullptr;
command_s Microshell::m_sCommand = {};
char Microshell::m_pstrInput[uSHELL_MAX_INPUT_BUF_LEN] = {0};
int  Microshell::m_iInputPos = 0;
int  Microshell::m_iCursorPos = 0;

#if (1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)
uShellInst_s *Microshell::m_pInstBackup = nullptr;
int  Microshell::m_iInstanceCounter = 0;
#endif /*(1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)*/

#if (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)
autocomplete_s Microshell::m_sAutocomplete = {};
#endif /* (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE) */

#if (1 == uSHELL_IMPLEMENTS_HISTORY)
circbuf_s Microshell::m_sCircBuf = {};
history_s Microshell::m_sHistory = {};
#endif /* (1 == uSHELL_IMPLEMENTS_HISTORY) */

#if (1 == uSHELL_IMPLEMENTS_EDITMODE)
#if defined(uSHELL_EDIT_MODE_DEFAULT_ACTIVE)
bool Microshell::m_bEditMode = true;
#else
bool Microshell::m_bEditMode = false;
#endif /* !defined(uSHELL_EDIT_MODE_DEFAULT_ACTIVE) */
#endif /* (1 == uSHELL_IMPLEMENTS_EDITMODE) */

#if (1 == uSHELL_IMPLEMENTS_DISABLE_ECHO)
bool Microshell::m_bEchoOn = true;
#endif /* (1 == uSHELL_IMPLEMENTS_DISABLE_ECHO) */

#if (defined(uSHELL_IMPLEMENTS_STRINGS) && (1 == uSHELL_SUPPORTS_SPACED_STRINGS))
char Microshell::m_cStringBorderSymbol = uSHELL_KEY_QUOTATION_MARK;
#endif /*(defined(uSHELL_IMPLEMENTS_STRINGS) && (1 == uSHELL_SUPPORTS_SPACED_STRINGS))*/

#if (1 == uSHELL_IMPLEMENTS_SMART_PROMPT)
#define  uSHELL_PROMPT_TABLE_BEGIN      char Microshell::m_pstrPrompt[uSHELL_PROMPTI_LAST + 1] = ""
#define  uSHELL_PROMPT_CELL(a, b, c)        ":"
#define  uSHELL_PROMPT_TABLE_END        ;
#include uSHELL_PROMPT_CONFIG_FILE
#undef   uSHELL_PROMPT_TABLE_BEGIN
#undef   uSHELL_PROMPT_CELL
#undef   uSHELL_PROMPT_TABLE_END

#define  uSHELL_PROMPT_TABLE_BEGIN      const char Microshell::m_pstrPromptInfo[uSHELL_PROMPTI_LAST + 1] = ""
#define  uSHELL_PROMPT_CELL(a, b, c)        #b
#define  uSHELL_PROMPT_TABLE_END        ;
#include uSHELL_PROMPT_CONFIG_FILE
#undef   uSHELL_PROMPT_TABLE_BEGIN
#undef   uSHELL_PROMPT_CELL
#undef   uSHELL_PROMPT_TABLE_END

#define  uSHELL_PROMPT_TABLE_BEGIN      const char Microshell::m_pstrPromptInfoEditMode[uSHELL_PROMPTI_LAST + 1] = ""
#define  uSHELL_PROMPT_CELL(a, b, c)        #c
#define  uSHELL_PROMPT_TABLE_END        ;
#include uSHELL_PROMPT_CONFIG_FILE
#undef   uSHELL_PROMPT_TABLE_BEGIN
#undef   uSHELL_PROMPT_CELL
#undef   uSHELL_PROMPT_TABLE_END
#endif /*(1 == uSHELL_IMPLEMENTS_SMART_PROMPT)*/

#if (1 == uSHELL_IMPLEMENTS_COMMAND_HELP)
#define  uSHELL_DATA_TYPES_TABLE_BEGIN  const char Microshell::m_vstrTypeMarks[uSHELL_TYPE_LAST] = {
#define  uSHELL_DATA_TYPE(a, b)             b,
#define  uSHELL_DATA_TYPES_TABLE_END    };
#include uSHELL_DATA_TYPES_CONFIG_FILE
#undef   uSHELL_DATA_TYPES_TABLE_BEGIN
#undef   uSHELL_DATA_TYPE
#undef   uSHELL_DATA_TYPES_TABLE_END
#endif /*(1 == uSHELL_IMPLEMENTS_COMMAND_HELP)*/

#define  uSHELL_DATA_TYPES_TABLE_BEGIN      const char *Microshell::m_vstrTypeNames[uSHELL_TYPE_LAST] = {
#define  uSHELL_DATA_TYPE(a, b)                 #a,
#define  uSHELL_DATA_TYPES_TABLE_END        };
#include uSHELL_DATA_TYPES_CONFIG_FILE
#undef   uSHELL_DATA_TYPES_TABLE_BEGIN
#undef   uSHELL_DATA_TYPE
#undef   uSHELL_DATA_TYPES_TABLE_END

const char *Microshell::m_pstrCoreShortcutCaption = "\t##|#|i|s : info short|all|i|substr s\n\r"
#if (1 == uSHELL_IMPLEMENTS_SHELL_EXIT)
                                                    "\t#q : quit\n\r"
#endif /* (1 == uSHELL_IMPLEMENTS_SHELL_EXIT) */
#if (1 == uSHELL_IMPLEMENTS_DISABLE_ECHO)
                                                    "\t#E|e : echo on|off\n\r"
#endif /* (1 == uSHELL_IMPLEMENTS_DISABLE_ECHO) */
#if (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)
                                                    "\t#A|a : autocomplete on|off\n\r"
#endif /*(1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE) */
#if (1 == uSHELL_IMPLEMENTS_HISTORY)
                                                    "\t#H|h|l|L|r|i : history on|off|list|load|reset|exec i\n\r"
#endif /*(1 == uSHELL_IMPLEMENTS_HISTORY) */
#if defined(uSHELL_IMPLEMENTS_STRINGS)
#if (1 == uSHELL_SUPPORTS_SPACED_STRINGS)
                                                    "\t#sD : set string delimiter set D|reset; default \"\n\r"
#endif /*defined(uSHELL_IMPLEMENTS_STRINGS)*/
#endif /*(1 == uSHELL_SUPPORTS_SPACED_STRINGS)*/
#if (1 == uSHELL_IMPLEMENTS_KEY_DECODER)
                                                    "\t#k : keydecoder\n\r"
#endif /*(1 == uSHELL_IMPLEMENTS_KEY_DECODER)*/
                                                    ;
