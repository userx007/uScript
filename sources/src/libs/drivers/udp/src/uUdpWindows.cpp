#include "uLogger.hpp"
#include "uUdp.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

// Straight Winsock port of uUdpPosix.cpp — see uTcpipWindows.cpp's header
// comment for the mechanical poll()->WSAPoll()/errno->WSAGetLastError()
// mapping this follows throughout.

/////////////////////////////////////////////////////////////////////////////////
//                            LOG DEFINITIONS                                  //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
#undef LT_HDR
#endif
#ifdef LOG_HDR
#undef LOG_HDR
#endif

#define LT_HDR  "UDP_DRV_WIN |"
#define LOG_HDR LOG_STRING(LT_HDR)

namespace {

// See uTcpipWindows.cpp's WinsockGuard — independent, self-contained
// init/teardown for this translation unit. WSAStartup()/WSACleanup()
// are reference-counted per-process by ws2_32.dll, so having uUdp and
// uTcpip each own a guard is safe even when both are linked into the
// same binary.
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

/**
 * @brief Split "host:port" or "[ipv6]:port" into separate host and port
 * strings. Identical to uUdpPosix.cpp's helper of the same name.
 */
bool split_host_port(std::string_view strInput, std::string &strHost, std::string &strPort)
{
    if (strInput.empty()) {
        return false;
    }

    if (strInput.front() == '[') {
        const size_t szCloseBracket = strInput.find(']');
        if (szCloseBracket == std::string_view::npos ||
            szCloseBracket + 1 >= strInput.size() ||
            strInput[szCloseBracket + 1] != ':') {
            return false;
        }
        strHost = std::string(strInput.substr(1, szCloseBracket - 1));
        strPort = std::string(strInput.substr(szCloseBracket + 2));
        return !strHost.empty() && !strPort.empty();
    }

    const size_t szColon = strInput.rfind(':');
    if (szColon == std::string_view::npos || szColon == 0 || szColon + 1 >= strInput.size()) {
        return false;
    }
    strHost = std::string(strInput.substr(0, szColon));
    strPort = std::string(strInput.substr(szColon + 1));
    return true;
}
} // namespace

bool UDP::resolve_numeric_host_port(std::string_view xtra_params,
                                    std::vector<uint8_t> &vAddrStorage) const
{
    std::string strHost;
    std::string strPort;
    if (!split_host_port(xtra_params, strHost, strPort)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("resolve_numeric_host_port: malformed \"host:port\""));
        return false;
    }

    struct addrinfo sHints   = {};
    sHints.ai_family         = AF_UNSPEC;
    sHints.ai_socktype       = SOCK_DGRAM;
    sHints.ai_protocol       = IPPROTO_UDP;
    sHints.ai_flags          = AI_NUMERICHOST | AI_NUMERICSERV;

    struct addrinfo *pResult = nullptr;
    const int iGaiRc         = ::getaddrinfo(strHost.c_str(), strPort.c_str(), &sHints, &pResult);
    if (iGaiRc != 0 || pResult == nullptr) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("resolve_numeric_host_port: getaddrinfo failed for ");
                  LOG_STRING(std::string(xtra_params).c_str()));
        return false;
    }

    vAddrStorage.assign(reinterpret_cast<const uint8_t *>(pResult->ai_addr),
                        reinterpret_cast<const uint8_t *>(pResult->ai_addr) + pResult->ai_addrlen);

    ::freeaddrinfo(pResult);
    return true;
}

// ============================================================================
// OPEN / CLOSE
// ============================================================================

UDP::Status UDP::open(const std::string &strHost, uint16_t u16Port, uint32_t /*u32ConnectTimeout*/)
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

    struct addrinfo sHints    = {};
    sHints.ai_family          = AF_UNSPEC;
    sHints.ai_socktype        = SOCK_DGRAM;
    sHints.ai_protocol        = IPPROTO_UDP;

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

        if (::connect(sock, pAi->ai_addr, static_cast<int>(pAi->ai_addrlen)) != 0) {
            const int err = ::WSAGetLastError();
            LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                      LOG_STRING("connect() failed, WSA error:"); LOG_INT(err));
            ::closesocket(sock);
            continue;
        }

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

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("UDP socket connected to "); LOG_STRING(strHost.c_str());
              LOG_STRING(":"); LOG_STRING(strPort.c_str());
              LOG_STRING(", handle:"); LOG_INT(m_iHandle));

    return Status::SUCCESS;
}

UDP::Status UDP::close()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_iHandle >= 0) {
        ::closesocket(static_cast<SOCKET>(m_iHandle));
        LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                  LOG_STRING("UDP socket closed, handle:"); LOG_INT(m_iHandle));
        m_iHandle = -1;
    }

    return Status::SUCCESS;
}

// ============================================================================
// INTERNAL READ PRIMITIVE
// ============================================================================

UDP::Status UDP::timeout_read(uint32_t u32ReadTimeout,
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
    }

    if (sPollFd.revents & POLLERR) {
        int iSockErr    = 0;
        int iSockErrLen = sizeof(iSockErr);
        ::getsockopt(static_cast<SOCKET>(m_iHandle), SOL_SOCKET, SO_ERROR,
                     reinterpret_cast<char *>(&iSockErr), &iSockErrLen);
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("timeout_read: socket error (e.g. ICMP unreachable):");
                  LOG_INT(iSockErr));
        return Status::READ_ERROR;
    }

    const int nbytes = ::recv(static_cast<SOCKET>(m_iHandle),
                              reinterpret_cast<char *>(buffer.data()),
                              static_cast<int>(buffer.size()), 0);
    if (nbytes < 0) {
        const int err = ::WSAGetLastError();
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("recv() failed, WSA error:"); LOG_INT(err));
        return Status::READ_ERROR;
    }

    // Unlike TCP, nbytes == 0 is not EOF here — it is a legitimate
    // zero-length UDP datagram.
    szBytesRead = static_cast<size_t>(nbytes);

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("RX bytes:"); LOG_UINT32(static_cast<uint32_t>(szBytesRead)));

    return Status::SUCCESS;
}

// ============================================================================
// INTERNAL WRITE PRIMITIVE
// ============================================================================

UDP::Status UDP::timeout_write(uint32_t u32WriteTimeout,
                               std::span<const uint8_t> buffer,
                               size_t &szBytesWritten,
                               const void *pDestAddr,
                               size_t szDestAddrLen,
                               std::stop_token stop_tok) const
{
    if (buffer.size() > UDP_MAX_DGRAM_LEN) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Invalid parameter: buffer exceeds UDP_MAX_DGRAM_LEN"));
        return Status::INVALID_PARAM;
    }

    szBytesWritten = 0;

    WSAPOLLFD sPollFd;
    sPollFd.fd                 = static_cast<SOCKET>(m_iHandle);
    sPollFd.events             = POLLOUT;
    sPollFd.revents            = 0;

    constexpr int kPollSliceMs = 200;
    const bool bInfinite       = (u32WriteTimeout == 0);
    const auto tDeadline       = std::chrono::steady_clock::now() + std::chrono::milliseconds(u32WriteTimeout);

    int iPollResult            = 0;
    while (true) {
        if (stop_tok.stop_requested()) {
            return Status::WRITE_TIMEOUT;
        }

        int iSliceMs = kPollSliceMs;
        if (!bInfinite) {
            const auto remaining = tDeadline - std::chrono::steady_clock::now();
            if (remaining <= std::chrono::milliseconds(0)) {
                return Status::WRITE_TIMEOUT;
            }
            iSliceMs = static_cast<int>(std::min<int64_t>(kPollSliceMs,
                                                          std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count()));
        }

        iPollResult = ::WSAPoll(&sPollFd, 1, iSliceMs);
        if (iPollResult < 0) {
            const int err = ::WSAGetLastError();
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("WSAPoll() failed"); LOG_INT(err));
            return Status::WRITE_ERROR;
        }
        if (iPollResult > 0) {
            break;
        }
    }

    if (sPollFd.revents & POLLERR) {
        int iSockErr    = 0;
        int iSockErrLen = sizeof(iSockErr);
        ::getsockopt(static_cast<SOCKET>(m_iHandle), SOL_SOCKET, SO_ERROR,
                     reinterpret_cast<char *>(&iSockErr), &iSockErrLen);
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("timeout_write: socket error (e.g. ICMP unreachable):");
                  LOG_INT(iSockErr));
        return Status::WRITE_ERROR;
    }

    const int nbytes = (pDestAddr == nullptr)
                           ? ::send(static_cast<SOCKET>(m_iHandle),
                                    reinterpret_cast<const char *>(buffer.data()),
                                    static_cast<int>(buffer.size()), 0)
                           : ::sendto(static_cast<SOCKET>(m_iHandle),
                                      reinterpret_cast<const char *>(buffer.data()),
                                      static_cast<int>(buffer.size()), 0,
                                      reinterpret_cast<const struct sockaddr *>(pDestAddr),
                                      static_cast<int>(szDestAddrLen));

    if (nbytes < 0 || static_cast<size_t>(nbytes) != buffer.size()) {
        const int err = ::WSAGetLastError();
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("send()/sendto() failed or partial, WSA error:"); LOG_INT(err));
        return Status::WRITE_ERROR;
    }

    szBytesWritten = buffer.size();

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("TX bytes:"); LOG_UINT32(static_cast<uint32_t>(szBytesWritten)));

    return Status::SUCCESS;
}
