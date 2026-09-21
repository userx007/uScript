#include "uLogger.hpp"
#include "uTcpip.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

// Winsock2.h must be included before windows.h to avoid the winsock.h /
// winsock2.h header-ordering clash.
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

// ─────────────────────────────────────────────────────────────────────────────
// This is a straight Winsock port of uTcpipPosix.cpp: same non-blocking
// connect()+poll()-for-writable pattern, same poll-in-bounded-slices read/
// write loops. The differences are all mechanical:
//   poll()        -> WSAPoll()          (signature-compatible, same POLLIN/
//                                         POLLOUT/POLLERR/POLLHUP flags)
//   close()       -> closesocket()
//   fcntl(O_NONBLOCK) -> ioctlsocket(FIONBIO)
//   errno/EINPROGRESS/EAGAIN/EWOULDBLOCK -> WSAGetLastError()/WSAEWOULDBLOCK
//   MSG_NOSIGNAL  -> omitted (Winsock sockets never raise SIGPIPE)
//
// m_iHandle stays `int` (declared once, shared with the POSIX build) rather
// than SOCKET (UINT_PTR): in practice Winsock hands out small handle values
// that fit in an int, and keeping the member type unchanged avoids an
// ICommDriver-wide #ifdef just for this one field. INVALID_SOCKET is mapped
// to -1, matching the POSIX "fd < 0 means closed" convention used throughout
// this class and by nativeHandle()'s callers (e.g. MqttDriver's
// SSL_set_fd(), which itself expects a plain int on every platform).
// ─────────────────────────────────────────────────────────────────────────────

/////////////////////////////////////////////////////////////////////////////////
//                            LOG DEFINITIONS                                  //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
#undef LT_HDR
#endif
#ifdef LOG_HDR
#undef LOG_HDR
#endif

#define LT_HDR  "TCPIP_DRV   |"
#define LOG_HDR LOG_STRING(LT_HDR)

namespace {

// Process-wide Winsock init/teardown. Constructed the first time this
// translation unit is touched (i.e. before any TCPIP::open() can run,
// since static init of function-local statics is thread-safe and
// happens-before their first use) and torn down at process exit.
class WinsockGuard
{
public:
    WinsockGuard()
    {
        WSADATA wsaData;
        m_bOk = (::WSAStartup(MAKEWORD(2, 2), &wsaData) == 0);
        if (!m_bOk) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("WSAStartup() failed"));
        }
    }

    ~WinsockGuard()
    {
        if (m_bOk) {
            ::WSACleanup();
        }
    }

    bool ok() const
    {
        return m_bOk;
    }

private:
    bool m_bOk = false;
};

WinsockGuard &winsock()
{
    static WinsockGuard sInstance;
    return sInstance;
}

} // namespace

// ============================================================================
// OPEN / CLOSE
// ============================================================================

TCPIP::Status TCPIP::open(const std::string &strHost, uint16_t u16Port, uint32_t u32ConnectTimeout)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!winsock().ok()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Winsock not initialised"));
        return Status::PORT_ACCESS;
    }

    if (strHost.empty() || u16Port == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Invalid parameter: empty host or port 0"));
        return Status::INVALID_PARAM;
    }

    const uint32_t u32Timeout = (u32ConnectTimeout == 0) ? TCPIP_CONNECT_DEFAULT_TIMEOUT : u32ConnectTimeout;

    struct addrinfo sHints    = {};
    sHints.ai_family          = AF_UNSPEC;
    sHints.ai_socktype        = SOCK_STREAM;
    sHints.ai_protocol        = IPPROTO_TCP;

    struct addrinfo *pResult  = nullptr;
    const std::string strPort = std::to_string(u16Port);

    const int iGaiRc          = ::getaddrinfo(strHost.c_str(), strPort.c_str(), &sHints, &pResult);
    if (iGaiRc != 0 || pResult == nullptr) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("getaddrinfo("); LOG_STRING(strHost.c_str());
                  LOG_STRING(") failed:"); LOG_INT(iGaiRc));
        return Status::INVALID_PARAM;
    }

    Status eResult = Status::PORT_ACCESS;

    for (struct addrinfo *pAi = pResult; pAi != nullptr; pAi = pAi->ai_next) {
        const SOCKET sock = ::socket(pAi->ai_family, pAi->ai_socktype, pAi->ai_protocol);
        if (sock == INVALID_SOCKET) {
            continue;
        }

        // Non-blocking connect() so we can bound the wait with WSAPoll()
        // instead of the (very long) default TCP connect timeout.
        u_long ulNonBlocking = 1;
        ::ioctlsocket(sock, FIONBIO, &ulNonBlocking);

        const int iConnRc = ::connect(sock, pAi->ai_addr, static_cast<int>(pAi->ai_addrlen));
        if (iConnRc == 0) {
            // Connected immediately (e.g. loopback). Restore blocking mode —
            // later recv()/send() calls are guarded by their own WSAPoll(),
            // so a blocking socket is safe and simpler there.
            u_long ulBlocking = 0;
            ::ioctlsocket(sock, FIONBIO, &ulBlocking);
            m_iHandle = static_cast<int>(sock);
            eResult   = Status::SUCCESS;
            break;
        }

        const int iErr = ::WSAGetLastError();
        if (iErr != WSAEWOULDBLOCK) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                      LOG_STRING("connect() failed immediately, WSA error:"); LOG_INT(iErr));
            ::closesocket(sock);
            continue;
        }

        WSAPOLLFD sPollFd;
        sPollFd.fd        = sock;
        sPollFd.events    = POLLOUT;
        sPollFd.revents   = 0;

        const int iPollRc = ::WSAPoll(&sPollFd, 1, static_cast<int>(u32Timeout));
        if (iPollRc <= 0) {
            ::closesocket(sock);
            eResult = (iPollRc == 0) ? Status::WRITE_TIMEOUT : Status::PORT_ACCESS;
            continue;
        }

        // WSAPoll() returning writable does not by itself mean connect()
        // succeeded — check SO_ERROR to distinguish success from a
        // completed-but-failed connection (e.g. WSAECONNREFUSED).
        int iSockErr    = 0;
        int iSockErrLen = sizeof(iSockErr);
        if (::getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&iSockErr), &iSockErrLen) != 0 || iSockErr != 0) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                      LOG_STRING("connect() completed with error:"); LOG_INT(iSockErr));
            ::closesocket(sock);
            eResult = Status::PORT_ACCESS;
            continue;
        }

        u_long ulBlocking = 0;
        ::ioctlsocket(sock, FIONBIO, &ulBlocking);
        m_iHandle = static_cast<int>(sock);
        eResult   = Status::SUCCESS;
        break;
    }

    ::freeaddrinfo(pResult);

    if (eResult != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Failed to connect to "); LOG_STRING(strHost.c_str());
                  LOG_STRING(":"); LOG_STRING(strPort.c_str()));
        return eResult;
    }

    // Disable Nagle's algorithm — see uTcpipPosix.cpp's identical rationale.
    BOOL bNoDelay = TRUE;
    if (::setsockopt(static_cast<SOCKET>(m_iHandle), IPPROTO_TCP, TCP_NODELAY,
                     reinterpret_cast<const char *>(&bNoDelay), sizeof(bNoDelay)) != 0) {
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("TCP_NODELAY not supported, ignoring"));
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("Connected to "); LOG_STRING(strHost.c_str());
              LOG_STRING(":"); LOG_STRING(strPort.c_str());
              LOG_STRING(", handle:"); LOG_INT(m_iHandle));

    return Status::SUCCESS;
}

TCPIP::Status TCPIP::close()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_iHandle >= 0) {
        ::closesocket(static_cast<SOCKET>(m_iHandle));
        LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                  LOG_STRING("Socket closed, handle:"); LOG_INT(m_iHandle));
        m_iHandle = -1;
    }

    return Status::SUCCESS;
}

// ============================================================================
// INTERNAL READ PRIMITIVE
// ============================================================================

TCPIP::Status TCPIP::timeout_read(uint32_t u32ReadTimeout,
                                  std::span<uint8_t> buffer,
                                  size_t &szBytesRead,
                                  std::stop_token stop_tok) const
{
    if (buffer.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("timeout_read: invalid parameter"));
        return Status::INVALID_PARAM;
    }

    szBytesRead = 0;

    WSAPOLLFD sPollFd;
    sPollFd.fd                 = static_cast<SOCKET>(m_iHandle);
    sPollFd.events             = POLLIN;
    sPollFd.revents            = 0;

    constexpr int kPollSliceMs = 200;
    const bool bInfinite       = (u32ReadTimeout == 0);
    const auto tDeadline       = std::chrono::steady_clock::now() + std::chrono::milliseconds(u32ReadTimeout);

    int iPollResult            = 0;
    while (true) {
        if (stop_tok.stop_requested()) {
            return Status::READ_TIMEOUT;
        }

        int iSliceMs = kPollSliceMs;
        if (!bInfinite) {
            const auto remaining = tDeadline - std::chrono::steady_clock::now();
            if (remaining <= std::chrono::milliseconds(0)) {
                return Status::READ_TIMEOUT;
            }
            iSliceMs = static_cast<int>(std::min<int64_t>(kPollSliceMs,
                                                          std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count()));
        }

        iPollResult = ::WSAPoll(&sPollFd, 1, iSliceMs);
        if (iPollResult < 0) {
            const int err = ::WSAGetLastError();
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("WSAPoll() failed"); LOG_INT(err));
            return Status::READ_ERROR;
        }
        if (iPollResult > 0) {
            break;
        }
        // iPollResult == 0: this slice timed out, loop again.
    }

    if (sPollFd.revents & (POLLERR | POLLHUP)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("timeout_read: peer closed or reset the connection"));
        return Status::READ_ERROR;
    }

    const int nbytes = ::recv(static_cast<SOCKET>(m_iHandle),
                              reinterpret_cast<char *>(buffer.data()),
                              static_cast<int>(buffer.size()), 0);
    if (nbytes < 0) {
        const int err = ::WSAGetLastError();
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("recv() failed, WSA error:"); LOG_INT(err));
        return Status::READ_ERROR;
    } else if (nbytes == 0) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("timeout_read: peer closed the connection"));
        return Status::READ_ERROR;
    }

    szBytesRead = static_cast<size_t>(nbytes);

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("RX bytes:"); LOG_UINT32(static_cast<uint32_t>(szBytesRead)));

    return Status::SUCCESS;
}

// ============================================================================
// INTERNAL WRITE PRIMITIVE
// ============================================================================

TCPIP::Status TCPIP::timeout_write(uint32_t u32WriteTimeout,
                                   std::span<const uint8_t> buffer,
                                   size_t &szBytesWritten,
                                   std::stop_token stop_tok) const
{
    if (buffer.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("timeout_write: invalid parameter"));
        return Status::INVALID_PARAM;
    }

    szBytesWritten             = 0;

    constexpr int kPollSliceMs = 200;
    const bool bInfinite       = (u32WriteTimeout == 0);
    const auto tDeadline       = std::chrono::steady_clock::now() +
                                 std::chrono::milliseconds(u32WriteTimeout);

    while (szBytesWritten < buffer.size()) {
        if (stop_tok.stop_requested()) {
            return Status::WRITE_TIMEOUT;
        }

        const auto tNow = std::chrono::steady_clock::now();
        if (!bInfinite && tNow >= tDeadline) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("timeout_write: overall timeout elapsed, bytes sent:");
                      LOG_UINT32(static_cast<uint32_t>(szBytesWritten)));
            return Status::WRITE_TIMEOUT;
        }

        int iPollTimeout = -1;
        if (!bInfinite) {
            const auto remainingMs =
                std::chrono::duration_cast<std::chrono::milliseconds>(tDeadline - tNow).count();
            iPollTimeout = static_cast<int>(remainingMs);
        }

        WSAPOLLFD sPollFd;
        sPollFd.fd            = static_cast<SOCKET>(m_iHandle);
        sPollFd.events        = POLLOUT;
        sPollFd.revents       = 0;

        const int iPollResult = ::WSAPoll(&sPollFd, 1, iPollTimeout);
        if (iPollResult < 0) {
            const int err = ::WSAGetLastError();
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("WSAPoll() failed"); LOG_INT(err));
            return Status::WRITE_ERROR;
        } else if (iPollResult == 0) {
            return Status::WRITE_TIMEOUT;
        }

        if (sPollFd.revents & (POLLERR | POLLHUP)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("timeout_write: peer closed or reset the connection"));
            return Status::WRITE_ERROR;
        }

        // No MSG_NOSIGNAL equivalent needed: Winsock sockets never raise SIGPIPE.
        const int nbytes = ::send(static_cast<SOCKET>(m_iHandle),
                                  reinterpret_cast<const char *>(buffer.data() + szBytesWritten),
                                  static_cast<int>(buffer.size() - szBytesWritten), 0);
        if (nbytes < 0) {
            const int err = ::WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                continue; // Spurious wakeup — re-poll for POLLOUT.
            }
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("send() failed, WSA error:"); LOG_INT(err));
            return Status::WRITE_ERROR;
        }

        szBytesWritten += static_cast<size_t>(nbytes);
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("TX bytes:"); LOG_UINT32(static_cast<uint32_t>(szBytesWritten)));

    return Status::SUCCESS;
}
