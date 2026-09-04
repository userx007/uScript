#include "uSystecCan.hpp"
#include "uLogger.hpp"

#include <cstring>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <fstream>
#include <net/if.h>              // if_nametoindex, ifreq
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>           // can_frame, CAN_RAW, CAN_MTU …
#include <linux/can/raw.h>       // SOL_CAN_RAW, CAN_RAW_FILTER


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR   "SYSTEC_DRV  |"
#define LOG_HDR  LOG_STRING(LT_HDR)


// ============================================================================
// OPEN / CLOSE
// ============================================================================

SYSTECCAN::Status SYSTECCAN::open(const std::string& strIface)
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

    // NOTE: unlike uKVCan, CAN_RAW_FD_FRAMES is deliberately NOT enabled here.
    // systec_can.ko never sets CAN_CTRLMODE_FD (see systec_can_probe() /
    // ctrlmode_supported in systec_can.c) and never raises netdev->mtu past
    // CAN_MTU, so a USB-CANmodul channel can only ever exchange classic
    // struct can_frame — enabling the sockopt here would just as noop, and
    // omitting it keeps this driver honest about the hardware's real limits
    // (see CAN_DRV_MAX_DLEN = 8 in uSystecCan.hpp).

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
    // particular to a vcan_mirror-style loopback app, which will echo them
    // back. That echo arrives here as a foreign frame (sent by a different
    // socket) and is therefore received normally, which is exactly what we
    // want.
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
              LOG_STRING("SYSTEC CAN socket opened on "); LOG_STRING(strIface.c_str());
              LOG_STRING(", handle:"); LOG_INT(m_iHandle));

    m_vFilters.clear(); // a freshly bound socket has no filters installed yet

    return Status::SUCCESS;
}


SYSTECCAN::Status SYSTECCAN::close()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_iHandle >= 0)
    {
        ::close(m_iHandle);
        LOG_PRINT(LOG_DEBUG, LOG_HDR;
                  LOG_STRING("SYSTEC CAN socket closed, handle:"); LOG_INT(m_iHandle));
        m_iHandle = -1;
    }

    m_vFilters.clear();

    return Status::SUCCESS;
}


// ============================================================================
// FILTER CONFIGURATION
// ============================================================================

SYSTECCAN::Status SYSTECCAN::set_filters(const std::vector<CanFilter>& filters)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_iHandle < 0)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("set_filters: socket not open"));
        return Status::PORT_ACCESS;
    }

    if (filters.empty())
    {
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
        acceptAllFilter.can_id  = 0;
        acceptAllFilter.can_mask = 0;

        if (::setsockopt(m_iHandle, SOL_CAN_RAW, CAN_RAW_FILTER,
                         &acceptAllFilter, sizeof(acceptAllFilter)) < 0)
        {
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
              LOG_STRING("SYSTEC CAN filters set, count:"); LOG_UINT32(static_cast<uint32_t>(filters.size())));

    return Status::SUCCESS;
}


// ============================================================================
// INTERNAL READ PRIMITIVE
// Receives one classic CAN frame and copies its payload into buffer.
// bytes_read is set to the actual DLC field of the received frame.
// (systec_can.ko is classic-CAN only — see uSystecCan.hpp class docs.)
// ============================================================================

SYSTECCAN::Status SYSTECCAN::timeout_read(uint32_t u32ReadTimeout,
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

    // 0 == infinite timeout: block until a frame is available.
    const int iPollTimeout = (u32ReadTimeout == 0) ? -1 : static_cast<int>(u32ReadTimeout);

    const int iPollResult = ::poll(&sPollFd, 1, iPollTimeout);
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

    struct can_frame frame = {};
    const ssize_t nbytes = ::read(m_iHandle, &frame, sizeof(frame));

    if (nbytes != static_cast<ssize_t>(CAN_MTU))
    {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("read() returned "); LOG_INT(static_cast<int>(nbytes));
                  LOG_STRING(" errno:"); LOG_INT(err));
        return Status::READ_ERROR;
    }

    // Determine actual payload length from the frame header.
    const size_t payloadLen = static_cast<size_t>(frame.can_dlc);

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
// Packs buffer into a classic CAN frame payload and transmits it.
// u32TxId is the resolved CAN ID (already chosen by the caller from either
// the xtra_params override or the default m_u32TxId).
// ============================================================================

SYSTECCAN::Status SYSTECCAN::timeout_write(uint32_t /*u32WriteTimeout*/,
                               std::span<const uint8_t> buffer,
                               size_t& szBytesWritten,
                               uint32_t u32TxId) const
{
    if (buffer.empty() || buffer.size() > CAN_DRV_MAX_DLEN)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Invalid parameter: buffer empty or exceeds CAN_DRV_MAX_DLEN (8 — classic CAN only)"));
        return Status::INVALID_PARAM;
    }

    szBytesWritten = 0;

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

    szBytesWritten = buffer.size();

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("TX id:"); LOG_HEX32(u32TxId);
              LOG_STRING(" len:"); LOG_UINT32(static_cast<uint32_t>(buffer.size())));

    return Status::SUCCESS;
}


// ============================================================================
// SYS TEC HARDWARE EXTRAS — plain sysfs file I/O, no socket required.
//
// Mirrors the attribute groups systec_can.ko registers (see systec_can.c):
//   systec_can_device_sysfs_attr_group    -> sysfs_create_group(&intf->dev.kobj, …)
//     reachable from a netdev via its "device" symlink (SET_NETDEV_DEV()
//     points the netdev's parent at the USB interface device), i.e.
//     /sys/class/net/<iface>/device/{devicenr,reset,dual_channel,
//                                     status_timeout,high_performance}
//   systec_can_interface_sysfs_attr_group -> sysfs_create_group(&netdev->dev.kobj, …)
//     i.e. directly under /sys/class/net/<iface>/{channel,tx_timeout_ms}
// ============================================================================

namespace {

std::string sysfs_device_path(const std::string& strIface, const char* pszAttr)
{
    return "/sys/class/net/" + strIface + "/device/" + pszAttr;
}

std::string sysfs_iface_path(const std::string& strIface, const char* pszAttr)
{
    return "/sys/class/net/" + strIface + "/" + pszAttr;
}

/** @brief Read a sysfs attribute file and parse it as an unsigned integer (0=auto base). */
SYSTECCAN::Status sysfs_read_uint(const std::string& strPath, uint32_t& u32Out)
{
    std::ifstream file(strPath);
    if (!file.is_open())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("sysfs read: cannot open"); LOG_STRING(strPath.c_str()));
        return SYSTECCAN::Status::PORT_ACCESS;
    }

    unsigned long ulValue = 0;
    file >> ulValue;
    if (file.fail())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("sysfs read: malformed value in"); LOG_STRING(strPath.c_str()));
        return SYSTECCAN::Status::READ_ERROR;
    }

    u32Out = static_cast<uint32_t>(ulValue);
    return SYSTECCAN::Status::SUCCESS;
}

/** @brief Write an unsigned integer (decimal) to a sysfs attribute file. */
SYSTECCAN::Status sysfs_write_uint(const std::string& strPath, uint32_t u32Value)
{
    std::ofstream file(strPath);
    if (!file.is_open())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("sysfs write: cannot open"); LOG_STRING(strPath.c_str()));
        return SYSTECCAN::Status::PORT_ACCESS;
    }

    file << u32Value;
    file.flush();
    if (file.fail())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("sysfs write: failed for"); LOG_STRING(strPath.c_str()));
        return SYSTECCAN::Status::WRITE_ERROR;
    }

    return SYSTECCAN::Status::SUCCESS;
}

} // anonymous namespace


SYSTECCAN::Status SYSTECCAN::hw_get_devicenr(const std::string& strIface, uint32_t& u32DeviceNr)
{
    return sysfs_read_uint(sysfs_device_path(strIface, "devicenr"), u32DeviceNr);
}

SYSTECCAN::Status SYSTECCAN::hw_set_devicenr(const std::string& strIface, uint32_t u32DeviceNr)
{
    // Kernel side validates 0-254 too (see systec_can_sysfs_set_devicenr());
    // checked here as well so the caller gets INVALID_PARAM rather than a
    // generic write failure.
    if (u32DeviceNr > 254)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("hw_set_devicenr: value out of range [0-254]"));
        return Status::INVALID_PARAM;
    }
    return sysfs_write_uint(sysfs_device_path(strIface, "devicenr"), u32DeviceNr);
}

SYSTECCAN::Status SYSTECCAN::hw_reset(const std::string& strIface)
{
    // Write-only trigger — any write causes systec_can.ko to issue
    // USBCAN_CMD_RESET_HW to the device (see systec_can_sysfs_set_reset()).
    return sysfs_write_uint(sysfs_device_path(strIface, "reset"), 1);
}

SYSTECCAN::Status SYSTECCAN::hw_get_dual_channel(const std::string& strIface, bool& bDualChannel)
{
    uint32_t u32Val = 0;
    const Status eStatus = sysfs_read_uint(sysfs_device_path(strIface, "dual_channel"), u32Val);
    if (eStatus == Status::SUCCESS)
    {
        bDualChannel = (u32Val != 0);
    }
    return eStatus;
}

SYSTECCAN::Status SYSTECCAN::hw_get_status_timeout(const std::string& strIface, uint32_t& u32TimeoutMs)
{
    return sysfs_read_uint(sysfs_device_path(strIface, "status_timeout"), u32TimeoutMs);
}

SYSTECCAN::Status SYSTECCAN::hw_set_status_timeout(const std::string& strIface, uint32_t u32TimeoutMs)
{
    return sysfs_write_uint(sysfs_device_path(strIface, "status_timeout"), u32TimeoutMs);
}

SYSTECCAN::Status SYSTECCAN::hw_get_high_performance(const std::string& strIface, bool& bHighPerformance)
{
    uint32_t u32Val = 0;
    const Status eStatus = sysfs_read_uint(sysfs_device_path(strIface, "high_performance"), u32Val);
    if (eStatus == Status::SUCCESS)
    {
        bHighPerformance = (u32Val != 0);
    }
    return eStatus;
}

SYSTECCAN::Status SYSTECCAN::hw_set_high_performance(const std::string& strIface, bool bHighPerformance)
{
    return sysfs_write_uint(sysfs_device_path(strIface, "high_performance"), bHighPerformance ? 1u : 0u);
}

SYSTECCAN::Status SYSTECCAN::hw_get_channel(const std::string& strIface, uint32_t& u32ChanNo)
{
    return sysfs_read_uint(sysfs_iface_path(strIface, "channel"), u32ChanNo);
}

SYSTECCAN::Status SYSTECCAN::hw_get_tx_timeout_ms(const std::string& strIface, uint32_t& u32TimeoutMs)
{
    // Dual-channel units only — systec_can_sysfs_show_tx_timeout_ms() returns
    // -ENOSYS (surfaced here as a read failure) on single-channel units.
    return sysfs_read_uint(sysfs_iface_path(strIface, "tx_timeout_ms"), u32TimeoutMs);
}

SYSTECCAN::Status SYSTECCAN::hw_set_tx_timeout_ms(const std::string& strIface, uint32_t u32TimeoutMs)
{
    return sysfs_write_uint(sysfs_iface_path(strIface, "tx_timeout_ms"), u32TimeoutMs);
}
