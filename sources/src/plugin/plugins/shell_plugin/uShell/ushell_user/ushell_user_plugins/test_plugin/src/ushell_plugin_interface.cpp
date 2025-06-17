#include "ushell_core_settings.h"
#include "ushell_core_datatypes.h"
#include "ushell_plugin_datatypes.h"

/* user commands dispatcher */
static int uShellExecuteCommand( const command_s *psCmd );

#if (1 == uSHELL_SUPPORTS_EXTERNAL_USER_DATA)
    void *pvLocalUserData = nullptr;
#endif /* (1 == uSHELL_SUPPORTS_EXTERNAL_USER_DATA) */

/* disable warnings */
#if defined (__GNUC__) && defined(__AVR__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wmissing-braces"
#endif /*defined (__GNUC__) && defined(__AVR__)*/

/** \brief define array of functions (basic properties) */
#define  uSHELL_COMMANDS_TABLE_BEGIN        static const fctDef_s g_vsFuncDefArray[] = {
#define  uSHELL_COMMAND_PARAMS_PATTERN(t)
#define  uSHELL_COMMAND(a,b,c)                  { #a, #b },
#define  uSHELL_COMMANDS_TABLE_END          };
#include uSHELL_COMMANDS_CONFIG_FILE
#undef   uSHELL_COMMANDS_TABLE_BEGIN
#undef   uSHELL_COMMAND_PARAMS_PATTERN
#undef   uSHELL_COMMAND
#undef   uSHELL_COMMANDS_TABLE_END

/** \brief define array of functions (extended properties) */
#define  uSHELL_COMMANDS_TABLE_BEGIN        static const fctDefEx_s g_vsFuncDefExArray[] = {
#define  uSHELL_COMMAND_PARAMS_PATTERN(t)
#if (defined(__GNUC__) && defined(__AVR__)) /* Arduino AVR compiler */
    #define  uSHELL_COMMAND(a,b,c)          { (v_fctptr_t)a, b##_type },
#elif ((defined(__GNUC__) && defined(__linux__)) || defined(__MINGW32__))
    #ifdef __cplusplus
        #define  uSHELL_COMMAND(a,b,c)      { (v_fctptr_t)a, b##_type },
    #else
        #define  uSHELL_COMMAND(a,b,c)      { (fctype_u)(b##_fctptr_t)a, b##_type },
    #endif  // __cplusplus
#elif (defined(_MSC_VER)) /* i.e MinGW or Microsoft VisualStudio for Windows console */
    #ifdef __cplusplus
        #define  uSHELL_COMMAND(a,b,c)      { (v_fctptr_t)a, b##_type },
    #else
        #define  uSHELL_COMMAND(a,b,c)      { (b##_fctptr_t)a, b##_type },
    #endif
#elif (defined(__ghs) || defined(__ghs__))   /* Green Hills Software compiler */
    #define  uSHELL_COMMAND(a,b,c)          { a, b##_type },
#else
    #error "Build variant not set, please define it..."
#endif
#define  uSHELL_COMMANDS_TABLE_END          };
#include uSHELL_COMMANDS_CONFIG_FILE
#undef   uSHELL_COMMANDS_TABLE_BEGIN
#undef   uSHELL_COMMAND_PARAMS_PATTERN
#undef   uSHELL_COMMAND
#undef   uSHELL_COMMANDS_TABLE_END

/* end of disable warnings */
#if (defined (__GNUC__) && defined(__AVR__))
    #pragma GCC diagnostic pop
#endif /*defined (__GNUC__) && defined(__AVR__)*/

/* info for functions */
#if (1 == uSHELL_IMPLEMENTS_COMMAND_HELP)
    #define  uSHELL_COMMANDS_TABLE_BEGIN             static const char* const g_vstrInfoArray[] = {
    #define  uSHELL_COMMAND_PARAMS_PATTERN(t)
    #define  uSHELL_COMMAND(a,b,c)                  c,
    #define  uSHELL_COMMANDS_TABLE_END          };
    #include uSHELL_COMMANDS_CONFIG_FILE
    #undef   uSHELL_COMMANDS_TABLE_BEGIN
    #undef   uSHELL_COMMAND_PARAMS_PATTERN
    #undef   uSHELL_COMMAND
    #undef   uSHELL_COMMANDS_TABLE_END
#endif /*(1 == uSHELL_IMPLEMENTS_COMMAND_HELP)*/

/* autocomplete index array */
#if (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)
static int g_viAutocompleteIndexArray[uSHELL_NR_ELEMS(g_vsFuncDefArray)] = { 0 };
#endif /*(1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)*/

/* user shortcuts callbacks declaration */
#if (1 == uSHELL_IMPLEMENTS_USER_SHORTCUTS)
    #define  uSHELL_USER_SHORTCUTS_TABLE_BEGIN
    #define  uSHELL_USER_SHORTCUT(a,b,c)             extern void uShellUserHandleShortcut_##b( const char *pstrArgs );
    #define  uSHELL_USER_SHORTCUTS_TABLE_END
    #include uSHELL_USER_SHORTCUTS_CONFIG_FILE
    #undef   uSHELL_USER_SHORTCUTS_TABLE_BEGIN
    #undef   uSHELL_USER_SHORTCUT
    #undef   uSHELL_USER_SHORTCUTS_TABLE_END
#endif /*(1 == uSHELL_IMPLEMENTS_USER_SHORTCUTS)*/

/* user shortcuts array */
#if (1 == uSHELL_IMPLEMENTS_USER_SHORTCUTS)
#define  uSHELL_USER_SHORTCUTS_TABLE_BEGIN       static shortcut_s g_vsShortcutsArray[] = { { ' ', nullptr }
#define  uSHELL_USER_SHORTCUT(a,b,c)                   ,{ a, uShellUserHandleShortcut_##b }
#define  uSHELL_USER_SHORTCUTS_TABLE_END          };
#include uSHELL_USER_SHORTCUTS_CONFIG_FILE
#undef   uSHELL_USER_SHORTCUTS_TABLE_BEGIN
#undef   uSHELL_USER_SHORTCUT
#undef   uSHELL_USER_SHORTCUTS_TABLE_END
#else
static shortcut_s g_vsShortcutsArray[] = { { ' ', nullptr } };
#endif /*(1 == uSHELL_IMPLEMENTS_USER_SHORTCUTS)*/

#if (1 == uSHELL_IMPLEMENTS_USER_SHORTCUTS)
    /* user shortcuts help info array */
    #define  uSHELL_USER_SHORTCUTS_TABLE_BEGIN       static const char* const g_vstrShortcutsInfoArray[] = {
    #define  uSHELL_USER_SHORTCUT(a,b,c)                   c,
    #define  uSHELL_USER_SHORTCUTS_TABLE_END          };
    #include uSHELL_USER_SHORTCUTS_CONFIG_FILE
    #undef   uSHELL_USER_SHORTCUTS_TABLE_BEGIN
    #undef   uSHELL_USER_SHORTCUT
    #undef   uSHELL_USER_SHORTCUTS_TABLE_END
#endif /*(1 == uSHELL_IMPLEMENTS_USER_SHORTCUTS)*/

/* partial initialization of the shell instance structure */
static uShellInst_s sShellInstance = {
    .psFuncDefArray           = g_vsFuncDefArray,
    .psShortcutsArray         = g_vsShortcutsArray,
#if (1 == uSHELL_IMPLEMENTS_COMMAND_HELP)
    .ppstrInfoArray           = g_vstrInfoArray,
#if (1 == uSHELL_IMPLEMENTS_USER_SHORTCUTS)
    .ppstrShortcutsInfoArray  = g_vstrShortcutsInfoArray,
#endif /*(1 == uSHELL_IMPLEMENTS_USER_SHORTCUTS)*/
#endif /* (1 == uSHELL_IMPLEMENTS_COMMAND_HELP) */
#if (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)
    .piAutocompleteIndexArray = g_viAutocompleteIndexArray,
#endif /* (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE) */
#if (1 == uSHELL_IMPLEMENTS_SAVE_HISTORY)
    .pfileHistory             = NULL,
#endif /*(1 == uSHELL_IMPLEMENTS_SAVE_HISTORY)*/
#if (1 == uSHELL_IMPLEMENTS_SHELL_EXIT)
    .bKeepRuning              = true,
#endif /*(1 == uSHELL_IMPLEMENTS_SHELL_EXIT)*/
    .iNrFunctions             = uSHELL_NR_ELEMS(g_vsFuncDefArray),
    .iNrShortcuts             = uSHELL_NR_ELEMS(g_vsShortcutsArray),
    .pfExec                   = uShellExecuteCommand,
    .vstrPrompt               = {0},
    .iPromptLength            = 0
};

/******************************************************************************/


extern "C"
{
#if (1 == uSHELL_SUPPORTS_EXTERNAL_USER_DATA)
    EXPORTED uShellPluginInterface *uShellPluginEntry( void *pvUserData )
    {
        pvLocalUserData = pvUserData;
        return &sShellInstance;
    }
#else
    EXPORTED uShellPluginInterface *uShellPluginEntry( void )
    {
        return &sShellInstance;
    }
#endif /*(1 == uSHELL_SUPPORTS_EXTERNAL_USER_DATA)*/

    EXPORTED void uShellPluginExit( uShellPluginInterface *ptrPlugin )
    {
        (void)ptrPlugin;
    }
}

/******************************************************************************/
static int uShellExecuteCommand( const command_s *psCmd )
{
    /* void:v, (byte)u8:b:vb, (word)u16:w:vw, (int)u32:i:vi, (long)u64:l:vl, float:f:vf, string:s:vs, bool:o:vo */
    switch(g_vsFuncDefExArray[psCmd->iFctIndex].eParamType) {
        case v_type          :return g_vsFuncDefExArray[psCmd->iFctIndex].uFctType.v_fct          ();
        case i_type          :return g_vsFuncDefExArray[psCmd->iFctIndex].uFctType.i_fct          (psCmd->vi[0]);
        case s_type          :return g_vsFuncDefExArray[psCmd->iFctIndex].uFctType.s_fct          (psCmd->vs[0]);
        case ii_type         :return g_vsFuncDefExArray[psCmd->iFctIndex].uFctType.ii_fct         (psCmd->vi[0], psCmd->vi[1]);
        case ss_type         :return g_vsFuncDefExArray[psCmd->iFctIndex].uFctType.ss_fct         (psCmd->vs[0], psCmd->vs[1]);
        case is_type         :return g_vsFuncDefExArray[psCmd->iFctIndex].uFctType.is_fct         (psCmd->vi[0], psCmd->vs[0]);
        case lio_type        :return g_vsFuncDefExArray[psCmd->iFctIndex].uFctType.lio_fct        (psCmd->vl[0], psCmd->vi[0], psCmd->vo[0]);
        default              :return uSHELL_ERR_PARAMS_PATTERN_NOT_IMPLEM;
    }
} /* uShellExecuteCommand() */


