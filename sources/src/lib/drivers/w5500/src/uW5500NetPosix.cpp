#include "uW5500Net.hpp"
#include "uLogger.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef LT_HDR
    #undef LT_HDR
#endif
#define LT_HDR "W5500_NET_POSIX"
#define LOG_HDR  LOG_STRING(LT_HDR)

// ============================================================================
// POSIX IMPLEMENTATION OF OPEN/CLOSE
// ============================================================================

W5500Net::Status W5500Net::open(const std::string& ipAddr, uint16_t u16Port)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_iSocketFd != -1) {
        ::close(m_iSocketFd);
    }

    m_strServerIp = ipAddr;
    m_u16Port = u16Port;

    // 1. Create Socket (IPv4, TCP)
    m_iSocketFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_iSocketFd < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to create socket"));
        return Status::PORT_ACCESS;
    }

    // 2. Configure Socket Options (TCP_NODELAY for low latency)
    int flag = 1;
    if (::setsockopt(m_iSocketFd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Failed to set TCP_NODELAY"));
    }

    // 3. Set Timeout for Connect
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    ::setsockopt(m_iSocketFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // 4. Connect to Server
    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(u16Port);

    // Resolve IP
    if (::inet_pton(AF_INET, ipAddr.c_str(), &server_addr.sin_addr) <= 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid IP address: "); LOG_STRING(ipAddr.c_str()));
        ::close(m_iSocketFd);
        m_iSocketFd = -1;
        return Status::INVALID_PARAM;
    }

    if (::connect(m_iSocketFd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Connection failed to "); LOG_STRING(ipAddr.c_str()));
        ::close(m_iSocketFd);
        m_iSocketFd = -1;
        return Status::PORT_ACCESS;
    }

    // Restore non-blocking or default timeout behavior for I/O
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    ::setsockopt(m_iSocketFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Connected to W5500 server at "); LOG_STRING(ipAddr.c_str()));

    return Status::SUCCESS;
}

W5500Net::Status W5500Net::close()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_iSocketFd != -1) {
        ::shutdown(m_iSocketFd, SHUT_RDWR);
        ::close(m_iSocketFd);
        m_iSocketFd = -1;
    }
    return Status::SUCCESS;
}

bool W5500Net::is_open() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_iSocketFd != -1;
}
