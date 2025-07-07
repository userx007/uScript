#ifndef USHELL_CORE_DATATYPES_H
#define USHELL_CORE_DATATYPES_H

/* disable warning like: padding added after data member */
#ifdef _MSC_VER
    #pragma warning(disable : 4820)
#endif

#include "ushell_core_settings.h"
#include <cstddef>
#if (1 == uSHELL_IMPLEMENTS_SAVE_HISTORY)
    #include <cstdio>
#endif /*(1 == uSHELL_IMPLEMENTS_SAVE_HISTORY)*/

#define  uSHELL_DATA_TYPES_TABLE_BEGIN      typedef enum dataType_e_ {
#define  uSHELL_DATA_TYPE(a, b)                 uSHELL_DATA_TYPE_##a,
#define  uSHELL_DATA_TYPES_TABLE_END        uSHELL_DATA_TYPE_LAST } dataType_e;
#include uSHELL_DATA_TYPES_CONFIG_FILE
#undef   uSHELL_DATA_TYPES_TABLE_BEGIN
#undef   uSHELL_DATA_TYPE
#undef   uSHELL_DATA_TYPES_TABLE_END

/** \brief microshell core errors */
enum {
    uSHELL_ERR_OK                        =  0,
    uSHELL_ERR_ITEM_NOT_FOUND            = -1,
    uSHELL_ERR_FUNCTION_NOT_FOUND        = -2,
    uSHELL_ERR_WRONG_NUMBER_ARGS         = -3,
    uSHELL_ERR_PARAM_TYPE_NOT_IMPLEM     = -4,
    uSHELL_ERR_PARAMS_PATTERN_NOT_IMPLEM = -5,
    uSHELL_ERR_STRING_NOT_CLOSED         = -6,
    uSHELL_ERR_TOO_MANY_ARGS             = -7,
    uSHELL_ERR_INVALID_NUMBER            = -8,
    uSHELL_ERR_VALUE_TOO_BIG             = -9,
    uSHELL_ERR_LAST
};

#if (1 == uSHELL_IMPLEMENTS_HISTORY)
typedef enum {
    uSHELL_DEINIT_RESET = 0,
    uSHELL_DEINIT_FULL,
    uSHELL_DEINIT_PARTIAL,
    uSHELL_DEINIT_LAST
} deinit_e;
#endif /*(1 == uSHELL_IMPLEMENTS_HISTORY)*/

#if (1 == uSHELL_IMPLEMENTS_EDITMODE) || (1 == uSHELL_IMPLEMENTS_HISTORY) || (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)
typedef enum {
    uSHELL_DIR_BACKWARD = 0,
    uSHELL_DIR_FORWARD,
    uSHELL_DIR_HOME,
    uSHELL_DIR_END,
    uSHELL_DIR_LAST
} dir_e;
#endif /*(1 == uSHELL_IMPLEMENTS_EDITMODE) || (1 == uSHELL_IMPLEMENTS_HISTORY) || (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)*/

#if (1 == uSHELL_IMPLEMENTS_HISTORY)
typedef struct {
    void**  ppData;
    size_t* pDataSize;
    int     iCrtPosWrite;
    int     iCrtPosRead;
    dir_e   ePrevDir;
    bool    bIsFull;
    bool    bIsEmpty;
} circbuf_s;

typedef struct {
    bool      bInitialized;
    bool      bEnabled;
} history_s;
#endif /*(1 == uSHELL_IMPLEMENTS_HISTORY)*/

#if (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)
typedef struct {
    int  iNrCrtElems;
    int  iSearchPos;
    int  iSavedSearchPos;
    int  iSearchIndex;
    char cPrevKey;
    char cCrtKey;
    bool bFirstFilter;
    bool bFoundExactMatch;
    bool bEnabled;
} autocomplete_s;
#endif /*(1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)*/

/* parsing storage structure */
typedef struct {
    const char*  pstrFctName;
#if defined(uSHELL_IMPLEMENTS_NUMBERS_64BIT)             /* 64 bit -> 'l' ([l]ong) */
    num64_t      vl[uSHELL_MAX_PARAMS_NUM64];
    unsigned int iNrNums64;
#endif /* defined(uSHELL_IMPLEMENTS_NUMBERS_64BIT)*/
#if defined(uSHELL_IMPLEMENTS_NUMBERS_32BIT)             /* 32 bit -> 'i' ([i]nteger) */
    num32_t      vi[uSHELL_MAX_PARAMS_NUM32];
    unsigned int iNrNums32;
#endif /*defined(uSHELL_IMPLEMENTS_NUMBERS_32BIT)*/
#if defined(uSHELL_IMPLEMENTS_NUMBERS_16BIT)             /* 16 bit -> 'w' ([w]ord) */
    num16_t      vw[uSHELL_MAX_PARAMS_NUM16];
    unsigned int iNrNums16;
#endif /*defined(uSHELL_IMPLEMENTS_NUMBERS_16BIT) */
#if defined(uSHELL_IMPLEMENTS_NUMBERS_8BIT)              /* 8 bit -> 'b' ([b]yte) */
    num8_t       vb[uSHELL_MAX_PARAMS_NUM8];
    unsigned int iNrNums8;
#endif /*defined(uSHELL_IMPLEMENTS_NUMBERS_8BIT) */
#ifdef uSHELL_IMPLEMENTS_NUMBERS_FLOAT                   /* float /double -> 'f' ([f]loat) */
    numfp_t      vf[uSHELL_MAX_PARAMS_FLOAT];
    unsigned int iNrNumsFloat;
#endif /* uSHELL_IMPLEMENTS_NUMBERS_FLOAT */
#if defined(uSHELL_IMPLEMENTS_STRINGS)                   /* char* -> 's' ([s]tring) */
    str_t*       vs[uSHELL_MAX_PARAMS_STRING];
    unsigned int iNrStrings;
#endif /* defined(uSHELL_IMPLEMENTS_STRINGS) */
#if defined(uSHELL_IMPLEMENTS_BOOLEAN)                   /* bool -> 'o' (b[o]ol) */
    bool         vo[uSHELL_MAX_PARAMS_BOOLEAN];
    unsigned int iNrBools;
#endif /*defined(uSHELL_IMPLEMENTS_BOOLEAN)*/
    int         iFctIndex;
    int         iTypIndex;
    int         iErrorInfo;
    dataType_e  eDataType;
} command_s;

typedef struct {
    const char* const pstrFctName;
    const char* const pstrFuncParamDef;
} fctDef_s;

/** \brief command execution function pointer */
typedef int (*PFEXEC)(const command_s *psCmd);

/** \brief shortcut execution function pointer */
typedef void (*PFSHORTCUT)(const char *pstrArgs);

/** \brief structure with the shortcut mapping */
typedef struct {
    char cSymbol;
    PFSHORTCUT pfShortcut;
} shortcut_s;

/** \brief main structure */
typedef struct {
    const fctDef_s         *const psFuncDefArray;
    shortcut_s             *psShortcutsArray;
#if (1 == uSHELL_IMPLEMENTS_COMMAND_HELP)
    const char* const*      ppstrInfoArray;
#if (1 == uSHELL_IMPLEMENTS_USER_SHORTCUTS)
    const char* const*      ppstrShortcutsInfoArray;
#endif /*(1 == uSHELL_IMPLEMENTS_USER_SHORTCUTS)*/
#endif /*(1 == uSHELL_IMPLEMENTS_COMMAND_HELP)*/
#if (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)
    int                    *piAutocompleteIndexArray;
#endif /* (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE) */
#if (1 == uSHELL_IMPLEMENTS_SAVE_HISTORY)
    FILE                  *pfileHistory;
#endif /*(1 == uSHELL_IMPLEMENTS_SAVE_HISTORY)*/
#if (1 == uSHELL_IMPLEMENTS_SHELL_EXIT)
    bool                    bKeepRuning;
#endif /*(1 == uSHELL_IMPLEMENTS_SHELL_EXIT)*/
    const int               iNrFunctions;
    const int               iNrShortcuts;
    PFEXEC                  pfExec;
    char                    vstrPrompt[uSHELL_PROMPT_MAX_LEN];
    int                     iPromptLength;
} uShellInst_s;

// Define EXPORTED for any platform
#if defined _WIN32 || defined __CYGWIN__
    #ifdef __GNUC__
        #define EXPORTED __attribute__ ((dllexport))
    #else
        #define EXPORTED __declspec(dllexport)
    #endif
    #define NOT_EXPORTED
#elif (defined(__GNUC__) && defined(__AVR__)) /* Arduino AVR compiler */
    #define EXPORTED
    #define NOT_EXPORTED
#else
    #if __GNUC__ >= 4
        #define EXPORTED __attribute__ ((visibility ("default")))
        #define NOT_EXPORTED  __attribute__ ((visibility ("hidden")))
    #else
        #define EXPORTED
        #define NOT_EXPORTED
    #endif
#endif


using uShellPluginInterface = uShellInst_s;

#ifdef __cplusplus
extern "C" {
#endif

#if (1 == uSHELL_SUPPORTS_EXTERNAL_USER_DATA)
    EXPORTED uShellPluginInterface *uShellPluginEntry(void *pvUserData);
#else
    EXPORTED uShellPluginInterface *uShellPluginEntry(void);
#endif /*(1 == uSHELL_SUPPORTS_EXTERNAL_USER_DATA)*/
EXPORTED void uShellPluginExit( uShellPluginInterface *ptrPlugin );

#ifdef __cplusplus
}
#endif

#endif /* USHELL_CORE_DATATYPES_H */