// udp_loopback_server.cpp
//
// Minimal UDP echo server: a single unconnected socket that receives one
// datagram at a time and immediately sends the same bytes back to whichever
// address it came from. This is the UDP counterpart to
// eth_loopback_server.cpp, and exists purely to exercise the UDP driver
// (tout_read() / tout_write() / the delimiter and token read modes, plus
// the xtra_params per-call destination override) against something real.
//
// Unlike the TCP version there is no accept()/connection concept — UDP is
// connectionless, so "serving multiple clients" falls out for free: every
// inbound datagram just gets bounced back to its own source address,
// interleaved on the same socket, with no per-peer state kept at all.
//
// It is a standalone test tool, not part of the uUdp library: it does not
// use ICommDriver, UDP, or any project headers, and can be built directly
// with a single compiler invocation:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -o udp_loopback_server udp_loopback_server.cpp
//
// Usage:
//   udp_loopback_server [port] [bind_address]
//
//   port          UDP port to listen on (default: 5000)
//   bind_address  Address to bind to (default: "::" — dual-stack, accepts
//                 both IPv6 and IPv4 clients, e.g. datagrams sent to
//                 "127.0.0.1" or "::1"). Pass "0.0.0.0" to restrict to
//                 IPv4-only, or a specific address to bind a single
//                 interface.
//
// Behaviour:
//   - Binds one UDP socket, then loops on recvfrom()/sendto() forever.
//   - Each datagram is echoed back byte-for-byte to its sender, with no
//     framing, delay, or reassembly — one inbound datagram in, one outbound
//     datagram out.
//   - Dumps every datagram, both RX (as received) and TX (as echoed back)
//     in a candump-like table — see print_datagram() — same DIR/.../DATA
//     layout kvcan_loopback.c uses for CAN frames, with PEER (host:port)
//     in place of CAN's ID/DLC.
//   - Ctrl+C (SIGINT) or SIGTERM stops the server after the current
//     recvfrom() call returns.

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{
    constexpr int    DEFAULT_PORT   = 5000;
    constexpr const char* DEFAULT_BIND = "::";
    // Matches UDP_MAX_DGRAM_LEN in uUdp.hpp — the IPv4 theoretical payload
    // ceiling (65535 - 8-byte UDP header - 20-byte IP header) — so no
    // legally-sized datagram is ever truncated on receipt.
    constexpr size_t RECV_BUFFER_SIZE = 65507;
    // A datagram can be up to RECV_BUFFER_SIZE bytes — unlike a CAN frame's
    // 8 bytes, printing every byte would flood the terminal. Cap the
    // console dump and note how much was left out, same idea as
    // CommDumpModel's preview truncation in the GUI.
    constexpr size_t DUMP_MAX_BYTES = 64;

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

    /** Print one UDP datagram in a candump-like table row: DIR, PEER
     *  (host:port), LEN, and a hex dump of the data — the UDP analogue of
     *  kvcan_loopback.c's print_frame(), with the peer address in place of
     *  CAN's ID/DLC. Called for both the as-received RX datagram and the TX
     *  datagram as it's echoed back, same as kvcan's print_frame(prefix,
     *  &frame) being called on both sides of the loopback.
     */
    void print_datagram(const char* prefix, const std::string& peer, const uint8_t* data, size_t len)
    {
        std::printf("%-4s  %-24s  %-6zu ", prefix, peer.c_str(), len);

        const size_t shown = std::min(len, DUMP_MAX_BYTES);
        for (size_t i = 0; i < shown; ++i)
            std::printf("%02X ", data[i]);
        if (len > shown)
            std::printf("... (+%zu more bytes)", len - shown);
        std::printf("\n");
        std::fflush(stdout);
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

    // See eth_loopback_server.cpp for why sigaction()+SA_RESTART-off is used
    // instead of plain signal(): glibc's signal() restarts interrupted
    // blocking calls by default, which would swallow Ctrl+C until the next
    // datagram arrived.
    struct sigaction sSigAction = {};
    sSigAction.sa_handler = on_signal;
    sSigAction.sa_flags   = 0; // no SA_RESTART
    ::sigemptyset(&sSigAction.sa_mask);
    ::sigaction(SIGINT, &sSigAction, nullptr);
    ::sigaction(SIGTERM, &sSigAction, nullptr);

    struct addrinfo sHints = {};
    sHints.ai_family   = AF_UNSPEC;
    sHints.ai_socktype = SOCK_DGRAM;
    sHints.ai_protocol = IPPROTO_UDP;
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

    int sockFd = -1;
    for (struct addrinfo* pAi = pResult; pAi != nullptr; pAi = pAi->ai_next)
    {
        sockFd = ::socket(pAi->ai_family, pAi->ai_socktype, pAi->ai_protocol);
        if (sockFd < 0)
        {
            continue;
        }

        int iReuse = 1;
        ::setsockopt(sockFd, SOL_SOCKET, SO_REUSEADDR, &iReuse, sizeof(iReuse));

        // If we bound "::" (dual-stack wildcard), also accept IPv4 datagrams
        // (e.g. sent to 127.0.0.1) on the same socket.
        if (pAi->ai_family == AF_INET6)
        {
            int iV6Only = 0;
            ::setsockopt(sockFd, IPPROTO_IPV6, IPV6_V6ONLY, &iV6Only, sizeof(iV6Only));
        }

        if (::bind(sockFd, pAi->ai_addr, pAi->ai_addrlen) == 0)
        {
            break; // bound successfully
        }

        ::close(sockFd);
        sockFd = -1;
    }
    ::freeaddrinfo(pResult);

    if (sockFd < 0)
    {
        std::fprintf(stderr, "Failed to bind to %s:%d, errno=%d (%s)\n",
                     strBindTo.c_str(), iPort, errno, std::strerror(errno));
        return 1;
    }

    std::printf("udp_loopback_server listening on [%s]:%d (Ctrl+C to stop)\n",
               strBindTo.c_str(), iPort);
    std::printf("%-4s  %-24s  %-6s  %s\n", "DIR", "PEER", "LEN", "DATA");
    std::printf("--------------------------------------------------------------------------------\n");

    static uint8_t buffer[RECV_BUFFER_SIZE];

    while (!g_stop)
    {
        struct sockaddr_storage sSenderAddr = {};
        socklen_t               szSenderLen = sizeof(sSenderAddr);

        const ssize_t nRecv = ::recvfrom(sockFd, buffer, sizeof(buffer), 0,
                                         reinterpret_cast<struct sockaddr*>(&sSenderAddr),
                                         &szSenderLen);
        if (nRecv < 0)
        {
            if (errno == EINTR)
            {
                continue; // likely our own signal handler firing
            }
            std::fprintf(stderr, "recvfrom() failed, errno=%d (%s)\n", errno, std::strerror(errno));
            break;
        }

        const std::string strPeer = peer_to_string(sSenderAddr, szSenderLen);
        print_datagram("RX", strPeer, buffer, static_cast<size_t>(nRecv));

        const ssize_t nSent = ::sendto(sockFd, buffer, static_cast<size_t>(nRecv), 0,
                                       reinterpret_cast<struct sockaddr*>(&sSenderAddr),
                                       szSenderLen);
        if (nSent < 0)
        {
            // Log and keep serving — a single bad send (e.g. an async ICMP
            // "port unreachable" from an earlier datagram to a since-gone
            // peer) shouldn't take the whole loopback server down.
            std::fprintf(stderr, "[%s] sendto() failed, errno=%d (%s)\n",
                         strPeer.c_str(), errno, std::strerror(errno));
        }
        else if (nSent != nRecv)
        {
            std::fprintf(stderr, "[%s] short sendto(): sent %zd of %zd bytes\n",
                         strPeer.c_str(), nSent, nRecv);
        }
        else
        {
            print_datagram("TX", strPeer, buffer, static_cast<size_t>(nSent));
        }
    }

    std::printf("udp_loopback_server shutting down\n");
    ::close(sockFd);
    return 0;
}
