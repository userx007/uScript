// tester_platform.h — thin POSIX / Winsock portability layer for the tester programs.
//
// Goals
//   * POSIX (Linux, macOS): every helper below compiles down to exactly the call the
//     testers used before (send/recv/close/errno/sigaction ...), so behaviour there is
//     unchanged.
//   * Windows (MSVC and MinGW-w64): Winsock2 equivalents.
//
// Differences this header hides
//   - <sys/socket.h>, <netdb.h>, <arpa/inet.h> ...   -> <winsock2.h>, <ws2tcpip.h>
//   - socket fd (int) vs SOCKET (UINT_PTR); close() vs closesocket()
//   - errno vs WSAGetLastError(); strerror() vs FormatMessage()
//   - char* option / buffer pointers and int lengths in the Winsock prototypes
//   - MSG_NOSIGNAL / SIGPIPE (do not exist on Windows)
//   - WSAStartup() requirement
//   - Ctrl+C: POSIX interrupts a blocking accept()/recv() with EINTR (sigaction without
//     SA_RESTART).  Windows does not, so tp_wait_readable() slices the wait with select()
//     and re-checks the stop flag.
//
// C++ only (uses inline functions / thread_local).

#ifndef TESTER_PLATFORM_H
#define TESTER_PLATFORM_H

#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
// winsock2.h must precede windows.h
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#if defined(_MSC_VER)
#include <BaseTsd.h>
using ssize_t = SSIZE_T;
#pragma comment(lib, "ws2_32.lib")
#endif

using tp_socket_t                          = SOCKET;
constexpr tp_socket_t TP_INVALID_SOCKET    = INVALID_SOCKET;
constexpr int TP_MSG_NOSIGNAL              = 0; // no SIGPIPE on Windows

#else // ------------------------------------------------------------ POSIX

#include <arpa/inet.h>
#include <cerrno>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

using tp_socket_t                       = int;
constexpr tp_socket_t TP_INVALID_SOCKET = -1;
#if defined(MSG_NOSIGNAL)
constexpr int TP_MSG_NOSIGNAL = MSG_NOSIGNAL;
#else
constexpr int TP_MSG_NOSIGNAL = 0; // e.g. macOS: SO_NOSIGPIPE / SIG_IGN instead
#endif

#endif // _WIN32

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

/** Winsock startup on Windows, no-op elsewhere. Returns true on success. */
inline bool tp_net_init()
{
#if defined(_WIN32)
    static bool s_bDone = false;
    static bool s_bOk   = false;
    if (!s_bDone) {
        WSADATA sWsa;
        s_bOk   = (::WSAStartup(MAKEWORD(2, 2), &sWsa) == 0);
        s_bDone = true;
    }
    return s_bOk;
#else
    return true;
#endif
}

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

/** errno on POSIX, WSAGetLastError() on Windows. */
inline int tp_last_error()
{
#if defined(_WIN32)
    return ::WSAGetLastError();
#else
    return errno;
#endif
}

/** True if the error means "interrupted, just retry". */
inline bool tp_is_eintr(int iErr)
{
#if defined(_WIN32)
    return iErr == WSAEINTR;
#else
    return iErr == EINTR;
#endif
}

/** Human readable text for an error code from tp_last_error(). Valid until the next call on this thread. */
inline const char *tp_errstr(int iErr)
{
#if defined(_WIN32)
    static thread_local char s_szBuf[256];
    const int iSavedWsaErr = ::WSAGetLastError(); // FormatMessage must not clobber the caller's error code
    DWORD dwLen            = ::FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                                   static_cast<DWORD>(iErr), 0, s_szBuf, sizeof(s_szBuf), nullptr);
    while (dwLen > 0 && (s_szBuf[dwLen - 1] == '\r' || s_szBuf[dwLen - 1] == '\n' || s_szBuf[dwLen - 1] == ' ')) {
        s_szBuf[--dwLen] = '\0';
    }
    if (dwLen == 0) {
        std::snprintf(s_szBuf, sizeof(s_szBuf), "error %d", iErr);
    }
    ::WSASetLastError(iSavedWsaErr);
    return s_szBuf;
#else
    return std::strerror(iErr);
#endif
}

/** perror() replacement that also works for Winsock errors. */
inline void tp_perror(const char *pszWhat)
{
    const int iErr = tp_last_error();
    std::fprintf(stderr, "%s: %s\n", pszWhat, tp_errstr(iErr));
}

// ---------------------------------------------------------------------------
// Socket primitives (typed wrappers around the Winsock-vs-POSIX prototypes)
// ---------------------------------------------------------------------------

inline void tp_close(tp_socket_t s)
{
#if defined(_WIN32)
    ::closesocket(s);
#else
    ::close(s);
#endif
}

inline int tp_setsockopt(tp_socket_t s, int iLevel, int iName, const void *pVal, socklen_t len)
{
    return ::setsockopt(s, iLevel, iName, static_cast<const char *>(pVal), len);
}

/** SO_REUSEADDR for *listening* TCP / UDP server sockets.
 *  POSIX: avoids "address already in use" after a restart.
 *  Windows: not needed for that, and there SO_REUSEADDR would allow a second process to hijack the port,
 *  so it is intentionally skipped. (Multicast receivers that want to share a port set it themselves.) */
inline void tp_set_reuse_listen(tp_socket_t s)
{
#if defined(_WIN32)
    (void)s;
#else
    int iReuse = 1;
    tp_setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &iReuse, sizeof(iReuse));
#endif
}

/** Windows only: by default an ICMP "port unreachable" (from an earlier sendto() to a peer that has gone away)
 *  makes the NEXT recvfrom() on that UDP socket fail with WSAECONNRESET. POSIX does not do that; this turns
 *  the Windows behaviour off so UDP servers keep serving. No-op elsewhere. */
inline void tp_udp_disable_connreset(tp_socket_t s)
{
#if defined(_WIN32)
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
    DWORD dwBytes    = 0;
    BOOL bNewBehavior = FALSE;
    ::WSAIoctl(s, SIO_UDP_CONNRESET, &bNewBehavior, sizeof(bNewBehavior), nullptr, 0, &dwBytes, nullptr, nullptr);
#else
    (void)s;
#endif
}

inline ssize_t tp_send(tp_socket_t s, const void *pBuf, size_t len, int iFlags)
{
#if defined(_WIN32)
    return ::send(s, static_cast<const char *>(pBuf), static_cast<int>(len), iFlags);
#else
    return ::send(s, pBuf, len, iFlags);
#endif
}

inline ssize_t tp_recv(tp_socket_t s, void *pBuf, size_t len, int iFlags)
{
#if defined(_WIN32)
    return ::recv(s, static_cast<char *>(pBuf), static_cast<int>(len), iFlags);
#else
    return ::recv(s, pBuf, len, iFlags);
#endif
}

inline ssize_t tp_sendto(tp_socket_t s, const void *pBuf, size_t len, int iFlags, const struct sockaddr *pTo,
                         socklen_t toLen)
{
#if defined(_WIN32)
    return ::sendto(s, static_cast<const char *>(pBuf), static_cast<int>(len), iFlags, pTo, toLen);
#else
    return ::sendto(s, pBuf, len, iFlags, pTo, toLen);
#endif
}

inline ssize_t tp_recvfrom(tp_socket_t s, void *pBuf, size_t len, int iFlags, struct sockaddr *pFrom,
                           socklen_t *pFromLen)
{
#if defined(_WIN32)
    int iFromLen = pFromLen ? static_cast<int>(*pFromLen) : 0;
    const int n  = ::recvfrom(s, static_cast<char *>(pBuf), static_cast<int>(len), iFlags, pFrom,
                              pFromLen ? &iFromLen : nullptr);
    if (pFromLen) {
        *pFromLen = static_cast<socklen_t>(iFromLen);
    }
    return n;
#else
    return ::recvfrom(s, pBuf, len, iFlags, pFrom, pFromLen);
#endif
}

// ---------------------------------------------------------------------------
// Ctrl+C / SIGTERM handling
// ---------------------------------------------------------------------------

/**
 * Install `pfnHandler` for SIGINT and SIGTERM.
 *  POSIX  : sigaction() WITHOUT SA_RESTART so blocking accept()/recv() return EINTR, and SIGPIPE ignored.
 *           (glibc's signal() would set SA_RESTART and Ctrl+C would appear to do nothing.)
 *  Windows: CRT signal() for SIGINT / SIGTERM / SIGBREAK (Ctrl+Break, console close).
 *           Blocking Winsock calls are NOT interrupted; pair with tp_wait_readable().
 */
inline void tp_install_stop_handler(void (*pfnHandler)(int))
{
#if defined(_WIN32)
    std::signal(SIGINT, pfnHandler);
    std::signal(SIGTERM, pfnHandler);
#if defined(SIGBREAK)
    std::signal(SIGBREAK, pfnHandler);
#endif
#else
    struct sigaction sSigAction = {};
    sSigAction.sa_handler       = pfnHandler;
    sSigAction.sa_flags         = 0; // no SA_RESTART
    ::sigemptyset(&sSigAction.sa_mask);
    ::sigaction(SIGINT, &sSigAction, nullptr);
    ::sigaction(SIGTERM, &sSigAction, nullptr);
    ::signal(SIGPIPE, SIG_IGN);
#endif
}

/**
 * Call right before a blocking accept()/recv()/recvfrom() on `s`.
 *  POSIX  : returns true immediately (the signal makes the real call return EINTR).
 *  Windows: waits (select, 200 ms slices) until `s` is readable or stopRequested() becomes true.
 *           Returns false iff stopRequested() was true; true otherwise (also on select errors, so the
 *           real call can report them).
 */
template <class StopPred>
inline bool tp_wait_readable_until(tp_socket_t s, StopPred stopRequested)
{
#if defined(_WIN32)
    while (!stopRequested()) {
        fd_set sSet;
        FD_ZERO(&sSet);
        FD_SET(s, &sSet);
        timeval sTv;
        sTv.tv_sec  = 0;
        sTv.tv_usec = 200 * 1000;
        const int iRc = ::select(0, &sSet, nullptr, nullptr, &sTv);
        if (iRc != 0) {
            return true; // readable, or error: let the real call deal with it
        }
    }
    return false;
#else
    (void)s;
    (void)stopRequested;
    return true;
#endif
}

/** Same, for a plain "volatile sig_atomic_t g_stop" flag. */
inline bool tp_wait_readable(tp_socket_t s, const volatile std::sig_atomic_t *pStop)
{
    return tp_wait_readable_until(s, [pStop] { return *pStop != 0; });
}

inline void tp_sleep_ms(unsigned uMs)
{
#if defined(_WIN32)
    ::Sleep(uMs);
#else
    ::usleep(static_cast<useconds_t>(uMs) * 1000u);
#endif
}

#endif // TESTER_PLATFORM_H
