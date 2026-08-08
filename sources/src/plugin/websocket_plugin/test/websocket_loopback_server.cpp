// websocket_loopback_server.cpp
//
// Minimal RFC 6455 WebSocket echo server: accepts one client connection at a
// time, performs the HTTP Upgrade handshake, then echoes back every Text or
// Binary message it receives, byte for byte, as a frame of the same opcode.
// Answers Ping with Pong and Close with Close. This is the WebSocket analog
// of tcpip_loopback_server.cpp / udp_loopback_server.cpp, and exists purely
// to exercise the WEBSOCKET driver (tout_read() / tout_write() / the
// delimiter and token read modes) against something real without needing an
// actual WebSocket service.
//
// It is a standalone test tool, not part of the uWebSocket library: it does
// not use ICommDriver, WebSocket, or any project headers (it even carries
// its own tiny SHA-1 + base64, deliberately not shared with the driver's),
// and can be built directly with a single compiler invocation:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -o websocket_loopback_server websocket_loopback_server.cpp
//
// Usage:
//   websocket_loopback_server [port] [bind_address]
//
//   port          TCP port to listen on (default: 5100)
//   bind_address  Address to bind to (default: "::" - dual-stack, accepts
//                 both IPv6 and IPv4 clients, e.g. connections to
//                 "127.0.0.1" or "::1"). Pass "0.0.0.0" to restrict to
//                 IPv4-only, or a specific address to bind a single
//                 interface.
//
// Behaviour:
//   - Listens, then serially accepts one client at a time (no threading -
//     good enough for a test double; a second client just queues in the
//     backlog until the first disconnects).
//   - For each client: performs the HTTP Upgrade handshake, then loops
//     reading/unmasking WS frames and echoing Text/Binary messages back
//     (unmasked, as required of a server - RFC 6455 s.5.1). Continuation
//     frames are forwarded as-is (no defragmentation needed to echo).
//   - Logs connect/disconnect and frame counts to stdout.
//   - Ctrl+C (SIGINT) or SIGTERM stops the server after the current recv()
//     call returns.

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{
    constexpr int         DEFAULT_PORT   = 5100;
    constexpr const char* DEFAULT_BIND   = "::";
    constexpr int          LISTEN_BACKLOG = 8;
    constexpr const char*  WS_GUID        = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    volatile sig_atomic_t g_stop = 0;

    void on_signal(int /*sig*/) { g_stop = 1; }

    // ------------------------------------------------------------------
    // SHA-1 + base64 - self-contained, only used to derive
    // Sec-WebSocket-Accept from the client's Sec-WebSocket-Key.
    // ------------------------------------------------------------------
    void sha1(const uint8_t* data, size_t len, uint8_t digestOut[20])
    {
        uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;

        std::vector<uint8_t> msg(data, data + len);
        const uint64_t ml = static_cast<uint64_t>(len) * 8ULL;
        msg.push_back(0x80);
        while ((msg.size() % 64) != 56) msg.push_back(0x00);
        for (int i = 7; i >= 0; --i) msg.push_back(static_cast<uint8_t>((ml >> (8 * i)) & 0xFF));

        for (size_t off = 0; off < msg.size(); off += 64)
        {
            uint32_t w[80];
            for (int i = 0; i < 16; ++i)
                w[i] = (uint32_t(msg[off + size_t(i) * 4]) << 24) | (uint32_t(msg[off + size_t(i) * 4 + 1]) << 16) |
                       (uint32_t(msg[off + size_t(i) * 4 + 2]) << 8) | uint32_t(msg[off + size_t(i) * 4 + 3]);
            for (int i = 16; i < 80; ++i) { uint32_t v = w[i-3]^w[i-8]^w[i-14]^w[i-16]; w[i] = (v<<1)|(v>>31); }

            uint32_t a=h0,b=h1,c=h2,d=h3,e=h4;
            for (int i = 0; i < 80; ++i)
            {
                uint32_t f,k;
                if (i<20)      { f=(b&c)|((~b)&d);        k=0x5A827999; }
                else if (i<40) { f=b^c^d;                 k=0x6ED9EBA1; }
                else if (i<60) { f=(b&c)|(b&d)|(c&d);     k=0x8F1BBCDC; }
                else           { f=b^c^d;                 k=0xCA62C1D6; }
                uint32_t temp = ((a<<5)|(a>>27)) + f + e + k + w[i];
                e=d; d=c; c=(b<<30)|(b>>2); b=a; a=temp;
            }
            h0+=a; h1+=b; h2+=c; h3+=d; h4+=e;
        }
        uint32_t hs[5] = {h0,h1,h2,h3,h4};
        for (int i=0;i<5;++i) {
            digestOut[i*4]=uint8_t((hs[i]>>24)&0xFF); digestOut[i*4+1]=uint8_t((hs[i]>>16)&0xFF);
            digestOut[i*4+2]=uint8_t((hs[i]>>8)&0xFF); digestOut[i*4+3]=uint8_t(hs[i]&0xFF);
        }
    }

    std::string base64_encode(const uint8_t* data, size_t len)
    {
        static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        size_t i = 0;
        while (i + 3 <= len) {
            uint32_t n = (uint32_t(data[i])<<16) | (uint32_t(data[i+1])<<8) | data[i+2];
            out += tbl[(n>>18)&0x3F]; out += tbl[(n>>12)&0x3F]; out += tbl[(n>>6)&0x3F]; out += tbl[n&0x3F];
            i += 3;
        }
        const size_t rem = len - i;
        if (rem == 1) {
            uint32_t n = uint32_t(data[i])<<16;
            out += tbl[(n>>18)&0x3F]; out += tbl[(n>>12)&0x3F]; out += "==";
        } else if (rem == 2) {
            uint32_t n = (uint32_t(data[i])<<16) | (uint32_t(data[i+1])<<8);
            out += tbl[(n>>18)&0x3F]; out += tbl[(n>>12)&0x3F]; out += tbl[(n>>6)&0x3F]; out += '=';
        }
        return out;
    }

    std::string compute_accept(const std::string& key)
    {
        const std::string concat = key + WS_GUID;
        uint8_t digest[20];
        sha1(reinterpret_cast<const uint8_t*>(concat.data()), concat.size(), digest);
        return base64_encode(digest, sizeof(digest));
    }

    // ------------------------------------------------------------------
    // Blocking socket helpers
    // ------------------------------------------------------------------
    bool recv_exact(int fd, uint8_t* buf, size_t len)
    {
        size_t got = 0;
        while (got < len) {
            const ssize_t n = ::recv(fd, buf + got, len - got, 0);
            if (n < 0) { if (errno == EINTR) continue; return false; }
            if (n == 0) return false; // peer closed
            got += size_t(n);
        }
        return true;
    }

    bool send_all(int fd, const uint8_t* data, size_t len)
    {
        size_t sent = 0;
        while (sent < len) {
            const ssize_t n = ::send(fd, data + sent, len - sent, MSG_NOSIGNAL);
            if (n < 0) { if (errno == EINTR) continue; return false; }
            sent += size_t(n);
        }
        return true;
    }

    std::string peer_to_string(const struct sockaddr_storage& addr, socklen_t addrLen)
    {
        char host[NI_MAXHOST] = "?", port[NI_MAXSERV] = "?";
        ::getnameinfo(reinterpret_cast<const struct sockaddr*>(&addr), addrLen,
                      host, sizeof(host), port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);
        return std::string(host) + ":" + port;
    }

    // ------------------------------------------------------------------
    // Handshake (server side)
    // ------------------------------------------------------------------
    bool do_handshake(int fd)
    {
        std::string request;
        std::array<uint8_t, 512> chunk{};
        size_t terminator = std::string::npos;

        while (terminator == std::string::npos) {
            const ssize_t n = ::recv(fd, chunk.data(), chunk.size(), 0);
            if (n <= 0) return false;
            request.append(reinterpret_cast<char*>(chunk.data()), size_t(n));
            if (request.size() > 8192) return false;
            terminator = request.find("\r\n\r\n");
        }

        // Extract Sec-WebSocket-Key (case-insensitive header name).
        std::string key;
        size_t pos = 0;
        while (pos < terminator) {
            const size_t eol = request.find("\r\n", pos);
            const std::string line = request.substr(pos, (eol == std::string::npos ? terminator : eol) - pos);
            const size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string name = line.substr(0, colon);
                for (auto& c : name) c = char(std::tolower(static_cast<unsigned char>(c)));
                if (name == "sec-websocket-key") {
                    std::string val = line.substr(colon + 1);
                    size_t s = val.find_first_not_of(" \t");
                    size_t e = val.find_last_not_of(" \t");
                    key = (s == std::string::npos) ? "" : val.substr(s, e - s + 1);
                }
            }
            if (eol == std::string::npos) break;
            pos = eol + 2;
        }

        if (key.empty()) {
            std::fprintf(stderr, "Missing Sec-WebSocket-Key, refusing upgrade\n");
            return false;
        }

        const std::string accept = compute_accept(key);
        const std::string response =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";

        return send_all(fd, reinterpret_cast<const uint8_t*>(response.data()), response.size());
    }

    // ------------------------------------------------------------------
    // Frame echo loop
    // ------------------------------------------------------------------
    void send_frame(int fd, uint8_t opcode, const uint8_t* payload, size_t len)
    {
        std::vector<uint8_t> frame;
        frame.push_back(uint8_t(0x80 | (opcode & 0x0F))); // FIN=1, server frames are never masked
        if (len <= 125) {
            frame.push_back(uint8_t(len));
        } else if (len <= 0xFFFF) {
            frame.push_back(126);
            frame.push_back(uint8_t((len >> 8) & 0xFF));
            frame.push_back(uint8_t(len & 0xFF));
        } else {
            frame.push_back(127);
            for (int i = 7; i >= 0; --i) frame.push_back(uint8_t((uint64_t(len) >> (8 * i)) & 0xFF));
        }
        frame.insert(frame.end(), payload, payload + len);
        send_all(fd, frame.data(), frame.size());
    }

    void serve_client(int fd, const std::string& peer)
    {
        if (!do_handshake(fd)) {
            std::fprintf(stderr, "[%s] handshake failed\n", peer.c_str());
            return;
        }
        std::printf("[%s] handshake OK, echoing frames\n", peer.c_str());

        size_t totalMessages = 0;

        while (!g_stop) {
            uint8_t hdr[2];
            if (!recv_exact(fd, hdr, 2)) break;

            const bool    fin    = (hdr[0] & 0x80) != 0;
            const uint8_t opcode = hdr[0] & 0x0F;
            const bool    masked = (hdr[1] & 0x80) != 0;
            uint64_t      len    = hdr[1] & 0x7F;

            if (len == 126) { uint8_t ext[2]; if (!recv_exact(fd, ext, 2)) break; len = (uint64_t(ext[0])<<8)|ext[1]; }
            else if (len == 127) { uint8_t ext[8]; if (!recv_exact(fd, ext, 8)) break; len = 0; for (int i=0;i<8;++i) len=(len<<8)|ext[i]; }

            uint8_t maskKey[4] = {0,0,0,0};
            if (masked) { if (!recv_exact(fd, maskKey, 4)) break; }

            std::vector<uint8_t> payload(static_cast<size_t>(len));
            if (len > 0 && !recv_exact(fd, payload.data(), payload.size())) break;
            if (masked) { for (size_t i = 0; i < payload.size(); ++i) payload[i] ^= maskKey[i % 4]; }

            if (opcode == 0x8) { // Close
                send_frame(fd, 0x8, payload.data(), payload.size());
                std::printf("[%s] client closed the connection (echoed %zu messages total)\n", peer.c_str(), totalMessages);
                break;
            }
            if (opcode == 0x9) { // Ping -> Pong
                send_frame(fd, 0xA, payload.data(), payload.size());
                continue;
            }
            if (opcode == 0xA) { // Pong - ignore
                continue;
            }

            // Text (0x1) / Binary (0x2) / Continuation (0x0): echo back as-is,
            // same opcode and FIN bit, no defragmentation needed for an echo.
            std::printf("[%s] echoing %llu bytes (opcode=0x%X, fin=%d)\n",
                        peer.c_str(), static_cast<unsigned long long>(len), opcode, fin ? 1 : 0);

            std::vector<uint8_t> frame;
            frame.push_back(uint8_t((fin ? 0x80 : 0x00) | (opcode & 0x0F)));
            if (len <= 125) {
                frame.push_back(uint8_t(len));
            } else if (len <= 0xFFFF) {
                frame.push_back(126); frame.push_back(uint8_t((len>>8)&0xFF)); frame.push_back(uint8_t(len&0xFF));
            } else {
                frame.push_back(127);
                for (int i = 7; i >= 0; --i) frame.push_back(uint8_t((uint64_t(len) >> (8*i)) & 0xFF));
            }
            frame.insert(frame.end(), payload.begin(), payload.end());
            if (!send_all(fd, frame.data(), frame.size())) {
                std::fprintf(stderr, "[%s] failed to echo frame, dropping connection\n", peer.c_str());
                break;
            }
            ++totalMessages;
        }
    }
} // namespace


int main(int argc, char** argv)
{
    const int         iPort     = (argc > 1) ? std::atoi(argv[1]) : DEFAULT_PORT;
    const std::string strBindTo = (argc > 2) ? argv[2] : DEFAULT_BIND;

    if (iPort <= 0 || iPort > 65535) {
        std::fprintf(stderr, "Invalid port: %s\n", (argc > 1) ? argv[1] : "");
        return 1;
    }

    struct sigaction sSigAction = {};
    sSigAction.sa_handler = on_signal;
    sSigAction.sa_flags   = 0; // no SA_RESTART, see tcpip_loopback_server.cpp
    ::sigemptyset(&sSigAction.sa_mask);
    ::sigaction(SIGINT, &sSigAction, nullptr);
    ::sigaction(SIGTERM, &sSigAction, nullptr);
    ::signal(SIGPIPE, SIG_IGN);

    struct addrinfo sHints = {};
    sHints.ai_family   = AF_UNSPEC;
    sHints.ai_socktype = SOCK_STREAM;
    sHints.ai_protocol = IPPROTO_TCP;
    sHints.ai_flags    = AI_PASSIVE;

    struct addrinfo* pResult = nullptr;
    const std::string strPort = std::to_string(iPort);
    const int iGaiRc = ::getaddrinfo(strBindTo.c_str(), strPort.c_str(), &sHints, &pResult);
    if (iGaiRc != 0 || pResult == nullptr) {
        std::fprintf(stderr, "getaddrinfo(%s) failed: %s\n", strBindTo.c_str(), ::gai_strerror(iGaiRc));
        return 1;
    }

    int listenFd = -1;
    for (struct addrinfo* pAi = pResult; pAi != nullptr; pAi = pAi->ai_next) {
        listenFd = ::socket(pAi->ai_family, pAi->ai_socktype, pAi->ai_protocol);
        if (listenFd < 0) continue;

        int iReuse = 1;
        ::setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &iReuse, sizeof(iReuse));

        if (::bind(listenFd, pAi->ai_addr, pAi->ai_addrlen) == 0) break;

        ::close(listenFd);
        listenFd = -1;
    }
    ::freeaddrinfo(pResult);

    if (listenFd < 0) {
        std::fprintf(stderr, "Failed to bind to %s:%d\n", strBindTo.c_str(), iPort);
        return 1;
    }

    if (::listen(listenFd, LISTEN_BACKLOG) != 0) {
        std::fprintf(stderr, "listen() failed, errno=%d (%s)\n", errno, std::strerror(errno));
        ::close(listenFd);
        return 1;
    }

    std::printf("websocket_loopback_server listening on %s:%d\n", strBindTo.c_str(), iPort);

    while (!g_stop) {
        struct sockaddr_storage clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);

        const int clientFd = ::accept(listenFd, reinterpret_cast<struct sockaddr*>(&clientAddr), &clientAddrLen);
        if (clientFd < 0) {
            if (errno == EINTR) continue;
            std::fprintf(stderr, "accept() failed, errno=%d (%s)\n", errno, std::strerror(errno));
            break;
        }

        int iNoDelay = 1;
        ::setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &iNoDelay, sizeof(iNoDelay));

        const std::string strPeer = peer_to_string(clientAddr, clientAddrLen);
        std::printf("[%s] client connected\n", strPeer.c_str());

        serve_client(clientFd, strPeer);
        ::close(clientFd);
    }

    ::close(listenFd);
    std::printf("websocket_loopback_server stopped\n");
    return 0;
}
