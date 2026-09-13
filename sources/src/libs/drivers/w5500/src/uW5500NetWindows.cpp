#include "uW5500Net.hpp"
#include "uLogger.hpp"

#include <cstring>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

// Winsock port of uW5500NetPosix.cpp. See uTcpipWindows.cpp's
// WinsockGuard for the reasoning behind a self-contained, per-translation-
// unit WSAStartup()/WSACleanup() pair; the same pattern is repeated here
// (and in uLan8720NetWindows.cpp) rather than shared,
// since these three "*Net" drivers don't otherwise share a translation unit
// with uTcpip/uUdp.

#ifdef LT_HDR
    #undef LT_HDR
#endif
#define LT_HDR "W5500_NET_WIN"
#define LOG_HDR  LOG_STRING(LT_HDR)

namespace {
    class WinsockGuard {
        public:
            WinsockGuard() {
                WSADATA wsaData;
                m_bOk = (::WSAStartup(MAKEWORD(2, 2), &wsaData) == 0);
                if (!m_bOk) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("WSAStartup() failed"));
                }
            }
            ~WinsockGuard() {
                if (m_bOk) {
                    ::WSACleanup();
                }
            }
            bool ok() const { return m_bOk; }
        private:
            bool m_bOk = false;
    };

    WinsockGuard& winsock()
    {
        static WinsockGuard sInstance;
        return sInstance;
    }
}

// ============================================================================
// WINSOCK IMPLEMENTATION OF OPEN/CLOSE
// ============================================================================

W5500Net::Status W5500Net::open(const std::string& ipAddr, uint16_t u16Port)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!winsock().ok()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Winsock not initialised"));
        return Status::PORT_ACCESS;
    }

    if (m_iSocketFd != -1) {
        ::closesocket(static_cast<SOCKET>(m_iSocketFd));
    }

    m_strServerIp = ipAddr;
    m_u16Port = u16Port;

    // 1. Create Socket (IPv4, TCP)
    SOCKET sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to create socket"));
        return Status::PORT_ACCESS;
    }
    m_iSocketFd = static_cast<int>(sock);

    // 2. Configure Socket Options (TCP_NODELAY for low latency)
    BOOL flag = TRUE;
    if (::setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
                      reinterpret_cast<const char*>(&flag), sizeof(flag)) != 0) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Failed to set TCP_NODELAY"));
    }

    // 3. Set Timeout for Connect
    DWORD dwTimeoutMs = 2000;
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&dwTimeoutMs), sizeof(dwTimeoutMs));

    // 4. Connect to Server
    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(u16Port);

    // Resolve IP
    if (::inet_pton(AF_INET, ipAddr.c_str(), &server_addr.sin_addr) != 1) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid IP address: "); LOG_STRING(ipAddr.c_str()));
        ::closesocket(sock);
        m_iSocketFd = -1;
        return Status::INVALID_PARAM;
    }

    if (::connect(sock, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)) != 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Connection failed to "); LOG_STRING(ipAddr.c_str()));
        ::closesocket(sock);
        m_iSocketFd = -1;
        return Status::PORT_ACCESS;
    }

    // Restore default (blocking, no timeout) behavior for I/O — the caller's
    // own timeout_read()/timeout_write() polling loop takes over from here.
    dwTimeoutMs = 0;
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&dwTimeoutMs), sizeof(dwTimeoutMs));

    LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("Connected to W5500 server at "); LOG_STRING(ipAddr.c_str()));

    return Status::SUCCESS;
}

W5500Net::Status W5500Net::close()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_iSocketFd != -1) {
        ::shutdown(static_cast<SOCKET>(m_iSocketFd), SD_BOTH);
        ::closesocket(static_cast<SOCKET>(m_iSocketFd));
        m_iSocketFd = -1;
    }
    return Status::SUCCESS;
}

bool W5500Net::is_open() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_iSocketFd != -1;
}
