// udp_channel.hpp - UDP driver.
//
// Two modes, chosen by the spec (see channel_factory.hpp):
//
//   server ("udp:5000" or "udp:server/5000[/bindaddr]")
//     Binds and, on read, remembers the sender of the last datagram; a
//     write on the *same* channel object goes back to that sender - this
//     reproduces udp_loopback_server.cpp exactly for the mirror case
//     ("-i udp:5000" alone).
//
//   client ("udp:client/host/port")
//     connect()s the datagram socket to a fixed target so every
//     writeMessage() goes there. Used as a pure output channel, e.g.
//     bridging CAN -> UDP.
#pragma once

#include "ichannel.hpp"

#include <cerrno> // sockets/Winsock come in through common.hpp -> tester_platform.h

namespace loopback
{

    class UdpChannel : public IChannel
    {
        public:
            struct ServerTag
            {
            };

            struct ClientTag
            {
            };

            UdpChannel(ServerTag, std::string strBind_addr, int iPort)
                : is_server_(true)
                , host_(std::move(strBind_addr))
                , port_(iPort)
            {
            }

            UdpChannel(ClientTag, std::string strHost, int iPort)
                : is_server_(false)
                , host_(std::move(strHost))
                , port_(iPort)
            {
            }

            ~UdpChannel() override
            {
                UdpChannel::close();
            }

            bool open() override
            {
                return is_server_ ? openServer() : openClient();
            }

            void close() override
            {
                if (fd_ != TP_INVALID_SOCKET) {
                    tp_close(fd_);
                    fd_ = TP_INVALID_SOCKET;
                }
            }

            bool readMessage(Message &sMsg) override
            {
                while (!g_stop) {
                    if (!tp_wait_readable(fd_, &g_stop)) { // Windows: lets Ctrl-C break a blocking recvfrom()
                        return false;
                    }
                    uint8_t buf[65507]; // IPv4 theoretical UDP payload ceiling
                    struct sockaddr_storage sender;
                    socklen_t sender_len = sizeof(sender);

                    ssize_t n            = tp_recvfrom(fd_, buf, sizeof(buf), 0,
                                                       reinterpret_cast<struct sockaddr *>(&sender), &sender_len);
                    if (n < 0) {
                        if (tp_is_eintr(tp_last_error())) {
                            continue;
                        }
                        log_err(name(), std::string("recvfrom: ") + tp_errstr(tp_last_error()));
                        return false;
                    }

                    if (is_server_) {
                        has_last_sender_ = true;
                        last_sender_     = sender;
                        last_sender_len_ = sender_len;
                        last_peer_str_   = peerToString(sender, sender_len);
                    }

                    sMsg.data.assign(buf, buf + n);
                    sMsg.has_can_id = false;
                    return true;
                }
                return false;
            }

            bool writeMessage(Message &sMsg) override
            {
                ssize_t sent;
                if (is_server_) {
                    if (!has_last_sender_) {
                        log_err(name(), "no destination known yet - this channel has not received a "
                                        "datagram to reply to; use udp:client/<host>/<port> for a pure output");
                        return false;
                    }
                    sent = tp_sendto(fd_, sMsg.data.data(), sMsg.data.size(), 0,
                                     reinterpret_cast<struct sockaddr *>(&last_sender_), last_sender_len_);
                } else {
                    sent = tp_send(fd_, sMsg.data.data(), sMsg.data.size(), 0);
                }

                if (sent < 0) {
                    log_err(name(), std::string("send: ") + tp_errstr(tp_last_error()));
                    return false;
                }
                if (static_cast<size_t>(sent) != sMsg.data.size()) {
                    log_warn(name(), "short send: " + std::to_string(sent) + " of " +
                                         std::to_string(sMsg.data.size()) + " bytes");
                }
                return true;
            }

            std::string name() const override
            {
                if (is_server_) {
                    return "udp:server/" + std::to_string(port_) + (host_.empty() ? "" : "/" + host_);
                }
                return "udp:client/" + host_ + "/" + std::to_string(port_);
            }

            std::string identity() const override
            {
                return is_server_ ? "udp:server:" + std::to_string(port_)
                                  : "udp:client:" + host_ + ":" + std::to_string(port_);
            }

            void dump(const char *pstrDir, const Message &sMsg) const override
            {
                dump_bytes(name(), pstrDir, sMsg.data.data(), sMsg.data.size());
            }

        private:
            bool openServer()
            {
                struct addrinfo hints;
                std::memset(&hints, 0, sizeof(hints));
                hints.ai_family         = AF_UNSPEC;
                hints.ai_socktype       = SOCK_DGRAM;
                hints.ai_protocol       = IPPROTO_UDP;
                hints.ai_flags          = AI_PASSIVE;

                struct addrinfo *result = nullptr;
                std::string port_str    = std::to_string(port_);
                int rc                  = ::getaddrinfo(host_.empty() ? nullptr : host_.c_str(), port_str.c_str(), &hints, &result);
                if (rc != 0 || result == nullptr) {
                    log_err(name(), std::string("getaddrinfo: ") + ::gai_strerror(rc));
                    return false;
                }

                for (struct addrinfo *ai = result; ai != nullptr; ai = ai->ai_next) {
                    fd_ = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
                    if (fd_ == TP_INVALID_SOCKET) {
                        continue;
                    }

                    tp_set_reuse_listen(fd_);
                    tp_udp_disable_connreset(fd_); // Windows: ICMP port-unreachable must not fail the next recvfrom()
                    if (ai->ai_family == AF_INET6) {
                        int v6only = 0;
                        tp_setsockopt(fd_, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
                    }

                    if (::bind(fd_, ai->ai_addr, ai->ai_addrlen) == 0) {
                        break;
                    }

                    tp_close(fd_);
                    fd_ = TP_INVALID_SOCKET;
                }
                ::freeaddrinfo(result);

                if (fd_ == TP_INVALID_SOCKET) {
                    log_err(name(), std::string("bind failed: ") + tp_errstr(tp_last_error()));
                    return false;
                }

                log_info(name(), "bound (Ctrl-C to stop)");
                return true;
            }

            bool openClient()
            {
                struct addrinfo hints;
                std::memset(&hints, 0, sizeof(hints));
                hints.ai_family         = AF_UNSPEC;
                hints.ai_socktype       = SOCK_DGRAM;
                hints.ai_protocol       = IPPROTO_UDP;

                struct addrinfo *result = nullptr;
                std::string port_str    = std::to_string(port_);
                int rc                  = ::getaddrinfo(host_.c_str(), port_str.c_str(), &hints, &result);
                if (rc != 0 || result == nullptr) {
                    log_err(name(), std::string("getaddrinfo: ") + ::gai_strerror(rc));
                    return false;
                }

                tp_socket_t fd = TP_INVALID_SOCKET;
                for (struct addrinfo *ai = result; ai != nullptr; ai = ai->ai_next) {
                    fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
                    if (fd == TP_INVALID_SOCKET) {
                        continue;
                    }
                    // connect() on a DGRAM socket just fixes the default peer for
                    // send()/recv() and lets us learn about ICMP "port
                    // unreachable" errors; it does not open a TCP-style
                    // connection.
                    if (::connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
                        break;
                    }
                    tp_close(fd);
                    fd = TP_INVALID_SOCKET;
                }
                ::freeaddrinfo(result);

                if (fd == TP_INVALID_SOCKET) {
                    log_err(name(), std::string("connect: ") + tp_errstr(tp_last_error()));
                    return false;
                }

                fd_ = fd;
                log_info(name(), "target set to " + host_ + ":" + std::to_string(port_));
                return true;
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

            bool is_server_;
            std::string host_;
            int port_;
            tp_socket_t fd_       = TP_INVALID_SOCKET;

            bool has_last_sender_ = false;
            struct sockaddr_storage last_sender_{};
            socklen_t last_sender_len_ = 0;
            std::string last_peer_str_;
    };

} // namespace loopback
