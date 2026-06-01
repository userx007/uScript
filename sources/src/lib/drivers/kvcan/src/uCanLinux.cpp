#include "uCan.hpp"
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

#define LT_HDR   "CAN_DRV     |"
#define LOG_HDR  LOG_STRING(LT_HDR)


// ============================================================================
// OPEN / CLOSE
// ============================================================================

CAN::Status CAN::open(const std::string& strIface)
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

    // Enable CAN FD frames so the socket can handle both classic (8-byte) and
    // FD (up to 64-byte) frames transparently.
    int canfd_on = 1;
    if (::setsockopt(m_iHandle, SOL_CAN_RAW, CAN_RAW_FD_FRAMES,
                     &canfd_on, sizeof(canfd_on)) < 0)
    {
        // Not fatal — the interface may not support CAN FD.
        LOG_PRINT(LOG_DEBUG, LOG_HDR;
                  LOG_STRING("CAN FD not supported on "); LOG_STRING(strIface.c_str());
                  LOG_STRING(", falling back to classic CAN"));
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

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("CAN socket opened on "); LOG_STRING(strIface.c_str());
              LOG_STRING(", handle:"); LOG_INT(m_iHandle));

    return Status::SUCCESS;
}


CAN::Status CAN::close()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_iHandle >= 0)
    {
        ::close(m_iHandle);
        LOG_PRINT(LOG_DEBUG, LOG_HDR;
                  LOG_STRING("CAN socket closed, handle:"); LOG_INT(m_iHandle));
        m_iHandle = -1;
    }

    return Status::SUCCESS;
}


// ============================================================================
// FILTER CONFIGURATION
// ============================================================================

CAN::Status CAN::set_filters(const std::vector<CanFilter>& filters)
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

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("CAN filters set, count:"); LOG_UINT32(static_cast<uint32_t>(filters.size())));

    return Status::SUCCESS;
}


// ============================================================================
// INTERNAL READ PRIMITIVE
// Receives one CAN / CAN FD frame and copies its payload into buffer.
// bytes_read is set to the actual DLC / len field of the received frame.
// ============================================================================

CAN::Status CAN::timeout_read(uint32_t u32ReadTimeout,
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

    // Try to receive a CAN FD frame first; fall back to classic can_frame size
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
              LOG_STRING("RX id:0x"); LOG_HEX32(frame.can_id);
              LOG_STRING(" len:"); LOG_UINT32(static_cast<uint32_t>(payloadLen)));

    // Copy as many bytes as the caller's buffer can hold.
    const size_t copyLen = std::min(payloadLen, buffer.size());
    std::memcpy(buffer.data(), frame.data, copyLen);
    szBytesRead = copyLen;

    return Status::SUCCESS;
}


// ============================================================================
// INTERNAL WRITE PRIMITIVE
// Packs buffer into a CAN / CAN FD frame payload and transmits it.
// ============================================================================

CAN::Status CAN::timeout_write(uint32_t /*u32WriteTimeout*/,
                               std::span<const uint8_t> buffer,
                               size_t& szBytesWritten) const
{
    if (buffer.empty() || buffer.size() > CAN_DRV_MAX_DLEN)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Invalid parameter: buffer empty or exceeds CAN_DRV_MAX_DLEN"));
        return Status::INVALID_PARAM;
    }

    szBytesWritten = 0;

    // Choose frame type based on payload size.
    // Classic CAN: ≤ 8 bytes; CAN FD: 9–64 bytes.
    if (buffer.size() <= CAN_MAX_DLEN)
    {
        struct can_frame frame = {};
        frame.can_id  = m_u32TxId;
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
        frame.can_id = m_u32TxId | CAN_EFF_FLAG; // FD frames typically use extended IDs
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
              LOG_STRING("TX id:0x"); LOG_HEX32(m_u32TxId);
              LOG_STRING(" len:"); LOG_UINT32(static_cast<uint32_t>(buffer.size())));

    return Status::SUCCESS;
}
