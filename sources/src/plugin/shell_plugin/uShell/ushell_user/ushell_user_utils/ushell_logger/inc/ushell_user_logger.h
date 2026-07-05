#ifndef USHELL_LOGGER_H
#define USHELL_LOGGER_H

#include "ushell_core_settings.h"
#include "ushell_core_printout.h"

/* define logging levels */
typedef enum
{
    ULOG_VERBOSE,
    ULOG_DEBUG,
    ULOG_INFO,
    ULOG_WARNING,
    ULOG_ERROR

} uLogLevel;

/* Logging function */
static inline void uSHELL_LOG( uLogLevel level, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    /* Determine severity string */
    char severity;
    switch (level)
    {
        case ULOG_ERROR   : severity = 'E'; break;
        case ULOG_WARNING : severity = 'W'; break;
        case ULOG_INFO    : severity = 'I'; break;
        case ULOG_DEBUG   : severity = 'D'; break;
        case ULOG_VERBOSE : severity = 'V'; break;
        default          : severity = '?'; break;
    }

#if (1 == uSHELL_SUPPORTS_COLORS)

    const char *color;
    switch (level)
    {
        case ULOG_ERROR   :color = uSHELL_ERROR_COLOR;   break;
        case ULOG_WARNING :color = uSHELL_WARNING_COLOR; break;
        case ULOG_INFO    :color = uSHELL_INFO_COLOR;    break;
        case ULOG_DEBUG   :color = uSHELL_DEBUG_COLOR;   break;
        case ULOG_VERBOSE :color = uSHELL_VERBOSE_COLOR; break;
        default          :color = uSHELL_RESET_COLOR;   break;
    }

    uSHELL_PRINTF("%s[%c] ", color, severity);

#else  /* (0 == uSHELL_SUPPORTS_COLORS) */
    uSHELL_PRINTF("[ %c ] ", severity);

#endif /* (1 == uSHELL_SUPPORTS_COLORS) */

    uSHELL_VPRINTF(format, args);
    uSHELL_PRINTF("%s\n", uSHELL_RESET_COLOR);
    va_end(args);
}

#endif // USHELL_LOGGER_H
