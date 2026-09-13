// can_channel.hpp - SocketCAN ("kvcan") driver.
//
// Mirror mode (this same channel object is used as both input and output,
// i.e. "-i kvcan:vcan0" with no "-o", or "-o kvcan:vcan0" identical to the
// input spec) reproduces vcan_mirror.c exactly: a single socket with
// CAN_RAW_RECV_OWN_MSGS=0, relying on CAN_RAW_LOOPBACK staying on (the
// default) so the frame we write is still delivered to every other local
// socket - just not back to ourselves. See kvcan_loopback.c's header
// comment for the full rationale; it is not repeated here.
//
// Bridge mode (a *different* CanChannel object, e.g. a different
// interface, or the same interface driven by a distinct arbitration ID)
// is just a plain CAN_RAW socket: it only ever writes frames it is handed,
// so there is no self-reception concern.
//
// CAN payloads are hard-limited to 8 bytes. When this channel is used as
// an *output* and the message is longer, bytes from index 8 onward are
// dropped and a warning is logged - the first 8 bytes are still sent.
#pragma once

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <optional>

#include <linux/can.h>
#include <linux/can/raw.h>

#include "ichannel.hpp"

namespace loopback
{

class CanChannel : public IChannel
{
public:
    // fixed_id: arbitration ID to use on TX, from the spec (e.g.
    // "kvcan:vcan0/0x100"). If absent, TX falls back to the ID carried in
    // the Message (i.e. whatever a CAN *input* channel last received),
    // and finally to kDefaultId.
    CanChannel(std::string ifname, std::optional<uint32_t> fixed_id)
        : ifname_(std::move(ifname)), fixed_id_(fixed_id)
    {
    }

    ~CanChannel() override { CanChannel::close(); }

    // Called by loopback.cpp when this exact channel object will serve as
    // both the input and the output (implicit mirror, or an explicit "-o"
    // that resolves to the same interface as "-i").
    void setMirrorMode(bool mirror) { mirror_mode_ = mirror; }

    bool open() override
    {
        struct ifreq ifr;
        std::memset(&ifr, 0, sizeof(ifr));
        std::strncpy(ifr.ifr_name, ifname_.c_str(), IFNAMSIZ - 1);

        fd_ = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
        if (fd_ < 0)
        {
            log_err(name(), std::string("socket: ") + std::strerror(errno));
            return false;
        }

        if (::ioctl(fd_, SIOCGIFINDEX, &ifr) < 0)
        {
            log_err(name(), "ioctl SIOCGIFINDEX for '" + ifname_ + "': " + std::strerror(errno));
            close();
            return false;
        }

        struct sockaddr_can addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.can_family  = AF_CAN;
        addr.can_ifindex = ifr.ifr_ifindex;

        if (::bind(fd_, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0)
        {
            log_err(name(), std::string("bind: ") + std::strerror(errno));
            close();
            return false;
        }

        if (mirror_mode_)
        {
            // Do NOT receive frames we wrote ourselves. CAN_RAW_LOOPBACK
            // stays on (default) so the peer waiting for our echoed reply
            // still gets it; only *this* socket's view of its own writes
            // is suppressed, which is what breaks the echo storm.
            int recv_own = 0;
            if (::setsockopt(fd_, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS,
                              &recv_own, sizeof(recv_own)) < 0)
            {
                log_err(name(), std::string("setsockopt CAN_RAW_RECV_OWN_MSGS: ") + std::strerror(errno));
                close();
                return false;
            }
        }

        log_info(name(), "bound (mirror_mode=" + std::string(mirror_mode_ ? "yes" : "no") + ")");
        return true;
    }

    void close() override
    {
        if (fd_ >= 0)
        {
            ::close(fd_);
            fd_ = -1;
        }
    }

    bool readMessage(Message &msg) override
    {
        while (!g_stop)
        {
            struct can_frame frame;
            ssize_t n = ::read(fd_, &frame, sizeof(frame));

            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                log_err(name(), std::string("read: ") + std::strerror(errno));
                return false;
            }
            if (n < static_cast<ssize_t>(sizeof(struct can_frame)))
            {
                log_warn(name(), "short read (" + std::to_string(n) + " bytes), ignoring");
                continue;
            }
            if (frame.can_id & CAN_RTR_FLAG)
            {
                log_info(name(), "RX (RTR, skipped)");
                continue;
            }

            msg.data.assign(frame.data, frame.data + frame.can_dlc);
            msg.has_can_id = true;
            msg.can_id     = frame.can_id & CAN_EFF_MASK;
            return true;
        }
        return false;
    }

    bool writeMessage(Message &msg) override
    {
        if (msg.data.size() > CAN_MAX_DLEN)
        {
            log_warn(name(), "message is " + std::to_string(msg.data.size()) +
                                  " bytes, CAN payload is limited to 8 - dropping bytes 9.." +
                                  std::to_string(msg.data.size()));
            msg.data.resize(CAN_MAX_DLEN);
        }

        struct can_frame frame;
        std::memset(&frame, 0, sizeof(frame));
        frame.can_id  = fixed_id_ ? *fixed_id_ : (msg.has_can_id ? msg.can_id : kDefaultId);
        frame.can_dlc = static_cast<uint8_t>(msg.data.size());
        std::memcpy(frame.data, msg.data.data(), msg.data.size());

        // Reflect the ID actually transmitted back into msg so the TX
        // dump line (printed by the caller after writeMessage returns)
        // shows what really went on the bus.
        msg.has_can_id = true;
        msg.can_id     = frame.can_id;

        ssize_t sent = ::write(fd_, &frame, sizeof(frame));
        if (sent < 0)
        {
            log_err(name(), std::string("write: ") + std::strerror(errno));
            return false;
        }
        return true;
    }

    std::string name() const override
    {
        std::string s = "kvcan:" + ifname_;
        if (fixed_id_)
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "/0x%X", *fixed_id_);
            s += buf;
        }
        return s;
    }

    // Identity deliberately ignores the fixed ID: two specs naming the
    // same interface must be collapsed into one socket regardless of ID,
    // or the RECV_OWN_MSGS trick above cannot prevent an echo storm
    // between them (see the file header comment).
    std::string identity() const override { return "kvcan:" + ifname_; }

    bool isCan() const override { return true; }

    void dump(const char *dir, const Message &msg) const override
    {
        dump_can("kvcan:" + ifname_, dir, msg.can_id, msg.data.data(), msg.data.size());
    }

private:
    static constexpr uint32_t kDefaultId = 0x100;

    std::string              ifname_;
    std::optional<uint32_t>  fixed_id_;
    bool                     mirror_mode_ = false;
    int                      fd_ = -1;
};

} // namespace loopback
