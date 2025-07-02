#pragma once

/* input / output interfaces specific to build environment */
#if defined(__GNUC__) && defined(__AVR__) /* Arduino AVR compiler */

    #include <stdarg.h>
    #include <miniprintf.h>
    char uart_getchar       (void);
    void uart_putchar       (char data);
    int  uart_printf        (const char *format, ...);
    #define uSHELL_PRINTF   uart_printf
    #define uSHELL_SNPRINTF mini_snprintf
    #define uSHELL_GETCH()  uart_getchar()
    #define uSHELL_PUTCH(x) uart_putchar(x)

#elif defined(__GNUC__) && defined(__linux__) /* linux console */

    #include <stdarg.h>
    #include <stdio.h>
    #define uSHELL_PRINTF   printf
    #define uSHELL_SNPRINTF snprintf
    #define uSHELL_VPRINTF  vprintf
    #define uSHELL_GETCH()  fgetc(stdin)
    #define uSHELL_PUTCH(x) putchar(x)

#elif (defined(__MINGW32__) || defined(_MSC_VER)) /* i.e MinGW or Microsoft VisualStudio for Windows console */

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
    #define uSHELL_SNPRINTF snprintf
    #define uSHELL_GETCH()  uart_getchar()
    #define uSHELL_PUTCH(x) uart_putchar(x)
#endif /*defined (SERIAL_TERMINAL) */

