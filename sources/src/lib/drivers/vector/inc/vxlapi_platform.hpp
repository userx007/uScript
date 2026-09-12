#ifndef U_VECTOR_VXLAPI_PLATFORM_H
#define U_VECTOR_VXLAPI_PLATFORM_H

/**
 * @brief Selects Vector's real Windows XL-API header or the unofficial Linux
 *        port's header (see vxlapi_linux.h's own header comment for what
 *        that is and how it was derived), plus whatever platform headers
 *        each one needs. Both declare the same type/macro/function names
 *        (XLportHandle, XLaccess, XLhandle, xlOpenPort(), ...) — see
 *        vxlapi_linux.h's "Changes vs. the original Windows vxlapi.h" list
 *        for the handful of underlying-type differences (XLhandle: Windows
 *        HANDLE vs. Linux eventfd int; XLlong/XLulong: kept 32-bit on both) —
 *        so every uVector*.hpp/.cpp file can #include this one header
 *        instead of repeating the #if defined(_WIN32) / #elif
 *        defined(__linux__) dance itself.
*/

#if defined(_WIN32)
#  include <windows.h>
#  include <vxlapi.h>
#elif defined(__linux__)
#  include <cstdint>
#  include <unistd.h>
#  include <vxlapi_linux.h>
#else
#  error "vxlapi_platform.hpp: Vector's XL Driver Library ships for Windows and Linux only."
#endif

#endif // U_VECTOR_VXLAPI_PLATFORM_H
