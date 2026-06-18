#include "uKVCan.hpp"
#include "uLogger.hpp"

#include <cstring>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <net/if.h>              // if_nametoindex, ifreq
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>           // can_frame, canfd_frame, CAN_RAW, CAN_MTU …
#include <linux/can/raw.h>       // SOL_CAN_RAW, CAN_RAW_FILTER, CAN_RAW_FD_FRAMES


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR   "KVCAN_DRV   |"
#define LOG_HDR  LOG_STRING(LT_HDR)


// ============================================================================
// OPEN / CLOSE
// ============================================================================

KVCAN::Status KVCAN::open(const std::string& strIface)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (strIface.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Invalid parameter: empty interface name"));
        return Status::INVALID_PARAM;
    }

    // Create a raw SocketCAN socket.
    m_iHandle = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (m_iHandle < 0)
    {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("socket(PF_CAN) failed, errno:"); LOG_INT(err));
        return Status::PORT_ACCESS;
    }

    // Resolve the interface name to an index.
    const unsigned int ifIdx = ::if_nametoindex(strIface.c_str());
    if (ifIdx == 0)
    {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("if_nametoindex("); LOG_STRING(strIface.c_str());
                  LOG_STRING(") failed, errno:"); LOG_INT(err));
        ::close(m_iHandle);
        m_iHandle = -1;
        return Status::PORT_ACCESS;
    }

    // Enable KVCAN FD frames so the socket can handle both classic (8-byte) and
    // FD (up to 64-byte) frames transparently.
    int canfd_on = 1;
    if (::setsockopt(m_iHandle, SOL_CAN_RAW, CAN_RAW_FD_FRAMES,
                     &canfd_on, sizeof(canfd_on)) < 0)
    {
        // Not fatal — the interface may not support KVCAN FD.
        LOG_PRINT(LOG_DEBUG, LOG_HDR;
                  LOG_STRING("KVCAN FD not supported on "); LOG_STRING(strIface.c_str());
                  LOG_STRING(", falling back to classic KVCAN"));
    }

    // Bind the socket to the interface.
    struct sockaddr_can addr = {};
    addr.can_family  = AF_CAN;
    addr.can_ifindex = static_cast<int>(ifIdx);

    if (::bind(m_iHandle,
               reinterpret_cast<struct sockaddr*>(&addr),
               sizeof(addr)) < 0)
    {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("bind() failed for "); LOG_STRING(strIface.c_str());
                  LOG_STRING(" errno:"); LOG_INT(err));
        ::close(m_iHandle);
        m_iHandle = -1;
        return Status::PORT_ACCESS;
    }

    // Do NOT receive frames this socket wrote itself.
    //
    // CAN_RAW_LOOPBACK is left at its default (enabled): the kernel still
    // delivers our TX frames to every OTHER socket on this host — in
    // particular to the vcan_mirror loopback app, which will echo them back.
    // That echo arrives here as a foreign frame (sent by a different socket)
    // and is therefore received normally, which is exactly what we want.
    //
    // CAN_RAW_RECV_OWN_MSGS = 0 excludes this socket from receiving its own
    // transmissions.  Without this, our TX would appear in our RX queue as a
    // spurious extra reply before the real echo arrives.
    //
    // Why not CAN_RAW_LOOPBACK = 0?
    //   That would suppress delivery to ALL local sockets, including the
    //   loopback app's RX socket — it would never see our frame and could
    //   never send the reply.
    int recv_own = 0;
    if (::setsockopt(m_iHandle, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS,
                     &recv_own, sizeof(recv_own)) < 0)
    {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("CAN_RAW_RECV_OWN_MSGS=0 failed, errno:"); LOG_INT(err));
        ::close(m_iHandle);
        m_iHandle = -1;
        return Status::PORT_ACCESS;
    }

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("KVCAN socket opened on "); LOG_STRING(strIface.c_str());
              LOG_STRING(", handle:"); LOG_INT(m_iHandle));

    m_vFilters.clear(); // a freshly bound socket has no filters installed yet

    return Status::SUCCESS;
}


KVCAN::Status KVCAN::close()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_iHandle >= 0)
    {
        ::close(m_iHandle);
        LOG_PRINT(LOG_DEBUG, LOG_HDR;
                  LOG_STRING("KVCAN socket closed, handle:"); LOG_INT(m_iHandle));
        m_iHandle = -1;
    }

    m_vFilters.clear();

    return Status::SUCCESS;
}


// ============================================================================
// FILTER CONFIGURATION
// ============================================================================

KVCAN::Status KVCAN::set_filters(const std::vector<CanFilter>& filters)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_iHandle < 0)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("set_filters: socket not open"));
        return Status::PORT_ACCESS;
    }

    if (filters.empty())
    {
        // Remove all filters — pass everything.
        if (::setsockopt(m_iHandle, SOL_CAN_RAW, CAN_RAW_FILTER, nullptr, 0) < 0)
        {
            const int err = errno;
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("setsockopt(CAN_RAW_FILTER, nullptr) failed, errno:");
                      LOG_INT(err));
            return Status::PORT_ACCESS;
        }
        m_vFilters.clear(); // mirror cleared kernel state so tout_read()'s
                            // transient-filter snapshot/restore stays accurate
        return Status::SUCCESS;
    }

    // Convert to kernel struct can_filter array.
    std::vector<struct can_filter> kFilters;
    kFilters.reserve(filters.size());
    for (const auto& f : filters)
    {
        struct can_filter kf = {};
        kf.can_id   = f.can_id;
        kf.can_mask = f.can_mask;
        kFilters.push_back(kf);
    }

    if (::setsockopt(m_iHandle, SOL_CAN_RAW, CAN_RAW_FILTER,
                     kFilters.data(),
                     static_cast<socklen_t>(kFilters.size() * sizeof(struct can_filter))) < 0)
    {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("setsockopt(CAN_RAW_FILTER) failed, errno:"); LOG_INT(err));
        return Status::PORT_ACCESS;
    }

    m_vFilters = filters; // mirror applied kernel state so tout_read()'s
                          // transient-filter snapshot/restore stays accurate

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("KVCAN filters set, count:"); LOG_UINT32(static_cast<uint32_t>(filters.size())));

    return Status::SUCCESS;
}


// ============================================================================
// INTERNAL READ PRIMITIVE
// Receives one KVCAN / KVCAN FD frame and copies its payload into buffer.
// bytes_read is set to the actual DLC / len field of the received frame.
// ============================================================================

KVCAN::Status KVCAN::timeout_read(uint32_t u32ReadTimeout,
                              std::span<uint8_t> buffer,
                              size_t& szBytesRead) const
{
    if (buffer.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("timeout_read: invalid parameter"));
        return Status::INVALID_PARAM;
    }

    szBytesRead = 0;

    struct pollfd sPollFd;
    sPollFd.fd      = m_iHandle;
    sPollFd.events  = POLLIN;
    sPollFd.revents = 0;

    const int iPollResult = ::poll(&sPollFd, 1, static_cast<int>(u32ReadTimeout));
    if (iPollResult < 0)
    {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("poll() failed"); LOG_INT(err));
        return Status::READ_ERROR;
    }
    else if (iPollResult == 0)
    {
        return Status::READ_TIMEOUT;
    }

    // Try to receive a KVCAN FD frame first; fall back to classic can_frame size
    // if the read returns CAN_MTU bytes.
    struct canfd_frame frame = {};
    const ssize_t nbytes = ::read(m_iHandle, &frame, sizeof(frame));

    if (nbytes < static_cast<ssize_t>(CAN_MTU))
    {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("read() returned "); LOG_INT(static_cast<int>(nbytes));
                  LOG_STRING(" errno:"); LOG_INT(err));
        return Status::READ_ERROR;
    }

    // Determine actual payload length from the frame header.
    const size_t payloadLen = static_cast<size_t>(frame.len);

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("RX id:"); LOG_HEX32(frame.can_id);
              LOG_STRING(" len:"); LOG_UINT32(static_cast<uint32_t>(payloadLen)));

    // Copy as many bytes as the caller's buffer can hold.
    const size_t copyLen = std::min(payloadLen, buffer.size());
    std::memcpy(buffer.data(), frame.data, copyLen);
    szBytesRead = copyLen;

    return Status::SUCCESS;
}


// ============================================================================
// INTERNAL WRITE PRIMITIVE
// Packs buffer into a KVCAN / KVCAN FD frame payload and transmits it.
// u32TxId is the resolved CAN ID (already chosen by the caller from either
// the xtra_params override or the default m_u32TxId).
// ============================================================================

KVCAN::Status KVCAN::timeout_write(uint32_t /*u32WriteTimeout*/,
                               std::span<const uint8_t> buffer,
                               size_t& szBytesWritten,
                               uint32_t u32TxId) const
{
    if (buffer.empty() || buffer.size() > CAN_DRV_MAX_DLEN)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Invalid parameter: buffer empty or exceeds CAN_DRV_MAX_DLEN"));
        return Status::INVALID_PARAM;
    }

    szBytesWritten = 0;

    // Choose frame type based on payload size.
    // Classic KVCAN: ≤ 8 bytes; KVCAN FD: 9–64 bytes.
    if (buffer.size() <= CAN_MAX_DLEN)
    {
        struct can_frame frame = {};
        frame.can_id  = u32TxId;
        frame.can_dlc = static_cast<uint8_t>(buffer.size());
        std::memcpy(frame.data, buffer.data(), buffer.size());

        const ssize_t nbytes = ::write(m_iHandle, &frame, CAN_MTU);
        if (nbytes != static_cast<ssize_t>(CAN_MTU))
        {
            const int err = errno;
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("write(can_frame) failed, errno:"); LOG_INT(err));
            return Status::WRITE_ERROR;
        }
    }
    else
    {
        struct canfd_frame frame = {};
        // KVCAN FD frames require a 29-bit extended ID field in the frame header.
        // CAN_EFF_FLAG is therefore OR-ed in unconditionally here, regardless of
        // whether u32TxId (from set_tx_id() or an xtra_params override) already
        // carries it.  Callers that intend a standard 11-bit ID on an FD-sized
        // payload must be aware that the frame will be transmitted as extended.
        frame.can_id = u32TxId | CAN_EFF_FLAG;
        frame.len    = static_cast<uint8_t>(buffer.size());
        frame.flags  = 0;
        std::memcpy(frame.data, buffer.data(), buffer.size());

        const ssize_t nbytes = ::write(m_iHandle, &frame, CANFD_MTU);
        if (nbytes != static_cast<ssize_t>(CANFD_MTU))
        {
            const int err = errno;
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("write(canfd_frame) failed, errno:"); LOG_INT(err));
            return Status::WRITE_ERROR;
        }
    }

    szBytesWritten = buffer.size();

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("TX id:"); LOG_HEX32(u32TxId);
              LOG_STRING(" len:"); LOG_UINT32(static_cast<uint32_t>(buffer.size())));

    return Status::SUCCESS;
}
