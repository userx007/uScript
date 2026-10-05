#include "uKCan.hpp"
#include "uLogger.hpp"

#include <algorithm>
#include <chrono>
#include <compare>
#include <cstring>
#include <errno.h>
#include <linux/can.h>     // can_frame, canfd_frame, CAN_RAW, CAN_MTU …
#include <linux/can/error.h> // CAN_ERR_* classes
#include <linux/can/raw.h> // SOL_CAN_RAW, CAN_RAW_FILTER, CAN_RAW_FD_FRAMES, CAN_RAW_ERR_FILTER
#include <mutex>
#include <net/if.h> // if_nametoindex, ifreq
#include <poll.h>
#include <span>
#include <stdint.h>
#include <stop_token>
#include <string>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

/////////////////////////////////////////////////////////////////////////////////
//                            LOG DEFINITIONS                                  //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
#undef LT_HDR
#endif
#ifdef LOG_HDR
#undef LOG_HDR
#endif

#define LT_HDR  "KCAN_DRV   |"
#define LOG_HDR LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                            IMPLEMENTATION                                   //
/////////////////////////////////////////////////////////////////////////////////

namespace {
    /** @brief Human readable list of the CAN_ERR_* classes set in an error frame's can_id. */
    std::string describe_error_class(canid_t id)
    {
        static const struct {
                canid_t bit;
                const char *name;
        } kClasses[] = {
            {CAN_ERR_TX_TIMEOUT, "TX-TIMEOUT"}, {CAN_ERR_LOSTARB, "LOST-ARBITRATION"}, {CAN_ERR_CRTL, "CONTROLLER"},
            {CAN_ERR_PROT, "PROTOCOL"},         {CAN_ERR_TRX, "TRANSCEIVER"},          {CAN_ERR_ACK, "NO-ACK"},
            {CAN_ERR_BUSOFF, "BUS-OFF"},        {CAN_ERR_BUSERROR, "BUS-ERROR"},       {CAN_ERR_RESTARTED, "RESTARTED"},
        };
        std::string str;
        for (const auto &c : kClasses) {
            if (id & c.bit) {
                str += str.empty() ? "" : ",";
                str += c.name;
            }
        }
        return str.empty() ? std::string("UNKNOWN") : str;
    }
} // namespace

// ============================================================================
// OPEN / CLOSE
// ============================================================================

KCAN::Status KCAN::open(const std::string &strIface)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (strIface.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Invalid parameter: empty interface name"));
        return Status::INVALID_PARAM;
    }

    // Re-open on a handle that is already open: do not leak the old socket.
    if (m_iHandle >= 0) {
        ::close(m_iHandle);
        m_iHandle = -1;
    }
    m_strIface.clear();
    m_u32ErrMask = 0;

    // Create a raw SocketCAN socket.
    m_iHandle = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (m_iHandle < 0) {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("socket(PF_CAN) failed, errno:"); LOG_INT(err));
        return Status::PORT_ACCESS;
    }

    // Resolve the interface name to an index.
    const unsigned int ifIdx = ::if_nametoindex(strIface.c_str());
    if (ifIdx == 0) {
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
                     &canfd_on, sizeof(canfd_on)) < 0) {
        // Not fatal — the interface may not support CAN FD.
        LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                  LOG_STRING("CAN FD not supported on "); LOG_STRING(strIface.c_str());
                  LOG_STRING(", falling back to classic CAN"));
    }

    // Bind the socket to the interface.
    struct sockaddr_can addr = {};
    addr.can_family          = AF_CAN;
    addr.can_ifindex         = static_cast<int>(ifIdx);

    if (::bind(m_iHandle,
               reinterpret_cast<struct sockaddr *>(&addr),
               sizeof(addr)) < 0) {
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
    // delivers our TX frames to every OTHER socket on this host (candump,
    // other plugin instances, loopback test apps ...), which is what a
    // bus-monitoring tool running next to the script expects to see.
    //
    // CAN_RAW_RECV_OWN_MSGS = 0 excludes THIS socket from receiving its own
    // transmissions. Without it every frame we send would show up in our own
    // RX queue and be mistaken for a reply from the bus.
    int recv_own = 0;
    if (::setsockopt(m_iHandle, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS,
                     &recv_own, sizeof(recv_own)) < 0) {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("CAN_RAW_RECV_OWN_MSGS=0 failed, errno:"); LOG_INT(err));
        ::close(m_iHandle);
        m_iHandle = -1;
        return Status::PORT_ACCESS;
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("KCAN socket opened on "); LOG_STRING(strIface.c_str());
              LOG_STRING(", handle:"); LOG_INT(m_iHandle));

    m_strIface = strIface;
    m_vFilters.clear(); // a freshly bound socket has no filters installed yet

    return Status::SUCCESS;
}

KCAN::Status KCAN::close()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_iHandle >= 0) {
        ::close(m_iHandle);
        LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                  LOG_STRING("KCAN socket closed, handle:"); LOG_INT(m_iHandle));
        m_iHandle = -1;
    }

    m_strIface.clear();
    m_u32ErrMask = 0;
    m_vFilters.clear();

    return Status::SUCCESS;
}

// ============================================================================
// FILTER CONFIGURATION
// ============================================================================

KCAN::Status KCAN::set_filters(const std::vector<CanFilter> &vFilters)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_iHandle < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("set_filters: socket not open"));
        return Status::PORT_ACCESS;
    }

    if (vFilters.empty()) {
        // Accept everything.
        //
        // A 0-length CAN_RAW_FILTER list does NOT mean "no filtering" in
        // SocketCAN — the kernel registers one internal receiver per filter
        // entry, so passing zero entries deregisters all of them and the
        // socket stops receiving ANY frame. The only truly permissive state
        // is a freshly bound socket that has never had CAN_RAW_FILTER set at
        // all. Since we may be asked to restore "accept all" on a socket
        // that has already been touched (e.g. after a transient per-call
        // filter), the only reliable way back is to install an explicit
        // filter that matches every id: can_id = 0, can_mask = 0, because
        // (frame_id & 0) == (0 & 0) is always true.
        struct can_filter acceptAllFilter = {};
        acceptAllFilter.can_id            = 0;
        acceptAllFilter.can_mask          = 0;

        if (::setsockopt(m_iHandle, SOL_CAN_RAW, CAN_RAW_FILTER,
                         &acceptAllFilter, sizeof(acceptAllFilter)) < 0) {
            const int err = errno;
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("setsockopt(CAN_RAW_FILTER, accept-all) failed, errno:");
                      LOG_INT(err));
            return Status::PORT_ACCESS;
        }
        m_vFilters.clear(); // mirror cleared kernel state so tout_read()'s
                            // transient-filter snapshot/restore stays accurate;
                            // "empty" is our own convention meaning accept-all
        return Status::SUCCESS;
    }

    // Convert to kernel struct can_filter array.
    std::vector<struct can_filter> kFilters;
    kFilters.reserve(vFilters.size());
    for (const auto &f : vFilters) {
        struct can_filter kf = {};
        kf.can_id            = f.can_id;
        kf.can_mask          = f.can_mask;
        kFilters.push_back(kf);
    }

    if (::setsockopt(m_iHandle, SOL_CAN_RAW, CAN_RAW_FILTER,
                     kFilters.data(),
                     static_cast<socklen_t>(kFilters.size() * sizeof(struct can_filter))) < 0) {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("setsockopt(CAN_RAW_FILTER) failed, errno:"); LOG_INT(err));
        return Status::PORT_ACCESS;
    }

    m_vFilters = vFilters; // mirror applied kernel state so tout_read()'s
                          // transient-filter snapshot/restore stays accurate

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("KCAN filters set, count:"); LOG_UINT32(static_cast<uint32_t>(vFilters.size())));

    return Status::SUCCESS;
}

// ============================================================================
// ERROR-FRAME CONFIGURATION
// ============================================================================

KCAN::Status KCAN::set_error_mask(uint32_t u32Mask)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_iHandle < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("set_error_mask: socket not open"));
        return Status::PORT_ACCESS;
    }

    const can_err_mask_t kernelMask = static_cast<can_err_mask_t>(u32Mask & CAN_ERR_MASK);
    if (::setsockopt(m_iHandle, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &kernelMask, sizeof(kernelMask)) < 0) {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("setsockopt(CAN_RAW_ERR_FILTER) failed, errno:"); LOG_INT(err));
        return Status::PORT_ACCESS;
    }

    m_u32ErrMask = static_cast<uint32_t>(kernelMask);
    return Status::SUCCESS;
}

// ============================================================================
// INTERNAL READ PRIMITIVE
// Receives one KCAN / CAN FD frame and copies its payload into buffer.
// bytes_read is set to the actual DLC / len field of the received frame.
// ============================================================================

KCAN::Status KCAN::timeout_read(uint32_t u32ReadTimeout,
                                std::span<uint8_t> buffer,
                                size_t &szBytesRead,
                                std::stop_token stop_tok) const
{
    if (buffer.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("timeout_read: invalid parameter"));
        return Status::INVALID_PARAM;
    }

    szBytesRead = 0;

    struct pollfd sPollFd;
    sPollFd.fd      = m_iHandle;
    sPollFd.events  = POLLIN;
    sPollFd.revents = 0;

    // 0 == infinite timeout: never expire the wait ourselves. Either way,
    // poll in bounded slices so a stop request can be observed promptly.
    constexpr int kPollSliceMs = 200;
    const bool bInfinite       = (u32ReadTimeout == 0);
    const auto tDeadline       = std::chrono::steady_clock::now() + std::chrono::milliseconds(u32ReadTimeout);

    while (true) {
        if (stop_tok.stop_requested()) {
            return Status::READ_TIMEOUT;
        }

        int iSliceMs = kPollSliceMs;
        if (!bInfinite) {
            const auto remaining = tDeadline - std::chrono::steady_clock::now();
            if (remaining <= std::chrono::milliseconds(0)) {
                return Status::READ_TIMEOUT;
            }
            iSliceMs = static_cast<int>(std::min<int64_t>(kPollSliceMs,
                                                          std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count()));
        }

        const int iPollResult = ::poll(&sPollFd, 1, iSliceMs);
        if (iPollResult < 0) {
            const int err = errno;
            if (err == EINTR) {
                continue;
            }
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("poll() failed"); LOG_INT(err));
            return Status::READ_ERROR;
        }
        if (iPollResult == 0) {
            continue; // slice expired, re-evaluate stop request / deadline
        }
        if (sPollFd.revents & (POLLERR | POLLNVAL)) {
            // A real adapter that is unplugged / taken down while we wait ends up here.
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("socket error while waiting for a frame on "); LOG_STRING(m_strIface.c_str());
                      LOG_STRING("(interface gone or down?)"));
            return Status::READ_ERROR;
        }

        // A classic frame arrives as CAN_MTU bytes, an FD frame as CANFD_MTU bytes.
        struct canfd_frame frame = {};
        const ssize_t nbytes     = ::read(m_iHandle, &frame, sizeof(frame));

        if (nbytes < static_cast<ssize_t>(CAN_MTU)) {
            const int err = errno;
            if (err == EINTR || err == EAGAIN) {
                continue;
            }
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("read() returned "); LOG_INT(static_cast<int>(nbytes));
                      LOG_STRING(" errno:"); LOG_INT(err));
            return Status::READ_ERROR;
        }

        // Error frames (only delivered after set_error_mask()) carry bus/controller
        // diagnostics, not data: report them and keep waiting for a data frame.
        if (frame.can_id & CAN_ERR_FLAG) {
            LOG_PRINT(LOG_WARNING, LOG_HDR;
                      LOG_STRING("CAN error frame on "); LOG_STRING(m_strIface.c_str());
                      LOG_STRING("class:"); LOG_HEX32(frame.can_id & CAN_ERR_MASK);
                      LOG_STRING(describe_error_class(frame.can_id)));
            continue;
        }

        // Remote-transmission requests carry no payload.
        if (frame.can_id & CAN_RTR_FLAG) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                      LOG_STRING("RTR frame ignored, id:"); LOG_HEX32(frame.can_id & CAN_EFF_MASK));
            continue;
        }

        // Payload length: classic frames overlay can_dlc on the FD len field, and
        // may legally report a DLC of 9..15 meaning 8 bytes.
        size_t payloadLen = static_cast<size_t>(frame.len);
        if (static_cast<size_t>(nbytes) == CAN_MTU && payloadLen > CAN_MAX_DLEN) {
            payloadLen = CAN_MAX_DLEN;
        }
        payloadLen = std::min<size_t>(payloadLen, CANFD_MAX_DLEN);

        LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                  LOG_STRING("RX id:"); LOG_HEX32(frame.can_id);
                  LOG_STRING(" len:"); LOG_UINT32(static_cast<uint32_t>(payloadLen)));

        // Copy as many bytes as the caller's buffer can hold.
        const size_t copyLen = std::min(payloadLen, buffer.size());
        std::memcpy(buffer.data(), frame.data, copyLen);
        szBytesRead = copyLen;

        return Status::SUCCESS;
    }
}

// ============================================================================
// INTERNAL WRITE PRIMITIVE
// Packs buffer into a KCAN / CAN FD frame payload and transmits it.
// u32TxId is the resolved CAN ID (already chosen by the caller from either
// the xtra_params override or the default m_u32TxId).
// ============================================================================

KCAN::Status KCAN::timeout_write(uint32_t u32WriteTimeout,
                                 std::span<const uint8_t> buffer,
                                 size_t &szBytesWritten,
                                 uint32_t u32TxId,
                                 std::stop_token stop_tok) const
{
    if (buffer.empty() || buffer.size() > CAN_DRV_MAX_DLEN) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Invalid parameter: buffer empty or exceeds CAN_DRV_MAX_DLEN"));
        return Status::INVALID_PARAM;
    }

    szBytesWritten = 0;

    // Build the frame. Classic CAN: <= 8 bytes; CAN FD: 9..64 bytes.
    // An 8-byte-or-less payload always goes out as a classic frame, so it also
    // works on adapters / interfaces that are not in FD mode.
    struct can_frame sClassic = {};
    struct canfd_frame sFd    = {};
    const void *pvFrame       = nullptr;
    size_t szFrame            = 0;

    if (buffer.size() <= CAN_MAX_DLEN) {
        sClassic.can_id  = u32TxId;
        sClassic.can_dlc = static_cast<uint8_t>(buffer.size());
        std::memcpy(sClassic.data, buffer.data(), buffer.size());
        pvFrame = &sClassic;
        szFrame = CAN_MTU;
    } else {
        // CAN FD frames require a 29-bit extended ID field in the frame header.
        // CAN_EFF_FLAG is therefore OR-ed in unconditionally here, regardless of
        // whether u32TxId (from set_tx_id() or an xtra_params override) already
        // carries it.  Callers that intend a standard 11-bit ID on an FD-sized
        // payload must be aware that the frame will be transmitted as extended.
        sFd.can_id = u32TxId | CAN_EFF_FLAG;
        sFd.len    = static_cast<uint8_t>(buffer.size());
        sFd.flags  = 0;
        std::memcpy(sFd.data, buffer.data(), buffer.size());
        pvFrame = &sFd;
        szFrame = CANFD_MTU;
    }

    // On a real adapter the socket's send buffer / the device TX queue fills up when
    // nobody acknowledges our frames (no other node, wrong bitrate, bus-off, cable
    // off). A plain blocking write() would then hang forever, so transmit
    // non-blocking and wait for room for at most u32WriteTimeout (0 = until a stop
    // is requested).
    constexpr int kSliceMs = 100;
    const bool bInfinite   = (u32WriteTimeout == 0);
    const auto tDeadline   = std::chrono::steady_clock::now() + std::chrono::milliseconds(u32WriteTimeout);

    while (true) {
        if (stop_tok.stop_requested()) {
            return Status::WRITE_TIMEOUT;
        }

        const ssize_t nbytes = ::send(m_iHandle, pvFrame, szFrame, MSG_DONTWAIT);
        if (nbytes == static_cast<ssize_t>(szFrame)) {
            break;
        }

        const int err = errno;
        const bool bWouldBlock = (nbytes < 0) && (err == EAGAIN || err == EWOULDBLOCK || err == ENOBUFS);
        if (!bWouldBlock) {
            if (nbytes >= 0 || err != EINTR) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("send() failed on "); LOG_STRING(m_strIface.c_str());
                          LOG_STRING("errno:"); LOG_INT(err);
                          LOG_STRING(err == ENETDOWN ? "(interface is down - bring it up / set the bitrate first)" : ""));
                return Status::WRITE_ERROR;
            }
            continue; // EINTR
        }

        int iSliceMs = kSliceMs;
        if (!bInfinite) {
            const auto remaining = tDeadline - std::chrono::steady_clock::now();
            if (remaining <= std::chrono::milliseconds(0)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("TX queue of "); LOG_STRING(m_strIface.c_str());
                          LOG_STRING("stayed full for the whole write timeout (no ACK on the bus? wrong bitrate? bus-off?)"));
                return Status::WRITE_TIMEOUT;
            }
            iSliceMs = static_cast<int>(std::min<int64_t>(kSliceMs,
                                                          std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count()));
        }

        if (err == ENOBUFS) {
            // qdisc full: nothing to poll on, back off briefly.
            ::poll(nullptr, 0, std::min(iSliceMs, 2));
        } else {
            struct pollfd sPollFd = {m_iHandle, POLLOUT, 0};
            ::poll(&sPollFd, 1, iSliceMs);
        }
    }

    szBytesWritten = buffer.size();

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("TX id:"); LOG_HEX32(u32TxId);
              LOG_STRING(" len:"); LOG_UINT32(static_cast<uint32_t>(buffer.size())));

    return Status::SUCCESS;
}
