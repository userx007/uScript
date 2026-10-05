#include "uKCanLink.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <linux/can/netlink.h> // can_bittiming, can_ctrlmode, IFLA_CAN_*, CAN_CTRLMODE_*, CAN_STATE_*
#include <linux/netlink.h>
#include <linux/rtnetlink.h>   // RTM_NEWLINK / RTM_GETLINK, ifinfomsg, rtattr, IFLA_*
#include <net/if.h>            // if_nametoindex, IFF_UP, IFF_LOWER_UP
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

namespace kcan {

    // The public CtrlMode values are a copy of the kernel's so that the plugin
    // layer never has to include kernel headers. Make sure they never drift.
    static_assert(CTRLMODE_LOOPBACK == CAN_CTRLMODE_LOOPBACK);
    static_assert(CTRLMODE_LISTEN_ONLY == CAN_CTRLMODE_LISTENONLY);
    static_assert(CTRLMODE_TRIPLE_SAMPLING == CAN_CTRLMODE_3_SAMPLES);
    static_assert(CTRLMODE_ONE_SHOT == CAN_CTRLMODE_ONE_SHOT);
    static_assert(CTRLMODE_BERR_REPORTING == CAN_CTRLMODE_BERR_REPORTING);
    static_assert(CTRLMODE_FD == CAN_CTRLMODE_FD);
#ifdef CAN_CTRLMODE_PRESUME_ACK
    static_assert(CTRLMODE_PRESUME_ACK == CAN_CTRLMODE_PRESUME_ACK);
#endif
#ifdef CAN_CTRLMODE_FD_NON_ISO
    static_assert(CTRLMODE_FD_NON_ISO == CAN_CTRLMODE_FD_NON_ISO);
#endif

    namespace {

        constexpr int kNlTimeoutSec = 3;

        // IFF_LOWER_UP lives in <linux/if.h>, which clashes with <net/if.h>; use the stable ABI value.
        constexpr unsigned kIffLowerUp = 0x10000;

        // ---------------------------------------------------------------------
        // Minimal rtnetlink message builder
        // ---------------------------------------------------------------------
        class NlRequest {
            public:
                NlRequest(uint16_t u16Type, uint16_t u16Flags, int iIfIndex)
                {
                    std::memset(m_buf, 0, sizeof(m_buf));
                    nlmsghdr *h    = hdr();
                    h->nlmsg_len   = NLMSG_LENGTH(sizeof(ifinfomsg));
                    h->nlmsg_type  = u16Type;
                    h->nlmsg_flags = u16Flags;
                    ifi()->ifi_family = AF_UNSPEC;
                    ifi()->ifi_index  = iIfIndex;
                }

                nlmsghdr *hdr()
                {
                    return reinterpret_cast<nlmsghdr *>(m_buf);
                }

                ifinfomsg *ifi()
                {
                    return static_cast<ifinfomsg *>(NLMSG_DATA(hdr()));
                }

                bool ok() const
                {
                    return m_ok;
                }

                void addAttr(uint16_t u16Type, const void *pvData, size_t szLen)
                {
                    rtattr *a = reserve(u16Type, szLen);
                    if (a != nullptr && szLen > 0) {
                        std::memcpy(RTA_DATA(a), pvData, szLen);
                    }
                }

                template <typename T>
                void addValue(uint16_t u16Type, const T &tValue)
                {
                    addAttr(u16Type, &tValue, sizeof(T));
                }

                rtattr *nestBegin(uint16_t u16Type)
                {
                    return reserve(u16Type, 0);
                }

                void nestEnd(rtattr *pNest)
                {
                    if (pNest != nullptr) {
                        pNest->rta_len = static_cast<unsigned short>(tail() - reinterpret_cast<uint8_t *>(pNest));
                    }
                }

            private:
                uint8_t *tail()
                {
                    return m_buf + NLMSG_ALIGN(hdr()->nlmsg_len);
                }

                rtattr *reserve(uint16_t u16Type, size_t szLen)
                {
                    if (!m_ok || NLMSG_ALIGN(hdr()->nlmsg_len) + RTA_SPACE(szLen) > sizeof(m_buf)) {
                        m_ok = false;
                        return nullptr;
                    }
                    rtattr *a   = reinterpret_cast<rtattr *>(tail());
                    a->rta_type = u16Type;
                    a->rta_len  = static_cast<unsigned short>(RTA_LENGTH(szLen));
                    hdr()->nlmsg_len = NLMSG_ALIGN(hdr()->nlmsg_len) + RTA_ALIGN(a->rta_len);
                    return a;
                }

                alignas(NLMSG_ALIGNTO) uint8_t m_buf[2048];
                bool m_ok = true;
        };

        // ---------------------------------------------------------------------
        // Netlink socket
        // ---------------------------------------------------------------------
        class NlSocket {
            public:
                NlSocket()
                {
                    m_fd = ::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
                    if (m_fd < 0) {
                        m_err = errno;
                        return;
                    }
                    sockaddr_nl sAddr = {};
                    sAddr.nl_family   = AF_NETLINK;
                    if (::bind(m_fd, reinterpret_cast<sockaddr *>(&sAddr), sizeof(sAddr)) < 0) {
                        m_err = errno;
                        ::close(m_fd);
                        m_fd = -1;
                        return;
                    }
                    timeval sTv = {};
                    sTv.tv_sec  = kNlTimeoutSec;
                    ::setsockopt(m_fd, SOL_SOCKET, SO_RCVTIMEO, &sTv, sizeof(sTv));
                }

                ~NlSocket()
                {
                    if (m_fd >= 0) {
                        ::close(m_fd);
                    }
                }

                NlSocket(const NlSocket &)            = delete;
                NlSocket &operator=(const NlSocket &) = delete;

                /**
                 * Send @p req; wait for the kernel's answer.
                 *   pReply == nullptr : wait for the NLMSG_ERROR ack (set-requests, NLM_F_ACK)
                 *   pReply != nullptr : wait for the RTM_NEWLINK answer of an RTM_GETLINK
                 * @return 0 or errno
                 */
                int transact(NlRequest &req, std::vector<uint8_t> *pReply)
                {
                    if (m_fd < 0) {
                        return m_err != 0 ? m_err : EBADF;
                    }
                    if (!req.ok()) {
                        return EMSGSIZE;
                    }

                    nlmsghdr *h = req.hdr();
                    h->nlmsg_seq = ++m_seq;
                    h->nlmsg_pid = 0;

                    sockaddr_nl sKernel = {};
                    sKernel.nl_family   = AF_NETLINK;
                    if (::sendto(m_fd, h, h->nlmsg_len, 0, reinterpret_cast<sockaddr *>(&sKernel), sizeof(sKernel)) < 0) {
                        return errno;
                    }

                    std::vector<uint8_t> vBuf(32 * 1024);
                    while (true) {
                        const ssize_t nRead = ::recv(m_fd, vBuf.data(), vBuf.size(), 0);
                        if (nRead < 0) {
                            if (errno == EINTR) {
                                continue;
                            }
                            return (errno == EAGAIN || errno == EWOULDBLOCK) ? ETIMEDOUT : errno;
                        }

                        size_t szLeft = static_cast<size_t>(nRead);
                        for (const nlmsghdr *r = reinterpret_cast<const nlmsghdr *>(vBuf.data());
                             NLMSG_OK(r, szLeft); r = NLMSG_NEXT(r, szLeft)) {
                            if (r->nlmsg_seq != m_seq) {
                                continue; // stale / unrelated
                            }
                            if (r->nlmsg_type == NLMSG_ERROR) {
                                const nlmsgerr *e = static_cast<const nlmsgerr *>(NLMSG_DATA(r));
                                if (e->error == 0) {
                                    if (pReply == nullptr) {
                                        return 0; // ack
                                    }
                                    continue;
                                }
                                return -e->error;
                            }
                            if (pReply != nullptr && r->nlmsg_type == RTM_NEWLINK) {
                                pReply->assign(reinterpret_cast<const uint8_t *>(r),
                                               reinterpret_cast<const uint8_t *>(r) + r->nlmsg_len);
                                return 0;
                            }
                        }
                    }
                }

            private:
                int m_fd      = -1;
                int m_err     = 0;
                uint32_t m_seq = 0;
        };

        int ifindex_of(const std::string &strIface)
        {
            if (strIface.empty() || strIface.size() >= IFNAMSIZ) {
                return 0;
            }
            return static_cast<int>(::if_nametoindex(strIface.c_str()));
        }

        bool close_enough(uint32_t u32Actual, uint32_t u32Wanted, uint32_t u32TolPermille)
        {
            const uint64_t diff = (u32Actual > u32Wanted) ? (u32Actual - u32Wanted) : (u32Wanted - u32Actual);
            return diff * 1000ULL <= static_cast<uint64_t>(u32Wanted) * u32TolPermille;
        }

        void parse_can_data(const rtattr *pData, LinkStatus &s)
        {
            int iLen = static_cast<int>(RTA_PAYLOAD(pData));
            for (const rtattr *a = static_cast<const rtattr *>(RTA_DATA(pData)); RTA_OK(a, iLen); a = RTA_NEXT(a, iLen)) {
                const unsigned type = a->rta_type & NLA_TYPE_MASK;
                const size_t szLen  = RTA_PAYLOAD(a);
                switch (type) {
                case IFLA_CAN_BITTIMING:
                    if (szLen >= sizeof(can_bittiming)) {
                        can_bittiming bt;
                        std::memcpy(&bt, RTA_DATA(a), sizeof(bt));
                        s.bitrate     = bt.bitrate;
                        s.samplePoint = bt.sample_point;
                        s.tq = bt.tq;
                        s.propSeg = bt.prop_seg;
                        s.phaseSeg1 = bt.phase_seg1;
                        s.phaseSeg2 = bt.phase_seg2;
                        s.sjw = bt.sjw;
                        s.brp = bt.brp;
                    }
                    break;
                case IFLA_CAN_DATA_BITTIMING:
                    if (szLen >= sizeof(can_bittiming)) {
                        can_bittiming bt;
                        std::memcpy(&bt, RTA_DATA(a), sizeof(bt));
                        s.dataBitrate     = bt.bitrate;
                        s.dataSamplePoint = bt.sample_point;
                    }
                    break;
                case IFLA_CAN_CLOCK:
                    if (szLen >= sizeof(can_clock)) {
                        can_clock ck;
                        std::memcpy(&ck, RTA_DATA(a), sizeof(ck));
                        s.clockHz = ck.freq;
                    }
                    break;
                case IFLA_CAN_STATE:
                    if (szLen >= sizeof(uint32_t)) {
                        std::memcpy(&s.state, RTA_DATA(a), sizeof(uint32_t));
                        s.hasState = true;
                    }
                    break;
                case IFLA_CAN_CTRLMODE:
                    if (szLen >= sizeof(can_ctrlmode)) {
                        can_ctrlmode cm;
                        std::memcpy(&cm, RTA_DATA(a), sizeof(cm));
                        s.ctrlModeMask  = cm.mask;
                        s.ctrlModeFlags = cm.flags;
                    }
                    break;
                case IFLA_CAN_RESTART_MS:
                    if (szLen >= sizeof(uint32_t)) {
                        std::memcpy(&s.restartMs, RTA_DATA(a), sizeof(uint32_t));
                    }
                    break;
                case IFLA_CAN_BERR_COUNTER:
                    if (szLen >= sizeof(can_berr_counter)) {
                        can_berr_counter bc;
                        std::memcpy(&bc, RTA_DATA(a), sizeof(bc));
                        s.txErr    = bc.txerr;
                        s.rxErr    = bc.rxerr;
                        s.hasBerr  = true;
                    }
                    break;
                default:
                    break;
                }
            }
        }

        void parse_linkinfo(const rtattr *pInfo, LinkStatus &s)
        {
            const rtattr *pData = nullptr;
            int iLen            = static_cast<int>(RTA_PAYLOAD(pInfo));
            for (const rtattr *a = static_cast<const rtattr *>(RTA_DATA(pInfo)); RTA_OK(a, iLen); a = RTA_NEXT(a, iLen)) {
                const unsigned type = a->rta_type & NLA_TYPE_MASK;
                if (type == IFLA_INFO_KIND) {
                    s.kind.assign(static_cast<const char *>(RTA_DATA(a)), ::strnlen(static_cast<const char *>(RTA_DATA(a)), RTA_PAYLOAD(a)));
                } else if (type == IFLA_INFO_DATA) {
                    pData = a;
                }
            }
            s.isCan = (s.kind == "can");
            if (s.isCan && pData != nullptr) {
                parse_can_data(pData, s);
            }
        }

    } // anonymous namespace

    // =========================================================================
    // Public API
    // =========================================================================

    int link_get_status(const std::string &strIface, LinkStatus &sStatus)
    {
        sStatus = LinkStatus{};

        const int iIdx = ifindex_of(strIface);
        if (iIdx == 0) {
            return ENODEV;
        }

        NlSocket sSock;
        NlRequest req(RTM_GETLINK, NLM_F_REQUEST, iIdx);
        std::vector<uint8_t> vReply;
        if (const int rc = sSock.transact(req, &vReply); rc != 0) {
            return rc;
        }

        const nlmsghdr *h   = reinterpret_cast<const nlmsghdr *>(vReply.data());
        const ifinfomsg *fi = static_cast<const ifinfomsg *>(NLMSG_DATA(h));

        sStatus.exists  = true;
        sStatus.up      = (fi->ifi_flags & IFF_UP) != 0;
        sStatus.lowerUp = (fi->ifi_flags & kIffLowerUp) != 0;

        int iLen = static_cast<int>(IFLA_PAYLOAD(h));
        for (const rtattr *a = IFLA_RTA(fi); RTA_OK(a, iLen); a = RTA_NEXT(a, iLen)) {
            const unsigned type = a->rta_type & NLA_TYPE_MASK;
            if (type == IFLA_MTU && RTA_PAYLOAD(a) >= sizeof(uint32_t)) {
                std::memcpy(&sStatus.mtu, RTA_DATA(a), sizeof(uint32_t));
            } else if (type == IFLA_LINKINFO) {
                parse_linkinfo(a, sStatus);
            }
        }
        return 0;
    }

    int link_set_up(const std::string &strIface, bool bUp)
    {
        const int iIdx = ifindex_of(strIface);
        if (iIdx == 0) {
            return ENODEV;
        }

        NlSocket sSock;
        NlRequest req(RTM_NEWLINK, NLM_F_REQUEST | NLM_F_ACK, iIdx);
        req.ifi()->ifi_flags  = bUp ? IFF_UP : 0;
        req.ifi()->ifi_change = IFF_UP;
        return sSock.transact(req, nullptr);
    }

    int link_configure(const std::string &strIface, const LinkConfig &sCfg)
    {
        const int iIdx = ifindex_of(strIface);
        if (iIdx == 0) {
            return ENODEV;
        }

        // CAN FD needs a data-phase bitrate, otherwise the kernel refuses to bring the link up.
        if ((sCfg.ctrlModeMask & CTRLMODE_FD) && (sCfg.ctrlModeFlags & CTRLMODE_FD) && sCfg.dataBitrate == 0) {
            return EINVAL;
        }

        NlSocket sSock;
        NlRequest req(RTM_NEWLINK, NLM_F_REQUEST | NLM_F_ACK, iIdx);

        rtattr *pLinkInfo = req.nestBegin(IFLA_LINKINFO);
        static const char kKind[] = "can";
        req.addAttr(IFLA_INFO_KIND, kKind, sizeof(kKind) - 1);
        rtattr *pData = req.nestBegin(IFLA_INFO_DATA);

        if (sCfg.bitrate != 0) {
            can_bittiming bt = {};
            bt.bitrate       = sCfg.bitrate;
            bt.sample_point  = sCfg.samplePoint; // 0 -> kernel picks (CiA 301 recommendation)
            req.addValue(IFLA_CAN_BITTIMING, bt);
        }
        if (sCfg.ctrlModeMask != 0) {
            can_ctrlmode cm = {};
            cm.mask         = sCfg.ctrlModeMask;
            cm.flags        = sCfg.ctrlModeFlags & sCfg.ctrlModeMask;
            req.addValue(IFLA_CAN_CTRLMODE, cm);
        }
        if (sCfg.restartMsSet) {
            req.addValue(IFLA_CAN_RESTART_MS, sCfg.restartMs);
        }
        if (sCfg.dataBitrate != 0) {
            can_bittiming dbt = {};
            dbt.bitrate       = sCfg.dataBitrate;
            dbt.sample_point  = sCfg.dataSamplePoint;
            req.addValue(IFLA_CAN_DATA_BITTIMING, dbt);
        }

        req.nestEnd(pData);
        req.nestEnd(pLinkInfo);

        return sSock.transact(req, nullptr);
    }

    int link_restart(const std::string &strIface)
    {
        const int iIdx = ifindex_of(strIface);
        if (iIdx == 0) {
            return ENODEV;
        }

        NlSocket sSock;
        NlRequest req(RTM_NEWLINK, NLM_F_REQUEST | NLM_F_ACK, iIdx);
        rtattr *pLinkInfo = req.nestBegin(IFLA_LINKINFO);
        static const char kKind[] = "can";
        req.addAttr(IFLA_INFO_KIND, kKind, sizeof(kKind) - 1);
        rtattr *pData = req.nestBegin(IFLA_INFO_DATA);
        const uint32_t u32One = 1;
        req.addValue(IFLA_CAN_RESTART, u32One);
        req.nestEnd(pData);
        req.nestEnd(pLinkInfo);
        return sSock.transact(req, nullptr);
    }

    bool link_matches(const LinkStatus &s, const LinkConfig &c)
    {
        if (!s.exists || !s.up) {
            return false;
        }
        if (!s.isCan) {
            return true; // vcan & co.: nothing to match beyond "is up"
        }

        // The kernel stores the bitrate it can really generate from the controller
        // clock, which may deviate slightly from the request (e.g. 83333 vs 83333.3).
        constexpr uint32_t kBitrateTolPermille     = 10; // 1 %
        constexpr uint32_t kSamplePointTolPermille = 20; // 2 percentage points

        if (c.bitrate != 0) {
            if (!close_enough(s.bitrate, c.bitrate, kBitrateTolPermille)) {
                return false;
            }
            if (c.samplePoint != 0 &&
                (s.samplePoint > c.samplePoint ? s.samplePoint - c.samplePoint : c.samplePoint - s.samplePoint) > kSamplePointTolPermille) {
                return false;
            }
        }
        if (c.dataBitrate != 0) {
            if (!close_enough(s.dataBitrate, c.dataBitrate, kBitrateTolPermille)) {
                return false;
            }
            if (c.dataSamplePoint != 0 &&
                (s.dataSamplePoint > c.dataSamplePoint ? s.dataSamplePoint - c.dataSamplePoint : c.dataSamplePoint - s.dataSamplePoint) > kSamplePointTolPermille) {
                return false;
            }
        }
        if (c.ctrlModeMask != 0 && (s.ctrlModeFlags & c.ctrlModeMask) != (c.ctrlModeFlags & c.ctrlModeMask)) {
            return false;
        }
        if (c.restartMsSet && s.restartMs != c.restartMs) {
            return false;
        }
        return true;
    }

    int link_apply(const std::string &strIface, const LinkConfig &sCfg, bool &bChanged)
    {
        bChanged = false;

        LinkStatus sSt;
        if (const int rc = link_get_status(strIface, sSt); rc != 0) {
            return rc;
        }

        if (!sSt.isCan) {
            // vcan0 / slcan / ... : no bit-timing to program, just make sure it is up.
            if (!sSt.up) {
                if (const int rc = link_set_up(strIface, true); rc != 0) {
                    return rc;
                }
                bChanged = true;
            }
            return 0;
        }

        if (link_matches(sSt, sCfg)) {
            return 0;
        }

        if (sSt.up) {
            if (const int rc = link_set_up(strIface, false); rc != 0) {
                return rc;
            }
        }
        if (!sCfg.empty()) {
            if (const int rc = link_configure(strIface, sCfg); rc != 0) {
                return rc;
            }
        }
        if (const int rc = link_set_up(strIface, true); rc != 0) {
            return rc;
        }

        bChanged = true;
        return 0;
    }

    const char *state_name(uint32_t u32State)
    {
        switch (u32State) {
        case CAN_STATE_ERROR_ACTIVE:  return "ERROR-ACTIVE";
        case CAN_STATE_ERROR_WARNING: return "ERROR-WARNING";
        case CAN_STATE_ERROR_PASSIVE: return "ERROR-PASSIVE";
        case CAN_STATE_BUS_OFF:       return "BUS-OFF";
        case CAN_STATE_STOPPED:       return "STOPPED";
        case CAN_STATE_SLEEPING:      return "SLEEPING";
        default:                      return "UNKNOWN";
        }
    }

    std::string format_status(const std::string &strIface, const LinkStatus &s)
    {
        if (!s.exists) {
            return strIface + ": not found";
        }

        char buf[512];
        int n = std::snprintf(buf, sizeof(buf), "%s: kind=%s up=%d carrier=%d mtu=%u",
                              strIface.c_str(), s.kind.empty() ? "-" : s.kind.c_str(),
                              s.up ? 1 : 0, s.lowerUp ? 1 : 0, s.mtu);
        std::string str(buf, static_cast<size_t>(n > 0 ? n : 0));

        if (s.isCan) {
            if (s.hasState) {
                str += std::string(" state=") + state_name(s.state);
            }
            std::snprintf(buf, sizeof(buf), " bitrate=%u sample_point=%u.%u%% clock=%u brp=%u tq=%u prop_seg=%u phase_seg1=%u phase_seg2=%u sjw=%u",
                          s.bitrate, s.samplePoint / 10, s.samplePoint % 10, s.clockHz, s.brp, s.tq, s.propSeg, s.phaseSeg1, s.phaseSeg2, s.sjw);
            str += buf;

            if (s.dataBitrate != 0) {
                std::snprintf(buf, sizeof(buf), " dbitrate=%u dsample_point=%u.%u%%", s.dataBitrate, s.dataSamplePoint / 10, s.dataSamplePoint % 10);
                str += buf;
            }

            static const struct {
                    uint32_t bit;
                    const char *name;
            } kModes[] = {
                {CTRLMODE_LOOPBACK, "LOOPBACK"},         {CTRLMODE_LISTEN_ONLY, "LISTEN-ONLY"}, {CTRLMODE_TRIPLE_SAMPLING, "TRIPLE-SAMPLING"},
                {CTRLMODE_ONE_SHOT, "ONE-SHOT"},         {CTRLMODE_BERR_REPORTING, "BERR-REPORTING"}, {CTRLMODE_FD, "FD"},
                {CTRLMODE_PRESUME_ACK, "PRESUME-ACK"},   {CTRLMODE_FD_NON_ISO, "FD-NON-ISO"},
            };
            str += " ctrlmode=<";
            bool bFirst = true;
            for (const auto &m : kModes) {
                if (s.ctrlModeFlags & m.bit) {
                    str += (bFirst ? "" : ",");
                    str += m.name;
                    bFirst = false;
                }
            }
            str += ">";

            std::snprintf(buf, sizeof(buf), " restart_ms=%u", s.restartMs);
            str += buf;

            if (s.hasBerr) {
                std::snprintf(buf, sizeof(buf), " txerr=%u rxerr=%u", static_cast<unsigned>(s.txErr), static_cast<unsigned>(s.rxErr));
                str += buf;
            }
        }
        return str;
    }

    std::string describe_errno(int iErrno)
    {
        std::string str = std::strerror(iErrno);
        switch (iErrno) {
        case EPERM:
        case EACCES:
            str += " - changing bitrate / link state needs root or CAP_NET_ADMIN (run with sudo, or: sudo setcap cap_net_admin+ep <your-binary>)";
            break;
        case ENODEV:
            str += " - no such interface; is the adapter plugged in and its driver loaded (e.g. 'sudo modprobe peak_usb')?";
            break;
        case EBUSY:
            str += " - the interface is up; bit-timing can only be changed while it is down";
            break;
        case EOPNOTSUPP:
            str += " - not supported by this adapter/driver (e.g. CAN FD or the requested controller mode)";
            break;
        case EDOM:
            str += " - the requested bitrate cannot be generated from this controller's clock (error too large)";
            break;
        case EINVAL:
            str += " - invalid value: bitrate/sample-point not achievable, bitrate not set before bringing the link up, or CAN FD without a data bitrate";
            break;
        case ETIMEDOUT:
            str += " - no answer from the kernel (netlink timeout)";
            break;
        default:
            break;
        }
        return str;
    }

} // namespace kcan
