#include "uTcpip.hpp"
#include "uLogger.hpp"

#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <chrono>


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR   "TCPIP_DRV   |"
#define LOG_HDR  LOG_STRING(LT_HDR)


// ============================================================================
// OPEN / CLOSE
// ============================================================================

TCPIP::Status TCPIP::open(const std::string& strHost, uint16_t u16Port, uint32_t u32ConnectTimeout)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (strHost.empty() || u16Port == 0)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Invalid parameter: empty host or port 0"));
        return Status::INVALID_PARAM;
    }

    const uint32_t u32Timeout = (u32ConnectTimeout == 0) ? TCPIP_CONNECT_DEFAULT_TIMEOUT : u32ConnectTimeout;

    // Resolve host (numeric IPv4/IPv6 literal or DNS name) to one or more
    // candidate addresses.
    struct addrinfo sHints = {};
    sHints.ai_family   = AF_UNSPEC;
    sHints.ai_socktype = SOCK_STREAM;
    sHints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* pResult = nullptr;
    const std::string strPort = std::to_string(u16Port);

    const int iGaiRc = ::getaddrinfo(strHost.c_str(), strPort.c_str(), &sHints, &pResult);
    if (iGaiRc != 0 || pResult == nullptr)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("getaddrinfo("); LOG_STRING(strHost.c_str());
                  LOG_STRING(") failed:"); LOG_STRING(::gai_strerror(iGaiRc)));
        return Status::INVALID_PARAM;
    }

    Status eResult = Status::PORT_ACCESS;

    // Try each candidate address in turn (e.g. a hostname that resolves to
    // both IPv6 and IPv4) until one connects, or the list is exhausted.
    for (struct addrinfo* pAi = pResult; pAi != nullptr; pAi = pAi->ai_next)
    {
        const int iSock = ::socket(pAi->ai_family, pAi->ai_socktype, pAi->ai_protocol);
        if (iSock < 0)
        {
            continue;
        }

        // Non-blocking connect(2) so we can bound the wait with poll(2)
        // instead of relying on the (very long) kernel TCP connect timeout.
        const int iFlags = ::fcntl(iSock, F_GETFL, 0);
        ::fcntl(iSock, F_SETFL, iFlags | O_NONBLOCK);

        const int iConnRc = ::connect(iSock, pAi->ai_addr, pAi->ai_addrlen);
        if (iConnRc == 0)
        {
            // Connected immediately (e.g. loopback). Restore blocking mode —
            // later recv()/send() calls are guarded by their own poll(2), so
            // a blocking socket is safe and simpler there.
            ::fcntl(iSock, F_SETFL, iFlags);
            m_iHandle = iSock;
            eResult   = Status::SUCCESS;
            break;
        }

        if (errno != EINPROGRESS)
        {
            const int err = errno;
            LOG_PRINT(LOG_DEBUG, LOG_HDR;
                      LOG_STRING("connect() failed immediately, errno:"); LOG_INT(err));
            ::close(iSock);
            continue;
        }

        struct pollfd sPollFd;
        sPollFd.fd      = iSock;
        sPollFd.events  = POLLOUT;
        sPollFd.revents = 0;

        const int iPollRc = ::poll(&sPollFd, 1, static_cast<int>(u32Timeout));
        if (iPollRc <= 0)
        {
            ::close(iSock);
            eResult = (iPollRc == 0) ? Status::WRITE_TIMEOUT : Status::PORT_ACCESS;
            continue;
        }

        // poll() returning writable does not by itself mean connect()
        // succeeded — check SO_ERROR to distinguish success from a
        // completed-but-failed connection (e.g. ECONNREFUSED).
        int       iSockErr    = 0;
        socklen_t szSockErrLen = sizeof(iSockErr);
        if (::getsockopt(iSock, SOL_SOCKET, SO_ERROR, &iSockErr, &szSockErrLen) < 0 || iSockErr != 0)
        {
            LOG_PRINT(LOG_DEBUG, LOG_HDR;
                      LOG_STRING("connect() completed with error:"); LOG_INT(iSockErr));
            ::close(iSock);
            eResult = Status::PORT_ACCESS;
            continue;
        }

        ::fcntl(iSock, F_SETFL, iFlags);
        m_iHandle = iSock;
        eResult   = Status::SUCCESS;
        break;
    }

    ::freeaddrinfo(pResult);

    if (eResult != Status::SUCCESS)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Failed to connect to "); LOG_STRING(strHost.c_str());
                  LOG_STRING(":"); LOG_STRING(strPort.c_str()));
        return eResult;
    }

    // Disable Nagle's algorithm. This driver is used for request/response
    // style exchanges (mirroring how the UART/I2C/SPI/CAN drivers are used),
    // so batching small writes to wait for an ACK only adds latency.
    int iNoDelay = 1;
    if (::setsockopt(m_iHandle, IPPROTO_TCP, TCP_NODELAY, &iNoDelay, sizeof(iNoDelay)) < 0)
    {
        // Not fatal — proceed without it.
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("TCP_NODELAY not supported, ignoring"));
    }

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("Connected to "); LOG_STRING(strHost.c_str());
              LOG_STRING(":"); LOG_STRING(strPort.c_str());
              LOG_STRING(", handle:"); LOG_INT(m_iHandle));

    return Status::SUCCESS;
}


TCPIP::Status TCPIP::close()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_iHandle >= 0)
    {
        ::close(m_iHandle);
        LOG_PRINT(LOG_DEBUG, LOG_HDR;
                  LOG_STRING("Socket closed, handle:"); LOG_INT(m_iHandle));
        m_iHandle = -1;
    }

    return Status::SUCCESS;
}


// ============================================================================
// INTERNAL READ PRIMITIVE
// One poll(2) + recv(2) pair. bytes_read is set to the number of bytes
// actually received (which may be less than buffer.size()).
// ============================================================================

TCPIP::Status TCPIP::timeout_read(uint32_t u32ReadTimeout,
                          std::span<uint8_t> buffer,
                          size_t& szBytesRead) const
{
    if (buffer.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("timeout_read: invalid parameter"));
        return Status::INVALID_PARAM;
    }

    szBytesRead = 0;

    struct pollfd sPollFd;
    sPollFd.fd      = m_iHandle;
    sPollFd.events  = POLLIN;
    sPollFd.revents = 0;

    // 0 == infinite timeout: block until data is available (poll(2) treats
    // a negative timeout as "wait indefinitely").
    const int iPollTimeout = (u32ReadTimeout == 0) ? -1 : static_cast<int>(u32ReadTimeout);

    const int iPollResult = ::poll(&sPollFd, 1, iPollTimeout);
    if (iPollResult < 0)
    {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("poll() failed"); LOG_INT(err));
        return Status::READ_ERROR;
    }
    else if (iPollResult == 0)
    {
        return Status::READ_TIMEOUT;
    }

    if (sPollFd.revents & (POLLERR | POLLHUP))
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("timeout_read: peer closed or reset the connection"));
        return Status::READ_ERROR;
    }

    const ssize_t nbytes = ::recv(m_iHandle, buffer.data(), buffer.size(), 0);
    if (nbytes < 0)
    {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("recv() failed, errno:"); LOG_INT(err));
        return Status::READ_ERROR;
    }
    else if (nbytes == 0)
    {
        // Orderly shutdown by the peer (recv() returning 0 on a stream
        // socket means EOF, not "no data available").
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("timeout_read: peer closed the connection"));
        return Status::READ_ERROR;
    }

    szBytesRead = static_cast<size_t>(nbytes);

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("RX bytes:"); LOG_UINT32(static_cast<uint32_t>(szBytesRead)));

    return Status::SUCCESS;
}


// ============================================================================
// INTERNAL WRITE PRIMITIVE
// Loops over send(2), re-polling for POLLOUT between short writes, until the
// whole buffer has gone out or the overall write timeout elapses.
// ============================================================================

TCPIP::Status TCPIP::timeout_write(uint32_t u32WriteTimeout,
                           std::span<const uint8_t> buffer,
                           size_t& szBytesWritten) const
{
    if (buffer.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("timeout_write: invalid parameter"));
        return Status::INVALID_PARAM;
    }

    szBytesWritten = 0;

    // 0 == infinite timeout: never time out the overall write, and block
    // indefinitely (poll(2) timeout -1) on each POLLOUT wait.
    const bool bInfinite = (u32WriteTimeout == 0);
    const auto tDeadline = std::chrono::steady_clock::now() +
                           std::chrono::milliseconds(u32WriteTimeout);

    while (szBytesWritten < buffer.size())
    {
        const auto tNow = std::chrono::steady_clock::now();
        if (!bInfinite && tNow >= tDeadline)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("timeout_write: overall timeout elapsed, bytes sent:");
                      LOG_UINT32(static_cast<uint32_t>(szBytesWritten)));
            return Status::WRITE_TIMEOUT;
        }

        int iPollTimeout = -1;
        if (!bInfinite)
        {
            const auto remainingMs =
                std::chrono::duration_cast<std::chrono::milliseconds>(tDeadline - tNow).count();
            iPollTimeout = static_cast<int>(remainingMs);
        }

        struct pollfd sPollFd;
        sPollFd.fd      = m_iHandle;
        sPollFd.events  = POLLOUT;
        sPollFd.revents = 0;

        const int iPollResult = ::poll(&sPollFd, 1, iPollTimeout);
        if (iPollResult < 0)
        {
            const int err = errno;
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("poll() failed"); LOG_INT(err));
            return Status::WRITE_ERROR;
        }
        else if (iPollResult == 0)
        {
            return Status::WRITE_TIMEOUT;
        }

        if (sPollFd.revents & (POLLERR | POLLHUP))
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("timeout_write: peer closed or reset the connection"));
            return Status::WRITE_ERROR;
        }

        // MSG_NOSIGNAL: don't raise SIGPIPE if the peer has already closed
        // its end — we handle the error via the return value instead.
        const ssize_t nbytes = ::send(m_iHandle,
                                      buffer.data() + szBytesWritten,
                                      buffer.size() - szBytesWritten,
                                      MSG_NOSIGNAL);
        if (nbytes < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                continue; // Spurious wakeup — re-poll for POLLOUT.
            }
            const int err = errno;
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("send() failed, errno:"); LOG_INT(err));
            return Status::WRITE_ERROR;
        }

        szBytesWritten += static_cast<size_t>(nbytes);
    }

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("TX bytes:"); LOG_UINT32(static_cast<uint32_t>(szBytesWritten)));

    return Status::SUCCESS;
}
