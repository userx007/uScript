#include "RealCommDriver.hpp"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>

namespace
{
    // Legacy fixed ids kept only so any old caller still spelling out "A2B"/"B2A"
    // keeps working; every other xtra_params value is parsed as a CAN id below.
    constexpr uint32_t kIdA2B_CAN = 0x100;
    constexpr uint32_t kIdB2A_CAN = 0x200;
}

RealCommDriver::RealCommDriver(const std::string& interfaceName)
    : m_interface(interfaceName)
{
    init(interfaceName);
}

RealCommDriver::~RealCommDriver()
{
    if (m_socket >= 0) {
        ::close(m_socket);
        m_socket = -1;
    }
}

bool RealCommDriver::init(const std::string& iface)
{
    m_socket = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (m_socket < 0) {
        std::perror("socket");
        return false;
    }

    ::memset(&ifr, 0, sizeof(ifr));
    std::strncpy(ifr.ifr_name, iface.c_str(), IFNAMSIZ - 1);
    ifr.ifr_ifindex = ::if_nametoindex(iface.c_str());

    if (!ifr.ifr_ifindex) {
        std::perror("if_nametoindex");
        ::close(m_socket);
        m_socket = -1;
        return false;
    }

    ::memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (::bind(m_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::perror("bind");
        ::close(m_socket);
        m_socket = -1;
        return false;
    }

    // --- Key Change: Disable Own Message Reception ---
    // This matches the vcan_mirror.c logic.
    // When we write a frame, it is looped back to other sockets (and our own socket
    // if we send to a DIFFERENT ID), but NOT to our own receive queue if we send
    // to the SAME ID.
    int recv_own = 0;
    if (::setsockopt(m_socket, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, &recv_own, sizeof(recv_own)) < 0) {
        std::perror("setsockopt CAN_RAW_RECV_OWN_MSGS");
        ::close(m_socket);
        m_socket = -1;
        return false;
    }

    // Set non-blocking
    int flags = fcntl(m_socket, F_GETFL, 0);
    if (flags < 0) {
        ::close(m_socket);
        m_socket = -1;
        return false;
    }
    fcntl(m_socket, F_SETFL, flags | O_NONBLOCK);

    reset();
    return true;
}

bool RealCommDriver::is_open() const
{
    return m_socket >= 0;
}

CommDetails RealCommDriver::describeConnection(std::string_view /*xtra_params*/) const
{
    CommDetails cd;
    cd.family = CommFamily::CAN;
    std::copy(m_interface.begin(), m_interface.end(), cd.label);

    return cd;
}

void RealCommDriver::reset()
{
    if (m_socket < 0) return;
    struct can_filter rfilter[1];
    ::memset(rfilter, 0, sizeof(rfilter));
    rfilter[0].can_id = 0;
    rfilter[0].can_mask = 0;
    ::setsockopt(m_socket, SOL_CAN_RAW, CAN_RAW_FILTER, rfilter, sizeof(rfilter));
}

uint32_t RealCommDriver::parse_can_id(std::string_view xtra_params) const
{
    // Legacy tokens from the in-memory loopback test vocabulary.
    if (xtra_params == "A2B") return kIdA2B_CAN;
    if (xtra_params == "B2A") return kIdB2A_CAN;

    // General case: xtra_params is the CAN id itself, as hex — with or
    // without a "0x"/"0X" prefix — e.g. "7E0", "0x7E0", "18DA10F1". This is
    // the convention ITransportProtocol implementations already document
    // for txId/rxId (fully-formed arbitration ids), and matches how
    // can-utils tools like cansend spell CAN ids on the command line.
    std::string s(xtra_params);
    size_t start = 0;
    if (s.size() > 1 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    {
        start = 2;
    }

    if (start >= s.size())
    {
        std::fprintf(stderr, "RealCommDriver: empty CAN id '%s', defaulting to 0\n", s.c_str());
        return 0;
    }

    try
    {
        size_t consumed = 0;
        unsigned long id = std::stoul(s.substr(start), &consumed, 16);
        if (start + consumed != s.size())
        {
            std::fprintf(stderr, "RealCommDriver: trailing garbage in CAN id '%s'\n", s.c_str());
        }
        return static_cast<uint32_t>(id);
    }
    catch (const std::exception&)
    {
        std::fprintf(stderr, "RealCommDriver: invalid CAN id '%s', defaulting to 0\n", s.c_str());
        return 0;
    }
}

ICommDriver::WriteResult RealCommDriver::tout_write(
    uint32_t u32WriteTimeout,
    std::span<const uint8_t> data,
    std::string_view xtra_params) const
{
    WriteResult result;
    result.status = Status::SUCCESS;

    uint32_t canId = parse_can_id(xtra_params);

    struct can_frame frame;
    ::memset(&frame, 0, sizeof(frame));
    frame.can_id = canId;
    if (canId > CAN_SFF_MASK)
    {
        // Doesn't fit in an 11-bit standard id: send as 29-bit extended.
        frame.can_id = (canId & CAN_EFF_MASK) | CAN_EFF_FLAG;
    }
    frame.can_dlc = std::min(data.size(), static_cast<size_t>(8));

    if (frame.can_dlc == 0) return result;

    ::memcpy(frame.data, data.data(), frame.can_dlc);

    struct timeval tv;
    tv.tv_sec = u32WriteTimeout / 1000;
    tv.tv_usec = (u32WriteTimeout % 1000) * 1000;

    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(m_socket, &writefds);

    int ret = ::select(m_socket + 1, nullptr, &writefds, nullptr, &tv);

    if (ret == 0) {
        result.status = Status::WRITE_TIMEOUT;
        return result;
    }

    ssize_t sent = ::send(m_socket, &frame, sizeof(frame), 0);
    if (sent < 0) {
        result.status = Status::WRITE_ERROR;
        return result;
    }

    result.bytes_written = frame.can_dlc;
    return result;
}

ICommDriver::ReadResult RealCommDriver::tout_read(
    uint32_t u32ReadTimeout,
    std::span<uint8_t> buffer,
    const ReadOptions& opts,
    std::string_view xtra_params) const
{
    ReadResult result;

    uint32_t filterId = parse_can_id(xtra_params);
    const bool extended = filterId > CAN_SFF_MASK;

    // Set filter for this read. RECV_OWN_MSGS is disabled (see init()), so
    // this only ever sees frames some other socket on the bus transmitted —
    // exactly what a loopback peer/tester on the far end of vcan sends us.
    if (m_socket >= 0) {
        struct can_filter rfilter[1];
        if (extended) {
            rfilter[0].can_id   = (filterId & CAN_EFF_MASK) | CAN_EFF_FLAG;
            rfilter[0].can_mask = CAN_EFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG;
        } else {
            rfilter[0].can_id   = filterId & CAN_SFF_MASK;
            rfilter[0].can_mask = CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG;
        }
        ::setsockopt(m_socket, SOL_CAN_RAW, CAN_RAW_FILTER, rfilter, sizeof(rfilter));
    }

    struct timeval tv;
    tv.tv_sec = u32ReadTimeout / 1000;
    tv.tv_usec = (u32ReadTimeout % 1000) * 1000;

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(m_socket, &readfds);

    int ret = ::select(m_socket + 1, &readfds, nullptr, nullptr, &tv);

    if (ret == 0) {
        result.status = Status::READ_TIMEOUT;
        return result;
    }

    if (ret < 0) {
        result.status = Status::READ_ERROR;
        return result;
    }

    struct can_frame frame;
    ssize_t recvLen = ::recv(m_socket, &frame, sizeof(frame), MSG_DONTWAIT);

    if (recvLen < 0) {
        result.status = Status::READ_ERROR;
        return result;
    }

    // Verify id (belt-and-braces on top of the kernel-side filter above,
    // and the only check at all for frame types the kernel filter doesn't
    // discriminate on, e.g. an incoming standard-id frame that numerically
    // collides with the low bits of an extended filterId).
    const bool receivedExtended = (frame.can_id & CAN_EFF_FLAG) != 0;
    if (receivedExtended != extended) {
        result.status = Status::READ_TIMEOUT;
        return result;
    }
    const uint32_t idMask = extended ? CAN_EFF_MASK : CAN_SFF_MASK;
    if ((frame.can_id & idMask) != (filterId & idMask)) {
         result.status = Status::READ_TIMEOUT;
         return result;
    }

    size_t len = std::min(static_cast<size_t>(frame.can_dlc), buffer.size());
    ::memcpy(buffer.data(), frame.data, len);

    result.status = Status::SUCCESS;
    result.bytes_read = len;

    return result;
}
