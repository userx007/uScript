#ifndef USHELL_CORE_KEYS_H
#define USHELL_CORE_KEYS_H


/* common key codes */
#define uSHELL_KEY_TAB                       (0x09)
#define uSHELL_KEY_SPACE                     (0x20)
#define uSHELL_KEY_DOT                       (0x2E)
#define uSHELL_KEY_COLON                     (0x3A)
#define uSHELL_KEY_EQUAL                     (0x3D)
#define uSHELL_KEY_ESCAPE                    (0x1B)
#define uSHELL_KEY_CTRL_U                    (0x15)
#define uSHELL_KEY_CTRL_K                    (0x0B)
#define uSHELL_KEY_QUOTATION_MARK            '"'

/*key codes specific to the build environment */
#if (defined(__MINGW32__) || defined(_MSC_VER)) /* i.e MinGW or Microsoft VisualStudio for Windows console */
    #define uSHELL_KEY_ESCAPESEQ             (-32)
    #define uSHELL_KEY_ESCAPESEQ1            (0x00)
    #define uSHELL_KEY_ESCAPESEQ1_HOME       (0x47) /* 0xE047 */
    #define uSHELL_KEY_ESCAPESEQ1_END        (0x4F) /* 0xE04F */
    #define uSHELL_KEY_ESCAPESEQ1_INSERT     (0x52) /* 0xE052 */
    #define uSHELL_KEY_ESCAPESEQ1_DELETE     (0x53) /* 0xE053 */
    #define uSHELL_KEY_ESCAPESEQ1_PAGEUP     (0x49) /* 0xE049 */
    #define uSHELL_KEY_ESCAPESEQ1_PAGEDOWN   (0x51) /* 0xE051 */
    /* arrows */
    #define uSHELL_KEY_ESCAPESEQ_ARROW_UP    (0x48) /* 0xE048 */
    #define uSHELL_KEY_ESCAPESEQ_ARROW_DOWN  (0x50) /* 0xE050 */
    #define uSHELL_KEY_ESCAPESEQ_ARROW_RIGHT (0x4D) /* 0xE04D */
    #define uSHELL_KEY_ESCAPESEQ_ARROW_LEFT  (0x4B) /* 0xE04B */
#else /* all other compilers / terminals */
    #define uSHELL_KEY_ESCAPESEQ             (0x1B)
    #define uSHELL_KEY_INSERT                (0x16)
    #define uSHELL_KEY_LEFT_BRACKET          (0x5B) /* [ */
    #define uSHELL_KEY_TILDE                 (0x7E) /* ~ */
    #define uSHELL_KEY_DELETE                (0x7F)
    /* home, end, insert, delete */
    #define uSHELL_KEY_ESCAPESEQ_HOME        (0x48) /* 0x1B5B48     \033 [ H */
    #define uSHELL_KEY_ESCAPESEQ1_HOME       (0x31) /* 0x1B5B317E   \033 [ 1 ~ */
    #define uSHELL_KEY_ESCAPESEQ1_INSERT     (0x32) /* 0x1B5B327E   \033 [ 2 ~ */
    #define uSHELL_KEY_ESCAPESEQ1_DELETE     (0x33) /* 0x1B5B337E   \033 [ 3 ~ */
    #define uSHELL_KEY_ESCAPESEQ1_END        (0x34) /* 0x1B5B347E   \033 [ 4 ~ */
    #define uSHELL_KEY_ESCAPESEQ1_PAGEUP     (0x35) /* 0x1B5B357E   \033 [ 5 ~ */
    #define uSHELL_KEY_ESCAPESEQ1_PAGEDOWN   (0x36) /* 0x1B5B367E   \033 [ 6 ~ */
    /* arrows */
    #define uSHELL_KEY_ESCAPESEQ_ARROW_UP    (0x41) /* 0x1B5B41     \033 [ A */
    #define uSHELL_KEY_ESCAPESEQ_ARROW_DOWN  (0x42) /* 0x1B5B42     \033 [ B */
    #define uSHELL_KEY_ESCAPESEQ_ARROW_RIGHT (0x43) /* 0x1B5B43     \033 [ C */
    #define uSHELL_KEY_ESCAPESEQ_ARROW_LEFT  (0x44) /* 0x1B5B44     \033 [ D */
#endif

/* the ENTER key */
#if (defined(__MINGW32__) || defined(_MSC_VER)) || defined(SERIAL_TERMINAL) /* i.e MinGW or Microsoft VisualStudio for Windows console */
    #define uSHELL_KEY_ENTER                 (0x0D)
#else /* linux console, etc */
    #define uSHELL_KEY_ENTER                 (0x0A)
#endif /* the ENTER key */

/* the BACKSPACE key */
#if ( ((defined(__MINGW32__) || defined(_MSC_VER))) || (defined(SERIAL_TERMINAL) && !defined(__AVR__)) )
    #define uSHELL_KEY_BACKSPACE             (0x08)
#else /* linux console and serial terminal for Arduino */
    #define uSHELL_KEY_BACKSPACE             (0x7F)
#endif /* the BACKSPACE key */

#if defined(SERIAL_TERMINAL)
    #define uSHELL_KEY_ESCAPESEQ_END         (0x4B) /*0x1B5B4B   \033 [ K */
#else
    #define uSHELL_KEY_ESCAPESEQ_END         (0x46) /*0x1B5B46   \033 [ F */
#endif /* END key (as escape sequence) */

/* special settings if needed */
#if defined(MY_TERMINAL)
    #if defined(uSHELL_KEY_ENTER)
        #undef uSHELL_KEY_ENTER
        #define uSHELL_KEY_ENTER             (0x0D)
    #endif /*uSHELL_KEY_ENTER*/

    #if defined(uSHELL_KEY_BACKSPACE)
        #undef uSHELL_KEY_BACKSPACE
        #define uSHELL_KEY_BACKSPACE         (0x7F)
    #endif /*uSHELL_KEY_ENTER*/
#endif /* MY_TERMINAL */

#endif /* USHELL_CORE_KEYS_H */