#include "uUdp.hpp"
#include "uLogger.hpp"

#include <cstring>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR   "UDP_DRV_PSX |"
#define LOG_HDR  LOG_STRING(LT_HDR)


// ============================================================================
// LOCAL HELPERS
// ============================================================================

namespace
{
    /**
     * @brief Split "host:port" or "[ipv6]:port" into separate host and port
     * strings. Does not validate either half — resolve_numeric_host_port()
     * does that via getaddrinfo(AI_NUMERICHOST | AI_NUMERICSERV).
     */
    bool split_host_port(std::string_view strInput, std::string& strHost, std::string& strPort)
    {
        if (strInput.empty())
        {
            return false;
        }

        if (strInput.front() == '[')
        {
            // Bracketed IPv6 literal: "[addr]:port"
            const size_t szCloseBracket = strInput.find(']');
            if (szCloseBracket == std::string_view::npos ||
                szCloseBracket + 1 >= strInput.size() ||
                strInput[szCloseBracket + 1] != ':')
            {
                return false;
            }
            strHost = std::string(strInput.substr(1, szCloseBracket - 1));
            strPort = std::string(strInput.substr(szCloseBracket + 2));
            return !strHost.empty() && !strPort.empty();
        }

        // "host:port" — split on the last ':' (host itself, being numeric
        // IPv4 only in the unbracketed form, cannot contain one).
        const size_t szColon = strInput.rfind(':');
        if (szColon == std::string_view::npos || szColon == 0 || szColon + 1 >= strInput.size())
        {
            return false;
        }
        strHost = std::string(strInput.substr(0, szColon));
        strPort = std::string(strInput.substr(szColon + 1));
        return true;
    }
}


bool UDP::resolve_numeric_host_port(std::string_view xtra_params,
                                    std::vector<uint8_t>& vAddrStorage) const
{
    std::string strHost;
    std::string strPort;
    if (!split_host_port(xtra_params, strHost, strPort))
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("resolve_numeric_host_port: malformed \"host:port\""));
        return false;
    }

    struct addrinfo sHints = {};
    sHints.ai_family   = AF_UNSPEC;
    sHints.ai_socktype = SOCK_DGRAM;
    sHints.ai_protocol = IPPROTO_UDP;
    // AI_NUMERICHOST | AI_NUMERICSERV: pure parse-and-validate, no DNS
    // lookup — keeps a per-call tout_write() override fast and non-blocking.
    sHints.ai_flags    = AI_NUMERICHOST | AI_NUMERICSERV;

    struct addrinfo* pResult = nullptr;
    const int iGaiRc = ::getaddrinfo(strHost.c_str(), strPort.c_str(), &sHints, &pResult);
    if (iGaiRc != 0 || pResult == nullptr)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("resolve_numeric_host_port: getaddrinfo failed for ");
                  LOG_STRING(std::string(xtra_params).c_str()));
        return false;
    }

    vAddrStorage.assign(reinterpret_cast<const uint8_t*>(pResult->ai_addr),
                        reinterpret_cast<const uint8_t*>(pResult->ai_addr) + pResult->ai_addrlen);

    ::freeaddrinfo(pResult);
    return true;
}


// ============================================================================
// OPEN / CLOSE
// ============================================================================

UDP::Status UDP::open(const std::string& strHost, uint16_t u16Port, uint32_t /*u32ConnectTimeout*/)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (strHost.empty() || u16Port == 0)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Invalid parameter: empty host or port 0"));
        return Status::INVALID_PARAM;
    }

    // Unlike the TCP driver, connect() on a UDP socket does not perform a
    // handshake — it only records a default peer in the kernel — so there is
    // no blocking step here to bound with poll(2)/a connect timeout.
    struct addrinfo sHints = {};
    sHints.ai_family   = AF_UNSPEC;
    sHints.ai_socktype = SOCK_DGRAM;
    sHints.ai_protocol = IPPROTO_UDP;

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

    for (struct addrinfo* pAi = pResult; pAi != nullptr; pAi = pAi->ai_next)
    {
        const int iSock = ::socket(pAi->ai_family, pAi->ai_socktype, pAi->ai_protocol);
        if (iSock < 0)
        {
            continue;
        }

        if (::connect(iSock, pAi->ai_addr, pAi->ai_addrlen) < 0)
        {
            const int err = errno;
            LOG_PRINT(LOG_DEBUG, LOG_HDR;
                      LOG_STRING("connect() failed, errno:"); LOG_INT(err));
            ::close(iSock);
            continue;
        }

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

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("UDP socket connected to "); LOG_STRING(strHost.c_str());
              LOG_STRING(":"); LOG_STRING(strPort.c_str());
              LOG_STRING(", handle:"); LOG_INT(m_iHandle));

    return Status::SUCCESS;
}


UDP::Status UDP::close()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_iHandle >= 0)
    {
        ::close(m_iHandle);
        LOG_PRINT(LOG_DEBUG, LOG_HDR;
                  LOG_STRING("UDP socket closed, handle:"); LOG_INT(m_iHandle));
        m_iHandle = -1;
    }

    return Status::SUCCESS;
}


// ============================================================================
// INTERNAL READ PRIMITIVE
// Receives one UDP datagram; copies its payload into buffer. bytes_read is
// set to the number of bytes actually copied (silently truncated by the
// kernel, per normal UDP semantics, if buffer is smaller than the datagram).
// ============================================================================

UDP::Status UDP::timeout_read(uint32_t u32ReadTimeout,
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

    const int iPollResult = ::poll(&sPollFd, 1, static_cast<int>(u32ReadTimeout));
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

    if (sPollFd.revents & POLLERR)
    {
        // Most commonly an asynchronous ICMP "port unreachable" delivered
        // because this socket is connect()ed to a specific peer.
        int       iSockErr    = 0;
        socklen_t szSockErrLen = sizeof(iSockErr);
        ::getsockopt(m_iHandle, SOL_SOCKET, SO_ERROR, &iSockErr, &szSockErrLen);
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("timeout_read: socket error (e.g. ICMP unreachable):");
                  LOG_INT(iSockErr));
        return Status::READ_ERROR;
    }

    const ssize_t nbytes = ::recv(m_iHandle, buffer.data(), buffer.size(), 0);
    if (nbytes < 0)
    {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("recv() failed, errno:"); LOG_INT(err));
        return Status::READ_ERROR;
    }

    // Unlike TCP, nbytes == 0 is not EOF here — it is a legitimate
    // zero-length UDP datagram.
    szBytesRead = static_cast<size_t>(nbytes);

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("RX bytes:"); LOG_UINT32(static_cast<uint32_t>(szBytesRead)));

    return Status::SUCCESS;
}


// ============================================================================
// INTERNAL WRITE PRIMITIVE
// Sends buffer as a single UDP datagram. A UDP send() either accepts the
// whole datagram or fails outright — there is no short-write/retry loop the
// way the TCP driver needs one.
// ============================================================================

UDP::Status UDP::timeout_write(uint32_t u32WriteTimeout,
                           std::span<const uint8_t> buffer,
                           size_t& szBytesWritten,
                           const void* pDestAddr,
                           size_t szDestAddrLen) const
{
    if (buffer.size() > UDP_MAX_DGRAM_LEN)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Invalid parameter: buffer exceeds UDP_MAX_DGRAM_LEN"));
        return Status::INVALID_PARAM;
    }

    szBytesWritten = 0;

    struct pollfd sPollFd;
    sPollFd.fd      = m_iHandle;
    sPollFd.events  = POLLOUT;
    sPollFd.revents = 0;

    const int iPollResult = ::poll(&sPollFd, 1, static_cast<int>(u32WriteTimeout));
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

    if (sPollFd.revents & POLLERR)
    {
        int       iSockErr    = 0;
        socklen_t szSockErrLen = sizeof(iSockErr);
        ::getsockopt(m_iHandle, SOL_SOCKET, SO_ERROR, &iSockErr, &szSockErrLen);
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("timeout_write: socket error (e.g. ICMP unreachable):");
                  LOG_INT(iSockErr));
        return Status::WRITE_ERROR;
    }

    const ssize_t nbytes = (pDestAddr == nullptr)
        ? ::send(m_iHandle, buffer.data(), buffer.size(), MSG_NOSIGNAL)
        : ::sendto(m_iHandle, buffer.data(), buffer.size(), MSG_NOSIGNAL,
                   reinterpret_cast<const struct sockaddr*>(pDestAddr),
                   static_cast<socklen_t>(szDestAddrLen));

    if (nbytes < 0 || static_cast<size_t>(nbytes) != buffer.size())
    {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("send()/sendto() failed or partial, errno:"); LOG_INT(err));
        return Status::WRITE_ERROR;
    }

    szBytesWritten = buffer.size();

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("TX bytes:"); LOG_UINT32(static_cast<uint32_t>(szBytesWritten)));

    return Status::SUCCESS;
}
