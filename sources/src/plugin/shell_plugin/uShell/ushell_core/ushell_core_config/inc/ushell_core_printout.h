#ifndef USHELL_CORE_PRINTOUT_H
#define USHELL_CORE_PRINTOUT_H


#ifdef __cplusplus
extern "C" {
#endif

/* microcontrollers */
#if (defined(__GNUC__) && (defined(__AVR__) || defined(__xtensa__) || defined(__ARM_ARCH)))

    #include <stdarg.h>
    #include <miniprintf.h>
    char uart_getchar       (void);
    void uart_putchar       (char data);
    int  uart_printf        (const char *format, ...);
    #define uSHELL_PRINTF   uart_printf
    #define uSHELL_SNPRINTF mini_snprintf
    #define uSHELL_GETCH()  uart_getchar()
    #define uSHELL_PUTCH(x) uart_putchar(x)

/* linux PC terminal */
#elif (defined(__GNUC__) && defined(__linux__) && (defined(__x86_64__) || defined(__i386__)))

    #include <stdarg.h>
    #include <stdio.h>
    #define uSHELL_PRINTF   printf
    #define uSHELL_SNPRINTF snprintf
    #define uSHELL_VPRINTF  vprintf
    #define uSHELL_GETCH()  fgetc(stdin)
    #define uSHELL_PUTCH(x) putchar(x)

/* i.e MinGW or Microsoft VisualStudio for Windows terminal */
#elif (defined(__MINGW32__) || defined(_MSC_VER))

    #include <stdarg.h>
    #include <stdio.h>
    #include <conio.h>
    #define uSHELL_PRINTF    printf
    #define uSHELL_SNPRINTF  snprintf
    #define uSHELL_VPRINTF   vprintf
    #define uSHELL_GETCH()  _getch()
    #define uSHELL_PUTCH(x) _putch(x)

#else /* build environment not defined  */
    #error "Build variant not defined, please define it..."
#endif

/* if crosscompiled for microcontroller */
#if defined (SERIAL_TERMINAL)
    #undef  uSHELL_PRINTF
    #undef  uSHELL_GETCH
    #undef  uSHELL_PUTCH
    #define uSHELL_PRINTF   uart_printf
    #ifndef uSHELL_SNPRINTF
        #define uSHELL_SNPRINTF snprintf
    #endif
    #define uSHELL_GETCH()  uart_getchar()
    #define uSHELL_PUTCH(x) uart_putchar(x)
#endif /*defined (SERIAL_TERMINAL) */

#ifdef __cplusplus
}
#endif

#endif /* USHELL_CORE_PRINTOUT_H */
