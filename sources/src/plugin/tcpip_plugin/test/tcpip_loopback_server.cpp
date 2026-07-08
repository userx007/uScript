// eth_loopback_server.cpp
//
// Minimal TCP echo server: accepts one client connection at a time and
// immediately sends back whatever it receives, byte for byte, with no
// framing or buffering delay. This is the Ethernet analog of the
// "vcan_mirror" loopback app referenced in the CAN driver's open()
// comments, and exists purely to exercise the ETH driver (tout_read() /
// tout_write() / the delimiter and token read modes) against something
// real without needing an actual peer service.
//
// It is a standalone test tool, not part of the uEth library: it does not
// use ICommDriver, ETH, or any project headers, and can be built directly
// with a single compiler invocation:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -o eth_loopback_server eth_loopback_server.cpp
//
// Usage:
//   eth_loopback_server [port] [bind_address]
//
//   port          TCP port to listen on (default: 5000)
//   bind_address  Address to bind to (default: "::" — dual-stack, accepts
//                 both IPv6 and IPv4 clients, e.g. connections to
//                 "127.0.0.1" or "::1"). Pass "0.0.0.0" to restrict to
//                 IPv4-only, or a specific address to bind a single
//                 interface.
//
// Behaviour:
//   - Listens, then serially accepts one client at a time (no threading —
//     good enough for a test double; a second client just queues in the
//     backlog until the first disconnects).
//   - For each client: loops on recv()/send(), echoing each chunk back
//     immediately as it arrives (no delimiter/token awareness — it does
//     not need to understand the protocol being tested, it just mirrors
//     bytes).
//   - Logs connect/disconnect and byte counts to stdout.
//   - Ctrl+C (SIGINT) or SIGTERM stops the server after the current recv()
//     call returns.

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{
    constexpr int         DEFAULT_PORT    = 5000;
    constexpr const char* DEFAULT_BIND    = "::";
    constexpr size_t       RECV_CHUNK_SIZE = 4096;
    constexpr int          LISTEN_BACKLOG  = 8;

    volatile sig_atomic_t g_stop = 0;

    void on_signal(int /*sig*/)
    {
        g_stop = 1;
    }

    // Format a sockaddr as "host:port" for logging. Best-effort — falls back
    // to "?" fields if getnameinfo() fails.
    std::string peer_to_string(const struct sockaddr_storage& addr, socklen_t addrLen)
    {
        char szHost[NI_MAXHOST] = "?";
        char szPort[NI_MAXSERV] = "?";

        ::getnameinfo(reinterpret_cast<const struct sockaddr*>(&addr), addrLen,
                      szHost, sizeof(szHost), szPort, sizeof(szPort),
                      NI_NUMERICHOST | NI_NUMERICSERV);

        return std::string(szHost) + ":" + szPort;
    }

    // Send the whole buffer, looping over short writes. Returns false on
    // error or if the peer went away mid-send.
    bool send_all(int fd, const uint8_t* data, size_t len)
    {
        size_t sent = 0;
        while (sent < len)
        {
            const ssize_t n = ::send(fd, data + sent, len - sent, MSG_NOSIGNAL);
            if (n < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                std::fprintf(stderr, "send() failed, errno=%d (%s)\n", errno, std::strerror(errno));
                return false;
            }
            sent += static_cast<size_t>(n);
        }
        return true;
    }

    // Serve one client connection: echo bytes until it disconnects or an
    // error occurs. Returns when the connection ends.
    void serve_client(int clientFd, const std::string& strPeer)
    {
        uint8_t buffer[RECV_CHUNK_SIZE];
        size_t  totalBytes = 0;

        while (!g_stop)
        {
            const ssize_t n = ::recv(clientFd, buffer, sizeof(buffer), 0);
            if (n < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                std::fprintf(stderr, "[%s] recv() failed, errno=%d (%s)\n",
                             strPeer.c_str(), errno, std::strerror(errno));
                break;
            }
            if (n == 0)
            {
                // Orderly shutdown by the peer.
                std::printf("[%s] client closed the connection (echoed %zu bytes total)\n",
                           strPeer.c_str(), totalBytes);
                break;
            }

            const size_t szReceived = static_cast<size_t>(n);
            totalBytes += szReceived;

            std::printf("[%s] echoing %zu bytes\n", strPeer.c_str(), szReceived);

            if (!send_all(clientFd, buffer, szReceived))
            {
                std::fprintf(stderr, "[%s] failed to echo bytes back, dropping connection\n",
                             strPeer.c_str());
                break;
            }
        }
    }
} // namespace


int main(int argc, char** argv)
{
    const int         iPort     = (argc > 1) ? std::atoi(argv[1]) : DEFAULT_PORT;
    const std::string strBindTo = (argc > 2) ? argv[2] : DEFAULT_BIND;

    if (iPort <= 0 || iPort > 65535)
    {
        std::fprintf(stderr, "Invalid port: %s\n", (argc > 1) ? argv[1] : "");
        return 1;
    }

    // NOTE: plain signal(2) (via glibc's BSD-compatible wrapper) installs the
    // handler with SA_RESTART set, which transparently restarts an
    // interrupted accept()/recv() — the g_stop flag would then never be
    // observed and Ctrl+C would appear to do nothing until the next byte
    // arrives. Use sigaction() directly with SA_RESTART OFF so blocking
    // calls actually return EINTR.
    struct sigaction sSigAction = {};
    sSigAction.sa_handler = on_signal;
    sSigAction.sa_flags   = 0; // no SA_RESTART
    ::sigemptyset(&sSigAction.sa_mask);
    ::sigaction(SIGINT, &sSigAction, nullptr);
    ::sigaction(SIGTERM, &sSigAction, nullptr);
    ::signal(SIGPIPE, SIG_IGN); // we already pass MSG_NOSIGNAL, but belt-and-braces

    // Resolve the bind address (supports "::", "0.0.0.0", or a specific
    // literal) the same way the ETH driver resolves its target host.
    struct addrinfo sHints = {};
    sHints.ai_family   = AF_UNSPEC;
    sHints.ai_socktype = SOCK_STREAM;
    sHints.ai_protocol = IPPROTO_TCP;
    sHints.ai_flags    = AI_PASSIVE;

    struct addrinfo* pResult = nullptr;
    const std::string strPort = std::to_string(iPort);

    const int iGaiRc = ::getaddrinfo(strBindTo.c_str(), strPort.c_str(), &sHints, &pResult);
    if (iGaiRc != 0 || pResult == nullptr)
    {
        std::fprintf(stderr, "getaddrinfo(%s) failed: %s\n",
                     strBindTo.c_str(), ::gai_strerror(iGaiRc));
        return 1;
    }

    int listenFd = -1;
    for (struct addrinfo* pAi = pResult; pAi != nullptr; pAi = pAi->ai_next)
    {
        listenFd = ::socket(pAi->ai_family, pAi->ai_socktype, pAi->ai_protocol);
        if (listenFd < 0)
        {
            continue;
        }

        int iReuse = 1;
        ::setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &iReuse, sizeof(iReuse));

        // If we bound "::" (dual-stack wildcard), also accept IPv4 clients
        // (e.g. connections to 127.0.0.1) on the same socket.
        if (pAi->ai_family == AF_INET6)
        {
            int iV6Only = 0;
            ::setsockopt(listenFd, IPPROTO_IPV6, IPV6_V6ONLY, &iV6Only, sizeof(iV6Only));
        }

        if (::bind(listenFd, pAi->ai_addr, pAi->ai_addrlen) == 0)
        {
            break; // bound successfully
        }

        ::close(listenFd);
        listenFd = -1;
    }
    ::freeaddrinfo(pResult);

    if (listenFd < 0)
    {
        std::fprintf(stderr, "Failed to bind to %s:%d, errno=%d (%s)\n",
                     strBindTo.c_str(), iPort, errno, std::strerror(errno));
        return 1;
    }

    if (::listen(listenFd, LISTEN_BACKLOG) < 0)
    {
        std::fprintf(stderr, "listen() failed, errno=%d (%s)\n", errno, std::strerror(errno));
        ::close(listenFd);
        return 1;
    }

    std::printf("eth_loopback_server listening on [%s]:%d (Ctrl+C to stop)\n",
               strBindTo.c_str(), iPort);

    while (!g_stop)
    {
        struct sockaddr_storage sClientAddr = {};
        socklen_t               szClientLen = sizeof(sClientAddr);

        const int clientFd = ::accept(listenFd,
                                      reinterpret_cast<struct sockaddr*>(&sClientAddr),
                                      &szClientLen);
        if (clientFd < 0)
        {
            if (errno == EINTR)
            {
                continue; // likely our own signal handler firing
            }
            std::fprintf(stderr, "accept() failed, errno=%d (%s)\n", errno, std::strerror(errno));
            break;
        }

        const std::string strPeer = peer_to_string(sClientAddr, szClientLen);
        std::printf("[%s] client connected\n", strPeer.c_str());

        int iNoDelay = 1;
        ::setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &iNoDelay, sizeof(iNoDelay));

        serve_client(clientFd, strPeer);

        ::close(clientFd);
    }

    std::printf("eth_loopback_server shutting down\n");
    ::close(listenFd);
    return 0;
}
