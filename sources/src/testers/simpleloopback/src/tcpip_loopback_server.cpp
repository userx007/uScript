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
//   - Dumps every chunk, both RX (as received) and TX (as echoed back) in
//     a candump-like table — see print_chunk() — same DIR/.../DATA layout
//     kvcan_loopback.c uses for CAN frames, with PEER (host:port) in place
//     of CAN's ID/DLC.
//   - Ctrl+C (SIGINT) or SIGTERM stops the server after the current recv()
//     call returns.

#include "tester_platform.h" // POSIX sockets / Winsock portability (must come first on Windows)

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <signal.h>
#include <string>

namespace {
    constexpr int DEFAULT_PORT         = 5000;
    constexpr const char *DEFAULT_BIND = "::";
    constexpr size_t RECV_CHUNK_SIZE   = 4096;
    constexpr int LISTEN_BACKLOG       = 8;
    // A TCP chunk can be up to RECV_CHUNK_SIZE bytes — unlike a CAN frame's
    // 8 bytes, printing every byte would flood the terminal. Cap the
    // console dump and note how much was left out, same idea as
    // CommDumpModel's preview truncation in the GUI.
    constexpr size_t DUMP_MAX_BYTES    = 64;

    volatile sig_atomic_t g_stop       = 0;

    void on_signal(int /*sig*/)
    {
        g_stop = 1;
    }

    // Format a sockaddr as "host:port" for logging. Best-effort — falls back
    // to "?" fields if getnameinfo() fails.
    std::string peer_to_string(const struct sockaddr_storage &addr, socklen_t addrLen)
    {
        char szHost[NI_MAXHOST] = "?";
        char szPort[NI_MAXSERV] = "?";

        ::getnameinfo(reinterpret_cast<const struct sockaddr *>(&addr), addrLen,
                      szHost, sizeof(szHost), szPort, sizeof(szPort),
                      NI_NUMERICHOST | NI_NUMERICSERV);

        return std::string(szHost) + ":" + szPort;
    }

    /** Print one TCP chunk in a candump-like table row: DIR, PEER (host:port),
     *  LEN, and a hex dump of the data — the TCP analogue of
     *  kvcan_loopback.c's print_frame(), with the peer address in place of
     *  CAN's ID/DLC. Called for both the as-received RX chunk and the TX
     *  chunk as it's echoed back, same as kvcan's print_frame(prefix, &frame)
     *  being called on both sides of the loopback.
     */
    void print_chunk(const char *pstrPrefix, const std::string &strPeer, const uint8_t *pu8Data, size_t len)
    {
        std::printf("%-4s  %-24s  %-6zu ", pstrPrefix, strPeer.c_str(), len);

        const size_t shown = std::min(len, DUMP_MAX_BYTES);
        for (size_t i = 0; i < shown; ++i) {
            std::printf("%02X ", pu8Data[i]);
        }
        if (len > shown) {
            std::printf("... (+%zu more bytes)", len - shown);
        }
        std::printf("\n");
        std::fflush(stdout);
    }

    // Send the whole buffer, looping over short writes. Returns false on
    // error or if the peer went away mid-send.
    bool send_all(tp_socket_t iFd, const uint8_t *pu8Data, size_t len)
    {
        size_t sent = 0;
        while (sent < len) {
            const ssize_t n = tp_send(iFd, pu8Data + sent, len - sent, TP_MSG_NOSIGNAL);
            if (n < 0) {
                if (tp_is_eintr(tp_last_error())) {
                    continue;
                }
                std::fprintf(stderr, "send() failed, errno=%d (%s)\n", tp_last_error(), tp_errstr(tp_last_error()));
                return false;
            }
            sent += static_cast<size_t>(n);
        }
        return true;
    }

    // Serve one client connection: echo bytes until it disconnects or an
    // error occurs. Returns when the connection ends.
    void serve_client(tp_socket_t iClientFd, const std::string &strPeer)
    {
        uint8_t buffer[RECV_CHUNK_SIZE];
        size_t totalBytes = 0;

        while (!g_stop) {
            if (!tp_wait_readable(iClientFd, &g_stop)) { // Windows: lets Ctrl+C break a blocking recv()
                break;
            }
            const ssize_t n = tp_recv(iClientFd, buffer, sizeof(buffer), 0);
            if (n < 0) {
                if (tp_is_eintr(tp_last_error())) {
                    continue;
                }
                std::fprintf(stderr, "[%s] recv() failed, errno=%d (%s)\n",
                             strPeer.c_str(), tp_last_error(), tp_errstr(tp_last_error()));
                break;
            }
            if (n == 0) {
                // Orderly shutdown by the peer.
                std::printf("[%s] client closed the connection (echoed %zu bytes total)\n",
                            strPeer.c_str(), totalBytes);
                break;
            }

            const size_t szReceived = static_cast<size_t>(n);
            totalBytes += szReceived;

            print_chunk("RX", strPeer, buffer, szReceived);

            if (!send_all(iClientFd, buffer, szReceived)) {
                std::fprintf(stderr, "[%s] failed to echo bytes back, dropping connection\n",
                             strPeer.c_str());
                break;
            }

            print_chunk("TX", strPeer, buffer, szReceived);
        }
    }
} // namespace

int main(int iArgc, char **ppstrArgv)
{
    const int iPort             = (iArgc > 1) ? std::atoi(ppstrArgv[1]) : DEFAULT_PORT;
    const std::string strBindTo = (iArgc > 2) ? ppstrArgv[2] : DEFAULT_BIND;

    if (iPort <= 0 || iPort > 65535) {
        std::fprintf(stderr, "Invalid port: %s\n", (iArgc > 1) ? ppstrArgv[1] : "");
        return 1;
    }

    if (!tp_net_init()) {
        std::fprintf(stderr, "Winsock initialisation failed\n");
        return 1;
    }

    // NOTE (POSIX): plain signal(2) (via glibc's BSD-compatible wrapper) installs the
    // handler with SA_RESTART set, which transparently restarts an
    // interrupted accept()/recv() — the g_stop flag would then never be
    // observed and Ctrl+C would appear to do nothing until the next byte
    // arrives. tp_install_stop_handler() uses sigaction() directly with SA_RESTART
    // OFF so blocking calls actually return EINTR (and ignores SIGPIPE).
    // On Windows it uses signal() and tp_wait_readable() polls g_stop instead.
    tp_install_stop_handler(on_signal);

    // Resolve the bind address (supports "::", "0.0.0.0", or a specific
    // literal) the same way the ETH driver resolves its target host.
    struct addrinfo sHints    = {};
    sHints.ai_family          = AF_UNSPEC;
    sHints.ai_socktype        = SOCK_STREAM;
    sHints.ai_protocol        = IPPROTO_TCP;
    sHints.ai_flags           = AI_PASSIVE;

    struct addrinfo *pResult  = nullptr;
    const std::string strPort = std::to_string(iPort);

    const int iGaiRc          = ::getaddrinfo(strBindTo.c_str(), strPort.c_str(), &sHints, &pResult);
    if (iGaiRc != 0 || pResult == nullptr) {
        std::fprintf(stderr, "getaddrinfo(%s) failed: %s\n",
                     strBindTo.c_str(), ::gai_strerror(iGaiRc));
        return 1;
    }

    tp_socket_t listenFd = TP_INVALID_SOCKET;
    for (struct addrinfo *pAi = pResult; pAi != nullptr; pAi = pAi->ai_next) {
        listenFd = ::socket(pAi->ai_family, pAi->ai_socktype, pAi->ai_protocol);
        if (listenFd == TP_INVALID_SOCKET) {
            continue;
        }

        tp_set_reuse_listen(listenFd);

        // If we bound "::" (dual-stack wildcard), also accept IPv4 clients
        // (e.g. connections to 127.0.0.1) on the same socket.
        if (pAi->ai_family == AF_INET6) {
            int iV6Only = 0;
            tp_setsockopt(listenFd, IPPROTO_IPV6, IPV6_V6ONLY, &iV6Only, sizeof(iV6Only));
        }

        if (::bind(listenFd, pAi->ai_addr, pAi->ai_addrlen) == 0) {
            break; // bound successfully
        }

        tp_close(listenFd);
        listenFd = TP_INVALID_SOCKET;
    }
    ::freeaddrinfo(pResult);

    if (listenFd == TP_INVALID_SOCKET) {
        std::fprintf(stderr, "Failed to bind to %s:%d, errno=%d (%s)\n",
                     strBindTo.c_str(), iPort, tp_last_error(), tp_errstr(tp_last_error()));
        return 1;
    }

    if (::listen(listenFd, LISTEN_BACKLOG) < 0) {
        std::fprintf(stderr, "listen() failed, errno=%d (%s)\n", tp_last_error(), tp_errstr(tp_last_error()));
        tp_close(listenFd);
        return 1;
    }

    std::printf("eth_loopback_server listening on [%s]:%d (Ctrl+C to stop)\n",
                strBindTo.c_str(), iPort);
    std::printf("%-4s  %-24s  %-6s  %s\n", "DIR", "PEER", "LEN", "DATA");
    std::printf("--------------------------------------------------------------------------------\n");

    while (!g_stop) {
        struct sockaddr_storage sClientAddr = {};
        socklen_t szClientLen               = sizeof(sClientAddr);

        if (!tp_wait_readable(listenFd, &g_stop)) { // Windows: lets Ctrl+C break a blocking accept()
            break;
        }

        const tp_socket_t clientFd          = ::accept(listenFd,
                                                       reinterpret_cast<struct sockaddr *>(&sClientAddr),
                                                       &szClientLen);
        if (clientFd == TP_INVALID_SOCKET) {
            if (tp_is_eintr(tp_last_error())) {
                continue; // likely our own signal handler firing
            }
            std::fprintf(stderr, "accept() failed, errno=%d (%s)\n", tp_last_error(), tp_errstr(tp_last_error()));
            break;
        }

        const std::string strPeer = peer_to_string(sClientAddr, szClientLen);
        std::printf("[%s] client connected\n", strPeer.c_str());

        int iNoDelay = 1;
        tp_setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &iNoDelay, sizeof(iNoDelay));

        serve_client(clientFd, strPeer);

        tp_close(clientFd);
    }

    std::printf("eth_loopback_server shutting down\n");
    tp_close(listenFd);
    return 0;
}
