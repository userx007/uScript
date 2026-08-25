// raweth_channel.hpp - raw-Ethernet (AF_PACKET/SOCK_RAW) driver.
//
// readMessage() strips the 14-byte Ethernet header and returns just the
// payload in Message::data, remembering the frame's source MAC and
// EtherType internally.
//
// writeMessage(): when this channel was also the one that last read a
// frame (mirror mode, or a bridge output that happens to loop back
// through the same object), the reply is sent to that remembered source
// MAC with that EtherType, addresses swapped - identical to
// eth_raw_loopback_server.cpp. When used as a pure output channel that
// never reads (bridging some other transport into raw-Ethernet), a fixed
// destination MAC and EtherType from the spec are used instead (default
// broadcast / 0x88B5, matching the original tool's own default capture
// EtherType convention for test traffic).
#pragma once

#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <array>
#include <cerrno>
#include <optional>

#include "ichannel.hpp"

namespace loopback
{

class RawEthChannel : public IChannel
{
public:
    static constexpr size_t MAC_LEN = 6;

    // dst_mac/ethertype are the *fixed* output target used when this
    // channel never reads its own reply-to context (bridge mode from a
    // different input transport). promisc puts the interface into
    // promiscuous mode so frames not addressed to our own MAC are
    // captured too.
    RawEthChannel(std::string ifname, uint16_t capture_ethertype,
                  std::optional<uint16_t> tx_ethertype,
                  std::optional<std::array<uint8_t, MAC_LEN>> dst_mac,
                  bool promisc)
        : ifname_(std::move(ifname)),
          capture_ethertype_(capture_ethertype),
          tx_ethertype_(tx_ethertype),
          fixed_dst_mac_(dst_mac),
          promisc_(promisc)
    {
    }

    ~RawEthChannel() override { RawEthChannel::close(); }

    bool open() override
    {
        fd_ = ::socket(AF_PACKET, SOCK_RAW, htons(capture_ethertype_));
        if (fd_ < 0)
        {
            log_err(name(), std::string("socket(AF_PACKET, SOCK_RAW): ") + std::strerror(errno) +
                                 " (usually missing CAP_NET_RAW - run as root, or "
                                 "'sudo setcap cap_net_raw+ep <binary>')");
            return false;
        }

        if (!resolveInterface())
        {
            close();
            return false;
        }

        struct sockaddr_ll bind_addr;
        std::memset(&bind_addr, 0, sizeof(bind_addr));
        bind_addr.sll_family   = AF_PACKET;
        bind_addr.sll_protocol = htons(capture_ethertype_);
        bind_addr.sll_ifindex  = ifindex_;

        if (::bind(fd_, reinterpret_cast<struct sockaddr *>(&bind_addr), sizeof(bind_addr)) < 0)
        {
            log_err(name(), "bind to '" + ifname_ + "': " + std::strerror(errno));
            close();
            return false;
        }

        if (promisc_)
        {
            struct packet_mreq mreq;
            std::memset(&mreq, 0, sizeof(mreq));
            mreq.mr_ifindex = ifindex_;
            mreq.mr_type    = PACKET_MR_PROMISC;
            if (::setsockopt(fd_, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
            {
                log_err(name(), std::string("setsockopt(PACKET_ADD_MEMBERSHIP): ") + std::strerror(errno));
                close();
                return false;
            }
        }

        log_info(name(), "bound to " + ifname_ + " (mac " + macToString(own_mac_.data()) + ")" +
                              (promisc_ ? ", promiscuous" : ""));
        return true;
    }

    void close() override
    {
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    }

    bool readMessage(Message &msg) override
    {
        uint8_t buf[65536];
        while (!g_stop)
        {
            struct sockaddr_ll src_addr;
            socklen_t addr_len = sizeof(src_addr);

            ssize_t n = ::recvfrom(fd_, buf, sizeof(buf), 0,
                                    reinterpret_cast<struct sockaddr *>(&src_addr), &addr_len);
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                log_err(name(), std::string("recvfrom: ") + std::strerror(errno));
                return false;
            }

            // Ignore frames we transmitted ourselves, or the echo storm
            // never ends.
            if (src_addr.sll_pkttype == PACKET_OUTGOING)
                continue;

            if (static_cast<size_t>(n) < kEthHdrLen)
            {
                log_warn(name(), "runt frame (" + std::to_string(n) + " bytes), dropping");
                continue;
            }

            std::array<uint8_t, MAC_LEN> src_mac;
            std::memcpy(src_mac.data(), buf + MAC_LEN, MAC_LEN);
            if (src_mac == own_mac_)
                continue; // defensive: some virtual interfaces miss PACKET_OUTGOING

            uint16_t ethertype = ntohs(*reinterpret_cast<uint16_t *>(buf + 2 * MAC_LEN));

            last_src_mac_     = src_mac;
            last_ethertype_   = ethertype;
            has_last_context_ = true;

            msg.data.assign(buf + kEthHdrLen, buf + n);
            msg.has_can_id = false;
            return true;
        }
        return false;
    }

    bool writeMessage(Message &msg) override
    {
        std::array<uint8_t, MAC_LEN> dst_mac;
        uint16_t ethertype;

        if (has_last_context_)
        {
            // Mirror / same-channel bridge: reply to whoever we last
            // heard from, on the EtherType they used.
            dst_mac   = last_src_mac_;
            ethertype = last_ethertype_;
        }
        else if (fixed_dst_mac_)
        {
            dst_mac   = *fixed_dst_mac_;
            ethertype = tx_ethertype_.value_or(capture_ethertype_ == ETH_P_ALL ? kDefaultTxEthertype
                                                                                : capture_ethertype_);
        }
        else
        {
            dst_mac   = kBroadcastMac;
            ethertype = tx_ethertype_.value_or(capture_ethertype_ == ETH_P_ALL ? kDefaultTxEthertype
                                                                                : capture_ethertype_);
        }

        std::vector<uint8_t> frame(kEthHdrLen + msg.data.size());
        std::memcpy(frame.data(), dst_mac.data(), MAC_LEN);
        std::memcpy(frame.data() + MAC_LEN, own_mac_.data(), MAC_LEN);
        uint16_t net_ethertype = htons(ethertype);
        std::memcpy(frame.data() + 2 * MAC_LEN, &net_ethertype, sizeof(net_ethertype));
        std::memcpy(frame.data() + kEthHdrLen, msg.data.data(), msg.data.size());

        struct sockaddr_ll dst_addr;
        std::memset(&dst_addr, 0, sizeof(dst_addr));
        dst_addr.sll_family   = AF_PACKET;
        dst_addr.sll_ifindex  = ifindex_;
        dst_addr.sll_halen    = MAC_LEN;
        std::memcpy(dst_addr.sll_addr, dst_mac.data(), MAC_LEN);

        ssize_t sent = ::sendto(fd_, frame.data(), frame.size(), 0,
                                 reinterpret_cast<struct sockaddr *>(&dst_addr), sizeof(dst_addr));
        if (sent < 0)
        {
            log_err(name(), std::string("sendto: ") + std::strerror(errno));
            return false;
        }
        if (static_cast<size_t>(sent) != frame.size())
        {
            log_warn(name(), "short write (" + std::to_string(sent) + " of " +
                                  std::to_string(frame.size()) + " bytes)");
        }

        last_tx_dst_mac_   = dst_mac;
        last_tx_ethertype_ = ethertype;
        return true;
    }

    std::string name() const override { return "raweth:" + ifname_; }

    std::string identity() const override { return "raweth:" + ifname_; }

    void dump(const char *dir, const Message &msg) const override
    {
        std::printf("%-10s %-8s [%zu] ", name().c_str(), dir, msg.data.size());
        for (uint8_t b : msg.data)
            std::printf("%02X ", b);
        std::printf("\n");
        std::fflush(stdout);
    }

private:
    static constexpr size_t kEthHdrLen           = 14;
    static constexpr uint16_t kDefaultTxEthertype = 0x88B5; // IEEE 802 "local experimental"
    static inline const std::array<uint8_t, MAC_LEN> kBroadcastMac = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    bool resolveInterface()
    {
        struct ifreq ifr;
        std::memset(&ifr, 0, sizeof(ifr));
        std::strncpy(ifr.ifr_name, ifname_.c_str(), IFNAMSIZ - 1);

        if (::ioctl(fd_, SIOCGIFINDEX, &ifr) < 0)
        {
            log_err(name(), "ioctl(SIOCGIFINDEX, " + ifname_ + "): " + std::strerror(errno));
            return false;
        }
        ifindex_ = ifr.ifr_ifindex;

        std::memset(&ifr, 0, sizeof(ifr));
        std::strncpy(ifr.ifr_name, ifname_.c_str(), IFNAMSIZ - 1);
        if (::ioctl(fd_, SIOCGIFHWADDR, &ifr) < 0)
        {
            log_err(name(), "ioctl(SIOCGIFHWADDR, " + ifname_ + "): " + std::strerror(errno));
            return false;
        }
        std::memcpy(own_mac_.data(), ifr.ifr_hwaddr.sa_data, MAC_LEN);
        return true;
    }

    static std::string macToString(const uint8_t *mac)
    {
        char sz[18];
        std::snprintf(sz, sizeof(sz), "%02x:%02x:%02x:%02x:%02x:%02x",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return std::string(sz);
    }

    std::string ifname_;
    uint16_t    capture_ethertype_;
    std::optional<uint16_t> tx_ethertype_;
    std::optional<std::array<uint8_t, MAC_LEN>> fixed_dst_mac_;
    bool        promisc_;

    int     fd_      = -1;
    int     ifindex_ = -1;
    std::array<uint8_t, MAC_LEN> own_mac_{};

    bool    has_last_context_ = false;
    std::array<uint8_t, MAC_LEN> last_src_mac_{};
    uint16_t last_ethertype_ = 0;

    std::array<uint8_t, MAC_LEN> last_tx_dst_mac_{};
    uint16_t last_tx_ethertype_ = 0;
};

} // namespace loopback
