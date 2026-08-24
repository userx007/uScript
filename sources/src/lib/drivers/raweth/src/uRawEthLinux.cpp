#include "uRawEth.hpp"
#include "uLogger.hpp"

#include <cstring>
#include <cstdio>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netpacket/packet.h>
#include <net/ethernet.h>
#include <chrono>


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR   "RAWETH_DRV   |"
#define LOG_HDR  LOG_STRING(LT_HDR)


// ============================================================================
// OPEN / CLOSE
// ============================================================================

RawEth::Status RawEth::open(const std::string& strIfaceName,
                            const MacAddr& defaultDestMac,
                            uint16_t u16EtherType,
                            bool bPromiscuous)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (strIfaceName.empty() || strIfaceName.size() >= IFNAMSIZ)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Invalid parameter: empty or over-long interface name"));
        return Status::INVALID_PARAM;
    }

    const uint16_t u16Ethertype = (u16EtherType == 0) ? RAWETH_DEFAULT_ETHERTYPE : u16EtherType;

    // AF_PACKET/SOCK_RAW with the socket's protocol argument already narrowed
    // to our EtherType — the kernel filters everything else out for us
    // before it ever reaches recvfrom().
    const int iSock = ::socket(AF_PACKET, SOCK_RAW, htons(u16Ethertype));
    if (iSock < 0)
    {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("socket(AF_PACKET) failed, errno:"); LOG_INT(err);
                  LOG_STRING("(CAP_NET_RAW / root required)"));
        return Status::PORT_ACCESS;
    }

    struct ifreq sIfr = {};
    std::strncpy(sIfr.ifr_name, strIfaceName.c_str(), IFNAMSIZ - 1);

    // Resolve interface index.
    if (::ioctl(iSock, SIOCGIFINDEX, &sIfr) < 0)
    {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("SIOCGIFINDEX failed for "); LOG_STRING(strIfaceName.c_str());
                  LOG_STRING(", errno:"); LOG_INT(err));
        ::close(iSock);
        return Status::INVALID_PARAM;
    }
    const int iIfIndex = sIfr.ifr_ifindex;

    // Resolve our own MAC address (used as the source MAC on every write).
    std::memset(&sIfr, 0, sizeof(sIfr));
    std::strncpy(sIfr.ifr_name, strIfaceName.c_str(), IFNAMSIZ - 1);
    if (::ioctl(iSock, SIOCGIFHWADDR, &sIfr) < 0)
    {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("SIOCGIFHWADDR failed for "); LOG_STRING(strIfaceName.c_str());
                  LOG_STRING(", errno:"); LOG_INT(err));
        ::close(iSock);
        return Status::INVALID_PARAM;
    }
    MacAddr ownMac;
    std::memcpy(ownMac.data(), sIfr.ifr_hwaddr.sa_data, RAWETH_MAC_ADDR_LEN);

    // Bind the socket to this interface + EtherType so we never see traffic
    // from other interfaces sharing the same protocol family.
    struct sockaddr_ll sAddr = {};
    sAddr.sll_family   = AF_PACKET;
    sAddr.sll_protocol = htons(u16Ethertype);
    sAddr.sll_ifindex  = iIfIndex;

    if (::bind(iSock, reinterpret_cast<struct sockaddr*>(&sAddr), sizeof(sAddr)) < 0)
    {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("bind() failed for "); LOG_STRING(strIfaceName.c_str());
                  LOG_STRING(", errno:"); LOG_INT(err));
        ::close(iSock);
        return Status::PORT_ACCESS;
    }

    bool bPromiscSetByUs = false;
    if (bPromiscuous)
    {
        struct packet_mreq sMreq = {};
        sMreq.mr_ifindex = iIfIndex;
        sMreq.mr_type    = PACKET_MR_PROMISC;

        if (::setsockopt(iSock, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &sMreq, sizeof(sMreq)) < 0)
        {
            // Not fatal — proceed without promiscuous mode.
            const int err = errno;
            LOG_PRINT(LOG_DEBUG, LOG_HDR;
                      LOG_STRING("Failed to enable promiscuous mode, errno:"); LOG_INT(err));
        }
        else
        {
            bPromiscSetByUs = true;
        }
    }

    m_iHandle         = iSock;
    m_iIfIndex        = iIfIndex;
    m_ownMac          = ownMac;
    m_defaultDestMac  = defaultDestMac;
    m_u16EtherType    = u16Ethertype;
    m_bPromiscSetByUs = bPromiscSetByUs;

    char szMacBuf[18];
    std::snprintf(szMacBuf, sizeof(szMacBuf), "%02X:%02X:%02X:%02X:%02X:%02X",
                  m_ownMac[0], m_ownMac[1], m_ownMac[2], m_ownMac[3], m_ownMac[4], m_ownMac[5]);

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("Bound to "); LOG_STRING(strIfaceName.c_str());
              LOG_STRING(", own MAC:"); LOG_STRING(szMacBuf);
              LOG_STRING(", EtherType:"); LOG_INT(static_cast<int>(u16Ethertype));
              LOG_STRING(", handle:"); LOG_INT(m_iHandle));

    return Status::SUCCESS;
}


RawEth::Status RawEth::close()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_iHandle >= 0)
    {
        if (m_bPromiscSetByUs)
        {
            struct packet_mreq sMreq = {};
            sMreq.mr_ifindex = m_iIfIndex;
            sMreq.mr_type    = PACKET_MR_PROMISC;
            ::setsockopt(m_iHandle, SOL_PACKET, PACKET_DROP_MEMBERSHIP, &sMreq, sizeof(sMreq));
            m_bPromiscSetByUs = false;
        }

        ::close(m_iHandle);
        LOG_PRINT(LOG_DEBUG, LOG_HDR;
                  LOG_STRING("Socket closed, handle:"); LOG_INT(m_iHandle));
        m_iHandle  = -1;
        m_iIfIndex = -1;
        m_ownMac   = {};
    }

    return Status::SUCCESS;
}


RawEth::MacAddr RawEth::local_mac() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_ownMac;
}


// ============================================================================
// INTERNAL READ PRIMITIVE
// One poll(2) + recvfrom(2) pair. Strips the 14-byte L2 header and copies up
// to buffer.size() bytes of the payload into buffer. szBytesRead is set to
// the number of payload bytes actually copied.
// ============================================================================

RawEth::Status RawEth::timeout_read(uint32_t u32ReadTimeout,
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

    if (sPollFd.revents & (POLLERR | POLLHUP))
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("timeout_read: socket error or interface went down"));
        return Status::READ_ERROR;
    }

    // Read the whole frame (header + payload) into a scratch buffer sized
    // for the largest possible non-jumbo frame, then split header/payload.
    uint8_t frame[RAWETH_MAX_FRAME_LEN];
    const ssize_t nbytes = ::recvfrom(m_iHandle, frame, sizeof(frame), 0, nullptr, nullptr);
    if (nbytes < 0)
    {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("recvfrom() failed, errno:"); LOG_INT(err));
        return Status::READ_ERROR;
    }
    if (static_cast<size_t>(nbytes) < RAWETH_ETH_HEADER_LEN)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("timeout_read: runt frame smaller than the L2 header"));
        return Status::READ_ERROR;
    }

    const size_t szPayloadLen = static_cast<size_t>(nbytes) - RAWETH_ETH_HEADER_LEN;
    const size_t szCopyLen    = std::min(szPayloadLen, buffer.size());

    if (szCopyLen < szPayloadLen)
    {
        // Same trade-off the CAN driver makes: excess payload bytes beyond
        // the caller's buffer are discarded, not carried over to the next call.
        LOG_PRINT(LOG_DEBUG, LOG_HDR;
                  LOG_STRING("timeout_read: frame payload larger than buffer, truncating"));
    }

    std::memcpy(buffer.data(), frame + RAWETH_ETH_HEADER_LEN, szCopyLen);
    szBytesRead = szCopyLen;

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("RX frame payload bytes:"); LOG_UINT32(static_cast<uint32_t>(szBytesRead)));

    return Status::SUCCESS;
}


// ============================================================================
// INTERNAL WRITE PRIMITIVE
// Builds one frame (dest MAC + own MAC + EtherType + payload) and sends it as
// a single sendto(2), bounded by poll(2) and retried on EAGAIN until the
// overall write timeout elapses. A raw Ethernet write is one atomic
// datagram, not a byte range that can be short-written the way a TCP send
// can, so there is no partial-progress loop here — only a bounded retry of
// the whole frame.
// ============================================================================

RawEth::Status RawEth::timeout_write(uint32_t u32WriteTimeout,
                                     std::span<const uint8_t> buffer,
                                     const MacAddr& destMac,
                                     uint16_t u16EtherType,
                                     size_t& szBytesWritten) const
{
    if (buffer.empty() || buffer.size() > RAWETH_MAX_PAYLOAD)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("timeout_write: invalid parameter (empty or over MTU)"));
        return Status::INVALID_PARAM;
    }

    szBytesWritten = 0;

    uint8_t frame[RAWETH_MAX_FRAME_LEN];
    std::memcpy(frame, destMac.data(), RAWETH_MAC_ADDR_LEN);
    std::memcpy(frame + RAWETH_MAC_ADDR_LEN, m_ownMac.data(), RAWETH_MAC_ADDR_LEN);
    const uint16_t u16NetEtherType = htons(u16EtherType);
    std::memcpy(frame + (2 * RAWETH_MAC_ADDR_LEN), &u16NetEtherType, sizeof(u16NetEtherType));
    std::memcpy(frame + RAWETH_ETH_HEADER_LEN, buffer.data(), buffer.size());
    const size_t szFrameLen = RAWETH_ETH_HEADER_LEN + buffer.size();

    struct sockaddr_ll sAddr = {};
    sAddr.sll_family   = AF_PACKET;
    sAddr.sll_ifindex  = m_iIfIndex;
    sAddr.sll_halen    = RAWETH_MAC_ADDR_LEN;
    sAddr.sll_protocol = u16NetEtherType;
    std::memcpy(sAddr.sll_addr, destMac.data(), RAWETH_MAC_ADDR_LEN);

    // 0 == infinite timeout: never time out the overall write, and block
    // indefinitely on each POLLOUT wait.
    const bool bInfinite = (u32WriteTimeout == 0);
    const auto tDeadline = std::chrono::steady_clock::now() +
                           std::chrono::milliseconds(u32WriteTimeout);

    while (true)
    {
        const auto tNow = std::chrono::steady_clock::now();
        if (!bInfinite && tNow >= tDeadline)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("timeout_write: overall timeout elapsed"));
            return Status::WRITE_TIMEOUT;
        }

        int iPollTimeout = -1;
        if (!bInfinite)
        {
            const auto remainingMs =
                std::chrono::duration_cast<std::chrono::milliseconds>(tDeadline - tNow).count();
            iPollTimeout = static_cast<int>(remainingMs);
        }

        struct pollfd sPollFd;
        sPollFd.fd      = m_iHandle;
        sPollFd.events  = POLLOUT;
        sPollFd.revents = 0;

        const int iPollResult = ::poll(&sPollFd, 1, iPollTimeout);
        if (iPollResult < 0)
        {
            const int err = errno;
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("poll() failed"); LOG_INT(err));
            return Status::WRITE_ERROR;
        }
        else if (iPollResult == 0)
        {
            return Status::WRITE_TIMEOUT;
        }

        if (sPollFd.revents & (POLLERR | POLLHUP))
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("timeout_write: socket error or interface went down"));
            return Status::WRITE_ERROR;
        }

        const ssize_t nbytes = ::sendto(m_iHandle, frame, szFrameLen, 0,
                                        reinterpret_cast<struct sockaddr*>(&sAddr), sizeof(sAddr));
        if (nbytes < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                continue; // TX queue transiently full — re-poll for POLLOUT.
            }
            const int err = errno;
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("sendto() failed, errno:"); LOG_INT(err));
            return Status::WRITE_ERROR;
        }

        // A raw Ethernet sendto() is all-or-nothing; a short count here
        // would indicate something has gone wrong at the driver/NIC level.
        if (static_cast<size_t>(nbytes) != szFrameLen)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("timeout_write: short frame send, expected/actual:");
                      LOG_UINT32(static_cast<uint32_t>(szFrameLen));
                      LOG_UINT32(static_cast<uint32_t>(nbytes)));
            return Status::WRITE_ERROR;
        }

        szBytesWritten = buffer.size();
        break;
    }

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("TX frame payload bytes:"); LOG_UINT32(static_cast<uint32_t>(szBytesWritten)));

    return Status::SUCCESS;
}
