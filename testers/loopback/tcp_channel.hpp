// tcp_channel.hpp - TCP/IP driver.
//
// Two modes, chosen by the spec (see channel_factory.hpp):
//
//   server  ("tcpip:5000" or "tcpip:server/5000[/bindaddr]")
//     Listens and accepts one client at a time, exactly like the original
//     eth_loopback_server.cpp. This is the natural mode for an *input*
//     channel: the tool waits for someone to connect and send it data.
//     Used as a mirror ("-i tcpip:5000" alone), replies go straight back
//     on the same accepted socket, reproducing the original tool exactly.
//
//   client ("tcpip:client/host/port")
//     Connects out to host:port once at startup. This is the natural mode
//     for a pure *output* channel: e.g. "-i uart:/dev/tnt0 -o
//     tcpip:client/192.168.1.10/5000" dials out and forwards every UART
//     chunk to that peer.
//
// A server-mode channel used purely as output (no "-i" on it) will
// block-accept a client the first time writeMessage() is called with none
// connected yet.
#pragma once

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>

#include "ichannel.hpp"

namespace loopback
{

class TcpChannel : public IChannel
{
public:
    struct ServerTag {};
    struct ClientTag {};

    // Server: listen on bind_addr:port.
    TcpChannel(ServerTag, std::string bind_addr, int port)
        : is_server_(true), host_(std::move(bind_addr)), port_(port)
    {
    }

    // Client: connect out to host:port.
    TcpChannel(ClientTag, std::string host, int port)
        : is_server_(false), host_(std::move(host)), port_(port)
    {
    }

    ~TcpChannel() override { TcpChannel::close(); }

    bool open() override
    {
        return is_server_ ? openServer() : openClient();
    }

    void close() override
    {
        if (client_fd_ >= 0) { ::close(client_fd_); client_fd_ = -1; }
        if (listen_fd_ >= 0) { ::close(listen_fd_); listen_fd_ = -1; }
    }

    bool readMessage(Message &msg) override
    {
        while (!g_stop)
        {
            if (client_fd_ < 0 && !acceptOrConnect())
                return false;

            uint8_t buf[4096];
            ssize_t n = ::recv(client_fd_, buf, sizeof(buf), 0);
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                log_err(name(), std::string("recv: ") + std::strerror(errno));
                dropClient();
                if (!is_server_)
                    return false; // a client has nobody else to talk to
                continue;         // server: wait for the next client
            }
            if (n == 0)
            {
                log_info(name(), "peer " + peer_ + " closed the connection");
                dropClient();
                if (!is_server_)
                    return false;
                continue; // server: accept the next client
            }

            msg.data.assign(buf, buf + n);
            msg.has_can_id = false;
            return true;
        }
        return false;
    }

    bool writeMessage(Message &msg) override
    {
        if (client_fd_ < 0 && !acceptOrConnect())
            return false;

        size_t sent = 0;
        while (sent < msg.data.size())
        {
            ssize_t n = ::send(client_fd_, msg.data.data() + sent, msg.data.size() - sent, MSG_NOSIGNAL);
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                log_err(name(), std::string("send: ") + std::strerror(errno));
                dropClient();
                return false;
            }
            sent += static_cast<size_t>(n);
        }
        return true;
    }

    std::string name() const override
    {
        if (is_server_)
            return "tcpip:server/" + std::to_string(port_) + (host_.empty() ? "" : "/" + host_);
        return "tcpip:client/" + host_ + "/" + std::to_string(port_);
    }

    std::string identity() const override
    {
        return is_server_ ? "tcpip:server:" + std::to_string(port_)
                           : "tcpip:client:" + host_ + ":" + std::to_string(port_);
    }

    void dump(const char *dir, const Message &msg) const override
    {
        dump_bytes(name(), dir, msg.data.data(), msg.data.size());
    }

private:
    bool openServer()
    {
        struct addrinfo hints;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family   = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags    = AI_PASSIVE;

        struct addrinfo *result = nullptr;
        std::string port_str = std::to_string(port_);
        int rc = ::getaddrinfo(host_.empty() ? nullptr : host_.c_str(), port_str.c_str(), &hints, &result);
        if (rc != 0 || result == nullptr)
        {
            log_err(name(), std::string("getaddrinfo: ") + ::gai_strerror(rc));
            return false;
        }

        for (struct addrinfo *ai = result; ai != nullptr; ai = ai->ai_next)
        {
            listen_fd_ = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (listen_fd_ < 0)
                continue;

            int reuse = 1;
            ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
            if (ai->ai_family == AF_INET6)
            {
                int v6only = 0;
                ::setsockopt(listen_fd_, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
            }

            if (::bind(listen_fd_, ai->ai_addr, ai->ai_addrlen) == 0)
                break;

            ::close(listen_fd_);
            listen_fd_ = -1;
        }
        ::freeaddrinfo(result);

        if (listen_fd_ < 0)
        {
            log_err(name(), std::string("bind failed: ") + std::strerror(errno));
            return false;
        }

        if (::listen(listen_fd_, kBacklog) < 0)
        {
            log_err(name(), std::string("listen: ") + std::strerror(errno));
            close();
            return false;
        }

        log_info(name(), "listening (Ctrl-C to stop)");
        return true;
    }

    bool openClient()
    {
        struct addrinfo hints;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family   = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        struct addrinfo *result = nullptr;
        std::string port_str = std::to_string(port_);
        int rc = ::getaddrinfo(host_.c_str(), port_str.c_str(), &hints, &result);
        if (rc != 0 || result == nullptr)
        {
            log_err(name(), std::string("getaddrinfo: ") + ::gai_strerror(rc));
            return false;
        }

        int fd = -1;
        for (struct addrinfo *ai = result; ai != nullptr; ai = ai->ai_next)
        {
            fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (fd < 0)
                continue;
            if (::connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
                break;
            ::close(fd);
            fd = -1;
        }
        ::freeaddrinfo(result);

        if (fd < 0)
        {
            log_err(name(), std::string("connect: ") + std::strerror(errno));
            return false;
        }

        int nodelay = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

        client_fd_ = fd;
        peer_      = host_ + ":" + std::to_string(port_);
        log_info(name(), "connected to " + peer_);
        return true;
    }

    // Ensures client_fd_ is valid: for a server, blocks in accept(); for a
    // client, the connection was already made in open() so this only
    // fails if that connection was lost and cannot be re-established
    // automatically.
    bool acceptOrConnect()
    {
        if (!is_server_)
            return client_fd_ >= 0; // client re-dialing is out of scope; open() already connected once

        while (!g_stop)
        {
            struct sockaddr_storage addr;
            socklen_t addr_len = sizeof(addr);
            int fd = ::accept(listen_fd_, reinterpret_cast<struct sockaddr *>(&addr), &addr_len);
            if (fd < 0)
            {
                if (errno == EINTR)
                    continue;
                log_err(name(), std::string("accept: ") + std::strerror(errno));
                return false;
            }

            int nodelay = 1;
            ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

            client_fd_ = fd;
            peer_      = peerToString(addr, addr_len);
            log_info(name(), "client connected: " + peer_);
            return true;
        }
        return false;
    }

    void dropClient()
    {
        if (client_fd_ >= 0)
        {
            ::close(client_fd_);
            client_fd_ = -1;
        }
    }

    static std::string peerToString(const struct sockaddr_storage &addr, socklen_t addr_len)
    {
        char host[NI_MAXHOST] = "?";
        char port[NI_MAXSERV] = "?";
        ::getnameinfo(reinterpret_cast<const struct sockaddr *>(&addr), addr_len,
                      host, sizeof(host), port, sizeof(port),
                      NI_NUMERICHOST | NI_NUMERICSERV);
        return std::string(host) + ":" + port;
    }

    static constexpr int kBacklog = 8;

    bool        is_server_;
    std::string host_;
    int         port_;
    int         listen_fd_ = -1;
    int         client_fd_ = -1;
    std::string peer_;
};

} // namespace loopback
