#include "uVector.hpp"
#include "uLogger.hpp"

#include <cstring>
#include <charconv>
#include <algorithm>
#include <cctype>


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
#  undef LT_HDR
#endif
#ifdef LOG_HDR
#  undef LOG_HDR
#endif

#define LT_HDR   "VECTOR_DRV  |"
#define LOG_HDR  LOG_STRING(LT_HDR)


// ============================================================================
// PROCESS-WIDE DRIVER HANDLE
// ============================================================================

std::mutex Vector::s_driverMutex;
uint32_t   Vector::s_u32DriverRefCount = 0;

ICommDriver::Status Vector::s_EnsureDriverOpen()
{
    std::lock_guard<std::mutex> lock(s_driverMutex);

    if (s_u32DriverRefCount == 0) {
        XLstatus sts = xlOpenDriver();
        if (sts != XL_SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("xlOpenDriver failed:"); LOG_STRING(xlGetErrorString(sts)));
            return Status::PORT_ACCESS;
        }
    }
    ++s_u32DriverRefCount;
    return Status::SUCCESS;
}

void Vector::s_ReleaseDriver()
{
    std::lock_guard<std::mutex> lock(s_driverMutex);

    if (s_u32DriverRefCount == 0) {
        return;
    }
    if (--s_u32DriverRefCount == 0) {
        xlCloseDriver();
    }
}


// ============================================================================
// STATIC HELPERS
// ============================================================================

bool Vector::parseUint32(std::string_view sv, uint32_t& out)
{
    if (sv.empty()) return false;

    int base = 10;
    if (sv.size() > 2 && sv[0] == '0' && (sv[1] == 'x' || sv[1] == 'X')) {
        sv   = sv.substr(2);
        base = 16;
    }

    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out, base);
    return (ec == std::errc{} && ptr == sv.data() + sv.size());
}


uint32_t Vector::resolveTxId(std::string_view xtra_params) const
{
    if (xtra_params.empty()) return m_u32DefaultTxId;

    uint32_t id = 0;
    if (!parseUint32(xtra_params, id)) {
        LOG_PRINT(LOG_WARNING, LOG_HDR;
                  LOG_STRING("resolveTxId: cannot parse xtra_params, using default TX ID");
                  LOG_STRING(xtra_params.data()));
        return m_u32DefaultTxId;
    }
    return id;
}


uint32_t Vector::resolveRxId(std::string_view xtra_params) const
{
    if (xtra_params.empty()) return m_u32DefaultRxFilterId;

    uint32_t id = 0;
    if (!parseUint32(xtra_params, id)) {
        LOG_PRINT(LOG_WARNING, LOG_HDR;
                  LOG_STRING("resolveRxId: cannot parse xtra_params, using default RX filter ID");
                  LOG_STRING(xtra_params.data()));
        return m_u32DefaultRxFilterId;
    }
    return id;
}


uint32_t Vector::resolveTpRxId(std::string_view xtra_params) const
{
    if (!xtra_params.empty()) {
        uint32_t id = 0;
        if (parseUint32(xtra_params, id)) {
            return id;
        }
        LOG_PRINT(LOG_WARNING, LOG_HDR;
                  LOG_STRING("resolveTpRxId: cannot parse xtra_params, falling back");
                  LOG_STRING(xtra_params.data()));
    }
    return m_bTpRxIdSet ? m_u32TpRxId : m_u32DefaultTxId;
}


void Vector::dumpFrame(CommDir dir, uint32_t u32Id, bool bExtended, std::span<const uint8_t> data) const
{
    if (!gui_mode_active()) {
        return;
    }
    char label[k_labelSize];
    std::snprintf(label, sizeof(label), "%s id=0x%X%s",
                  m_strIdentityLabel.empty() ? "Vector" : m_strIdentityLabel.c_str(),
                  u32Id, bExtended ? " (ext)" : "");
    gui_notify_comm_dump(m_strInstanceName, commdump_details(CommFamily::CAN, label),
                          dir, data.data(), static_cast<uint32_t>(data.size()));
}


bool Vector::frameMatchesFilter(const XLevent& evt, uint32_t u32RxFilterId) const
{
    if (u32RxFilterId == 0) {
        return true;   // accept-all
    }

    // Normalise the SocketCAN canid_t convention (bit 31 = CAN_EFF_FLAG) the
    // same way PCAN/KVCAN/SLCAN do; XL-API instead folds extended-ness into
    // XLcanMsg::id itself via XL_CAN_EXT_MSG_ID.
    const bool     bWantExtended  = (u32RxFilterId & CAN_EFF_FLAG) != 0U ||
                                     ((u32RxFilterId & CAN_EFF_MASK) > CAN_SFF_MASK);
    const uint32_t u32WantId      = u32RxFilterId & (bWantExtended ? CAN_EFF_MASK : CAN_SFF_MASK);
    const bool     bFrameExtended = (evt.tagData.msg.id & XL_CAN_EXT_MSG_ID) != 0U;
    const uint32_t u32FrameId     = evt.tagData.msg.id & CAN_EFF_MASK;

    return (bFrameExtended == bWantExtended) && (u32FrameId == u32WantId);
}


bool Vector::m_ShouldSkipRxEvent(const XLevent& evt)
{
    static constexpr uint16_t k_invalidDataFlags = XL_CAN_MSG_FLAG_ERROR_FRAME
                                                  | XL_CAN_MSG_FLAG_OVERRUN
                                                  | XL_CAN_MSG_FLAG_NERR;

    if (evt.tagData.msg.flags & k_invalidDataFlags) {
        return true;   // not valid CAN payload
    }
    if (evt.tagData.msg.flags & XL_CAN_MSG_FLAG_TX_COMPLETED) {
        return true;   // our own transmitted frame, echoed back - see header comment
    }
    return false;
}


ICommDriver::Status Vector::mapXlError(XLstatus sts)
{
    if (sts == XL_SUCCESS)             return Status::SUCCESS;
    if (sts == XL_ERR_QUEUE_IS_EMPTY)  return Status::READ_TIMEOUT;
    if (sts == XL_ERR_INVALID_ACCESS)  return Status::PORT_ACCESS;
    if (sts == XL_ERR_PORT_IS_OFFLINE) return Status::PORT_ACCESS;
    return Status::READ_ERROR;
}


// ============================================================================
// LIFECYCLE
// ============================================================================

ICommDriver::Status Vector::m_OpenWithMask_locked(XLaccess accessMask,
                                                  uint32_t u32Bitrate,
                                                  uint32_t u32TxId,
                                                  bool     bExtended)
{
    // ASSUMES m_mutex IS ALREADY HELD and s_EnsureDriverOpen() has ALREADY
    // been called by the caller (open() / openDirect()).

    if (accessMask == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("m_OpenWithMask_locked: empty access mask"));
        return Status::PORT_ACCESS;
    }

    XLportHandle portHandle = XL_INVALID_PORTHANDLE;
    XLaccess     permissionMask = accessMask;

    // "Vector" as the userName here is just a label XL-API surfaces in its
    // own diagnostics (Vector Hardware Config's port list, etc.) - unrelated
    // to the Vector Hardware Config "application name" used by open()'s
    // xlGetApplConfig() path; openDirect() never touches that indirection.
    XLstatus sts = xlOpenPort(&portHandle, const_cast<char*>("Vector"), accessMask, &permissionMask,
                              VECTOR_RX_QUEUE_SIZE, XL_INTERFACE_VERSION_V3, XL_BUS_TYPE_CAN);
    if (sts != XL_SUCCESS || portHandle == XL_INVALID_PORTHANDLE) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("xlOpenPort failed:"); LOG_STRING(xlGetErrorString(sts)));
        return Status::PORT_ACCESS;
    }

    // Only the port that was granted init access for this channel is allowed
    // to configure the bitrate — a second application sharing the same
    // channel is not. permissionMask reflects what xlOpenPort() actually
    // granted, which may be a subset of accessMask.
    if ((permissionMask & accessMask) == accessMask) {
        sts = xlCanSetChannelBitrate(portHandle, accessMask, u32Bitrate);
        if (sts != XL_SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("xlCanSetChannelBitrate failed:"); LOG_STRING(xlGetErrorString(sts)));
            xlClosePort(portHandle);
            return Status::PORT_ACCESS;
        }
    } else {
        LOG_PRINT(LOG_WARNING, LOG_HDR;
                  LOG_STRING("init access not granted for this channel - bitrate left as configured by another application"));
    }

    HANDLE hEvent = nullptr;
    sts = xlSetNotification(portHandle, &hEvent, 1);
    if (sts != XL_SUCCESS || hEvent == nullptr) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("xlSetNotification failed:"); LOG_STRING(xlGetErrorString(sts)));
        xlClosePort(portHandle);
        return Status::PORT_ACCESS;
    }

    sts = xlActivateChannel(portHandle, accessMask, XL_BUS_TYPE_CAN, XL_ACTIVATE_RESET_CLOCK);
    if (sts != XL_SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("xlActivateChannel failed:"); LOG_STRING(xlGetErrorString(sts)));
        xlClosePort(portHandle);
        return Status::PORT_ACCESS;
    }

    m_xlPort         = portHandle;
    m_xlAccessMask   = accessMask;
    m_hRxEvent       = hEvent;
    m_bOpen          = true;
    m_bExtendedId    = bExtended;
    m_u32DefaultTxId = u32TxId;

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("Vector channel opened, access mask:"); LOG_HEX32(static_cast<uint32_t>(accessMask));
              LOG_STRING("bitrate:"); LOG_UINT32(u32Bitrate);
              LOG_STRING("TX ID:"); LOG_HEX32(m_u32DefaultTxId));

    return Status::SUCCESS;
}


ICommDriver::Status Vector::open(const std::string& strAppName,
                                 uint32_t           u32AppChannel,
                                 uint32_t           u32Bitrate,
                                 uint32_t           u32TxId,
                                 bool               bExtended,
                                 bool               bFD)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (strAppName.empty() || u32Bitrate == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("open: invalid parameter(s), app:"); LOG_STRING(strAppName.c_str());
                  LOG_STRING("bitrate:"); LOG_UINT32(u32Bitrate));
        return Status::INVALID_PARAM;
    }

    if (bFD) {
        // See uVector.hpp class comment: CAN FD is intentionally unimplemented.
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("open: CAN FD is not supported by this driver"));
        return Status::INVALID_PARAM;
    }

    Status s = s_EnsureDriverOpen();
    if (s != Status::SUCCESS) {
        return s;
    }

    // Resolve the physical channel Vector Hardware Config has assigned to
    // (strAppName, u32AppChannel).
    unsigned int hwType = 0, hwIndex = 0, hwChannel = 0;
    std::vector<char> vAppName(strAppName.begin(), strAppName.end());
    vAppName.push_back('\0');

    XLstatus sts = xlGetApplConfig(vAppName.data(), u32AppChannel,
                                   &hwType, &hwIndex, &hwChannel, XL_BUS_TYPE_CAN);
    if (sts != XL_SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("xlGetApplConfig failed - check that");
                  LOG_STRING(strAppName.c_str());
                  LOG_STRING("is configured with a CAN channel in Vector Hardware Config:");
                  LOG_STRING(xlGetErrorString(sts)));
        s_ReleaseDriver();
        return Status::PORT_ACCESS;
    }

    XLaccess accessMask = xlGetChannelMask(static_cast<int>(hwType),
                                           static_cast<int>(hwIndex),
                                           static_cast<int>(hwChannel));
    if (accessMask == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("xlGetChannelMask returned an empty mask"));
        s_ReleaseDriver();
        return Status::PORT_ACCESS;
    }

    Status openSts = m_OpenWithMask_locked(accessMask, u32Bitrate, u32TxId, bExtended);
    if (openSts != Status::SUCCESS) {
        s_ReleaseDriver();
        return openSts;
    }

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("(via Vector Hardware Config) app:"); LOG_STRING(strAppName.c_str());
              LOG_STRING("index:"); LOG_UINT32(u32AppChannel));

    return Status::SUCCESS;
}


ICommDriver::Status Vector::openDirect(const DeviceSelector& sel,
                                       uint32_t              u32Bitrate,
                                       uint32_t              u32TxId,
                                       bool                  bExtended,
                                       bool                  bFD)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (u32Bitrate == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("openDirect: invalid bitrate"));
        return Status::INVALID_PARAM;
    }

    if (bFD) {
        // See uVector.hpp class comment: CAN FD is intentionally unimplemented.
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("openDirect: CAN FD is not supported by this driver"));
        return Status::INVALID_PARAM;
    }

    if (sel.i32HwType < 0 && sel.u32SerialNumber == 0 && sel.strChannelName.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("openDirect: DeviceSelector is empty (set at least one of "
                             "i32HwType / u32SerialNumber / strChannelName) - refusing to "
                             "match every CAN-capable channel in the system"));
        return Status::INVALID_PARAM;
    }

    // matchChannels() opens/releases the process-wide driver handle itself
    // (see enumerateChannels()) - independent of the s_EnsureDriverOpen()
    // this function calls below for the port it's about to open.
    std::vector<ChannelInfo> vMatches = matchChannels(sel);

    if (vMatches.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("openDirect: no CAN-capable channel matched the given selector "
                             "- run VECTOR.DEVICES to see what's currently visible to XL-API"));
        return Status::INVALID_PARAM;
    }

    if (vMatches.size() > 1) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("openDirect: selector is ambiguous,"); LOG_UINT32(static_cast<uint32_t>(vMatches.size()));
                  LOG_STRING("channels matched - narrow it with hwidx=/hwch=/serial="));
        for (const auto& info : vMatches) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("  candidate:"); LOG_STRING(info.strName.c_str());
                      LOG_STRING(info.strHwType.c_str());
                      LOG_STRING("hwIndex:"); LOG_UINT32(info.u32HwIndex);
                      LOG_STRING("hwChannel:"); LOG_UINT32(info.u32HwChannel);
                      LOG_STRING("serial:"); LOG_UINT32(info.u32SerialNumber));
        }
        return Status::INVALID_PARAM;
    }

    const ChannelInfo& matched = vMatches.front();

    Status s = s_EnsureDriverOpen();
    if (s != Status::SUCCESS) {
        return s;
    }

    Status openSts = m_OpenWithMask_locked(matched.xlChannelMask, u32Bitrate, u32TxId, bExtended);
    if (openSts != Status::SUCCESS) {
        s_ReleaseDriver();
        return openSts;
    }

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("(direct selection) matched:"); LOG_STRING(matched.strName.c_str());
              LOG_STRING(matched.strHwType.c_str());
              LOG_STRING("serial:"); LOG_UINT32(matched.u32SerialNumber));

    return Status::SUCCESS;
}


// ============================================================================
// DEVICE ENUMERATION
// ============================================================================

std::vector<Vector::ChannelInfo> Vector::enumerateChannels()
{
    std::vector<ChannelInfo> vResult;

    if (s_EnsureDriverOpen() != Status::SUCCESS) {
        return vResult;
    }

    XLdriverConfig cfg;
    std::memset(&cfg, 0, sizeof(cfg));

    XLstatus sts = xlGetDriverConfig(&cfg);
    if (sts != XL_SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("xlGetDriverConfig failed:"); LOG_STRING(xlGetErrorString(sts)));
        s_ReleaseDriver();
        return vResult;
    }

    const uint32_t count = std::min<uint32_t>(cfg.channelCount,
                                              static_cast<uint32_t>(XL_CONFIG_MAX_CHANNELS));

    for (uint32_t i = 0; i < count; ++i) {
        const auto& ch = cfg.channel[i];

        ChannelInfo info;
        info.strName         = std::string(ch.name, strnlen(ch.name, sizeof(ch.name)));
        info.u32HwType       = ch.hwType;
        info.strHwType       = hwTypeToString(ch.hwType);
        info.u32HwIndex      = ch.hwIndex;
        info.u32HwChannel    = ch.hwChannel;
        info.u32ChannelIndex = ch.channelIndex;
        info.xlChannelMask   = ch.channelMask;
        info.u32SerialNumber = ch.serialNumber;
        info.bIsOnBus        = (ch.isOnBus != 0);
        info.bSupportsCan    = (ch.channelBusCapabilities & XL_BUS_ACTIVE_CAP_CAN) != 0U;

        vResult.push_back(std::move(info));
    }

    s_ReleaseDriver();
    return vResult;
}


std::vector<Vector::ChannelInfo> Vector::matchChannels(const DeviceSelector& sel)
{
    std::vector<ChannelInfo> vResult;

    for (auto& info : enumerateChannels()) {
        if (!info.bSupportsCan) continue;
        if (sel.i32HwType >= 0 && info.u32HwType != static_cast<uint32_t>(sel.i32HwType)) continue;
        if (sel.u32SerialNumber != 0 && info.u32SerialNumber != sel.u32SerialNumber) continue;
        if (!sel.strChannelName.empty() && info.strName != sel.strChannelName) continue;
        if (sel.i32HwIndex >= 0 && info.u32HwIndex != static_cast<uint32_t>(sel.i32HwIndex)) continue;
        if (sel.i32HwChannel >= 0 && info.u32HwChannel != static_cast<uint32_t>(sel.i32HwChannel)) continue;

        vResult.push_back(std::move(info));
    }

    return vResult;
}


namespace {
    // Name <-> XL_HWTYPE_* lookup table. Extend as needed - see the stub
    // vxlapi.h's own "subset, not exhaustive" note; the real vxlapi.h defines
    // many more XL_HWTYPE_* constants than are listed here.
    struct HwTypeEntry { const char* name; uint32_t value; };
    constexpr HwTypeEntry k_hwTypeTable[] = {
        { "VIRTUAL",    XL_HWTYPE_VIRTUAL    },
        { "CANCARDX",   XL_HWTYPE_CANCARDX   },
        { "CANCARDY",   XL_HWTYPE_CANCARDY   },
        { "CANCARDXL",  XL_HWTYPE_CANCARDXL  },
        { "CANCASEXL",  XL_HWTYPE_CANCASEXL  },
        { "CANBOARDXL", XL_HWTYPE_CANBOARDXL },
        { "VN8900",     XL_HWTYPE_VN8900     },
        { "VN8950",     XL_HWTYPE_VN8950     },
        { "VN1610",     XL_HWTYPE_VN1610     },
        { "VN1630",     XL_HWTYPE_VN1630     },
        { "VN1640",     XL_HWTYPE_VN1640     },
        { "VN8970",     XL_HWTYPE_VN8970     },
        { "VN1611",     XL_HWTYPE_VN1611     },
        { "VN5610",     XL_HWTYPE_VN5610     },
        { "VN7570",     XL_HWTYPE_VN7570     },
        { "VX1121",     XL_HWTYPE_VX1121     },
        { "VX1131",     XL_HWTYPE_VX1131     },
        { "VN7610",     XL_HWTYPE_VN7610     },
        { "VN7572",     XL_HWTYPE_VN7572     },
        { "VN8972",     XL_HWTYPE_VN8972     },
        { "VX0312",     XL_HWTYPE_VX0312     },
        { "VN8800",     XL_HWTYPE_VN8800     },
        { "VN5610A",    XL_HWTYPE_VN5610A    },
        { "VN7640",     XL_HWTYPE_VN7640     },
    };
}

std::string Vector::hwTypeToString(uint32_t u32HwType)
{
    for (const auto& e : k_hwTypeTable) {
        if (e.value == u32HwType) return e.name;
    }
    char buf[24];
    std::snprintf(buf, sizeof(buf), "HWTYPE_%u", u32HwType);
    return buf;
}


bool Vector::hwTypeFromString(const std::string& strName, uint32_t& u32HwType)
{
    if (strName.empty()) return false;

    // Try a raw numeric value first (decimal or 0x-hex), same convention as
    // resolveTxId()/resolveRxId().
    if (parseUint32(strName, u32HwType)) {
        return true;
    }

    std::string strUpper(strName);
    std::transform(strUpper.begin(), strUpper.end(), strUpper.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    for (const auto& e : k_hwTypeTable) {
        if (strUpper == e.name) {
            u32HwType = e.value;
            return true;
        }
    }

    return false;
}


ICommDriver::Status Vector::close()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_bOpen) {
        xlDeactivateChannel(m_xlPort, m_xlAccessMask);
        xlClosePort(m_xlPort);
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("Vector channel closed"));
        m_xlPort       = XL_INVALID_PORTHANDLE;
        m_xlAccessMask = 0;
        m_hRxEvent     = nullptr;
        m_bOpen        = false;
        s_ReleaseDriver();
    }
    return Status::SUCCESS;
}


bool Vector::is_open() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_bOpen;
}


// ============================================================================
// FRAME-LEVEL PRIMITIVES
// ============================================================================

ICommDriver::Status Vector::recvFrame(uint32_t u32TimeoutMs, XLevent& evt, std::stop_token stop_tok) const
{
    const DWORD dwWaitTimeout = (u32TimeoutMs == 0) ? INFINITE : static_cast<DWORD>(u32TimeoutMs);

    // Registered once for the whole call (not per wait-iteration below) so
    // a stop request at any point during this recvFrame() wakes whichever
    // WaitForSingleObject() happens to be blocked at the time. m_hRxEvent is
    // long-lived and shared across every call for this channel's lifetime
    // (unlike PCAN's fresh per-call CreateEvent()), so this SetEvent() is
    // indistinguishable from a genuine notification at the OS level — every
    // return point below re-checks stop_tok.stop_requested() to tell the two
    // apart rather than trusting a signalled wait alone.
    std::stop_callback onStop(stop_tok, [this]() {
        SetEvent(m_hRxEvent);
    });

    // A single xlReceive() call only ever returns ONE event, and the RX
    // notification event may already be signalled from a previous frame
    // that was left in the queue (chip-state events, etc.) — loop until we
    // either get a real data frame or the wait genuinely times out.
    for (;;) {
        if (stop_tok.stop_requested()) {
            return Status::READ_TIMEOUT;
        }

        unsigned int msgCount = 1;
        XLstatus sts = xlReceive(m_xlPort, &msgCount, &evt);

        if (sts == XL_SUCCESS && msgCount > 0) {
            if (evt.tag == XL_RECEIVE_MSG) {
                return Status::SUCCESS;
            }
            // Non-data event (chip state, etc.) — keep draining without
            // re-waiting, there may be more queued behind it.
            continue;
        }

        if (sts != XL_ERR_QUEUE_IS_EMPTY) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("xlReceive error:"); LOG_STRING(xlGetErrorString(sts)));
            return Status::READ_ERROR;
        }

        // Queue empty — wait for the notification event, then retry.
        DWORD dwWait = WaitForSingleObject(m_hRxEvent, dwWaitTimeout);

        if (stop_tok.stop_requested()) {
            // dwWait may have returned WAIT_OBJECT_0 because of our own
            // SetEvent() above rather than a real frame arriving — treat it
            // as a (harmless, cooperative) timeout either way, same as
            // PCAN::recvFrame() does for its own per-call event.
            return Status::READ_TIMEOUT;
        }

        if (dwWait == WAIT_TIMEOUT) {
            return Status::READ_TIMEOUT;
        }
        if (dwWait != WAIT_OBJECT_0) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("WaitForSingleObject returned:"); LOG_UINT32(dwWait));
            return Status::READ_ERROR;
        }
        // Event fired -- loop back and drain xlReceive().
    }
}


ICommDriver::Status Vector::sendFrame(uint32_t                 u32Id,
                                      bool                     bExtended,
                                      std::span<const uint8_t> data) const
{
    if (data.size() > VECTOR_MAX_PAYLOAD) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("sendFrame: payload too large"); LOG_SIZET(data.size()));
        return Status::INVALID_PARAM;
    }

    XLevent evt;
    std::memset(&evt, 0, sizeof(evt));
    evt.tag              = XL_TRANSMIT_MSG;
    evt.tagData.msg.id   = bExtended ? (u32Id | XL_CAN_EXT_MSG_ID) : u32Id;
    evt.tagData.msg.dlc  = static_cast<uint16_t>(data.size());
    evt.tagData.msg.flags = 0;
    std::memcpy(evt.tagData.msg.data, data.data(), data.size());

    unsigned int msgCount = 1;
    XLstatus sts = xlCanTransmit(m_xlPort, m_xlAccessMask, &msgCount, &evt);
    if (sts != XL_SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("xlCanTransmit failed:"); LOG_STRING(xlGetErrorString(sts));
                  LOG_STRING("ID:"); LOG_HEX32(u32Id));
        return Status::WRITE_ERROR;
    }

    dumpFrame(CommDir::Tx, u32Id, bExtended, data);

    return Status::SUCCESS;
}


// ============================================================================
// READ-MODE IMPLEMENTATIONS (identical structure to uPcan.cpp)
// ============================================================================

void Vector::buildKmpTable(std::span<const uint8_t> pattern, std::vector<int>& viLps)
{
    const size_t n = pattern.size();
    viLps.assign(n, 0);
    int len = 0;

    for (size_t i = 1; i < n; ) {
        if (pattern[i] == pattern[len]) {
            viLps[i++] = ++len;
        } else if (len != 0) {
            len = viLps[len - 1];
        } else {
            viLps[i++] = 0;
        }
    }
}


ICommDriver::Status Vector::readExact(uint32_t           u32TimeoutMs,
                                      std::span<uint8_t> buffer,
                                      size_t&            szBytesRead,
                                      uint32_t           u32RxFilterId,
                                      std::stop_token    stop_tok) const
{
    szBytesRead = 0;
    XLevent evt;

    while (szBytesRead < buffer.size()) {
        Status s = recvFrame(u32TimeoutMs, evt, stop_tok);
        if (s != Status::SUCCESS) return s;

        if (!frameMatchesFilter(evt, u32RxFilterId)) continue;
        if (m_ShouldSkipRxEvent(evt)) continue;

        const uint32_t rawId = evt.tagData.msg.id & CAN_EFF_MASK;
        const bool     ext   = (evt.tagData.msg.id & XL_CAN_EXT_MSG_ID) != 0U;
        dumpFrame(CommDir::Rx, rawId, ext,
                  std::span<const uint8_t>(evt.tagData.msg.data, evt.tagData.msg.dlc));

        size_t toCopy = std::min<size_t>(evt.tagData.msg.dlc, buffer.size() - szBytesRead);
        std::memcpy(buffer.data() + szBytesRead, evt.tagData.msg.data, toCopy);
        szBytesRead += toCopy;
    }

    return Status::SUCCESS;
}


ICommDriver::Status Vector::readUntilDelimiter(uint32_t           u32TimeoutMs,
                                               std::span<uint8_t> buffer,
                                               uint8_t            cDelimiter,
                                               size_t&            szBytesRead,
                                               uint32_t           u32RxFilterId,
                                               std::stop_token    stop_tok) const
{
    if (buffer.size() < 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("readUntilDelimiter: buffer too small"));
        return Status::INVALID_PARAM;
    }

    szBytesRead = 0;
    XLevent evt;

    while (true) {
        Status s = recvFrame(u32TimeoutMs, evt, stop_tok);
        if (s != Status::SUCCESS) return s;

        if (!frameMatchesFilter(evt, u32RxFilterId)) continue;
        if (m_ShouldSkipRxEvent(evt)) continue;

        const uint32_t rawId = evt.tagData.msg.id & CAN_EFF_MASK;
        const bool     ext   = (evt.tagData.msg.id & XL_CAN_EXT_MSG_ID) != 0U;
        dumpFrame(CommDir::Rx, rawId, ext,
                  std::span<const uint8_t>(evt.tagData.msg.data, evt.tagData.msg.dlc));

        for (size_t i = 0; i < evt.tagData.msg.dlc; ++i) {
            uint8_t ch = evt.tagData.msg.data[i];
            if (ch == cDelimiter) {
                if (szBytesRead < buffer.size()) {
                    buffer[szBytesRead] = '\0';
                }
                return Status::SUCCESS;
            }
            if (szBytesRead >= buffer.size() - 1) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("readUntilDelimiter: buffer overflow"));
                return Status::BUFFER_OVERFLOW;
            }
            buffer[szBytesRead++] = ch;
        }
    }
}


ICommDriver::Status Vector::readUntilToken(uint32_t                 u32TimeoutMs,
                                           std::span<const uint8_t> token,
                                           uint32_t                 u32RxFilterId,
                                           std::stop_token          stop_tok) const
{
    if (token.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("readUntilToken: empty token"));
        return Status::INVALID_PARAM;
    }

    std::vector<int> viLps;
    buildKmpTable(token, viLps);

    XLevent evt;
    size_t  szMatched = 0;

    while (true) {
        Status s = recvFrame(u32TimeoutMs, evt, stop_tok);
        if (s != Status::SUCCESS) return s;

        if (!frameMatchesFilter(evt, u32RxFilterId)) continue;
        if (m_ShouldSkipRxEvent(evt)) continue;

        const uint32_t rawId = evt.tagData.msg.id & CAN_EFF_MASK;
        const bool     ext   = (evt.tagData.msg.id & XL_CAN_EXT_MSG_ID) != 0U;
        dumpFrame(CommDir::Rx, rawId, ext,
                  std::span<const uint8_t>(evt.tagData.msg.data, evt.tagData.msg.dlc));

        for (size_t i = 0; i < evt.tagData.msg.dlc; ++i) {
            uint8_t ch = evt.tagData.msg.data[i];

            while (szMatched > 0 && ch != token[szMatched]) {
                szMatched = static_cast<size_t>(viLps[szMatched - 1]);
            }
            if (ch == token[szMatched]) {
                ++szMatched;
                if (szMatched == token.size()) {
                    return Status::SUCCESS;
                }
            }
        }
    }
}


// ============================================================================
// PUBLIC UNIFIED INTERFACE
// ============================================================================

ICommDriver::ReadResult Vector::readOneFrame_locked(uint32_t           u32TimeoutMs,
                                                     std::span<uint8_t> buffer,
                                                     std::string_view   xtra_params) const
{
    // ASSUMES m_mutex IS ALREADY HELD (see class comment / RawIo).
    ReadResult result;
    const uint32_t rxFilterId = resolveRxId(xtra_params);
    XLevent        evt;

    while (true) {
        // TODO(stop-token): readOneFrame_locked() is invoked as a callback
        // through the transport-protocol interface (RawIo), which doesn't
        // carry a stop_token — same gap PCAN::readOneFrame_locked() documents.
        // A segmented Vector transfer (ISO-TP/J1939) is therefore not yet
        // cancellable via the STOP button.
        Status s = recvFrame(u32TimeoutMs, evt);
        if (s != Status::SUCCESS) {
            result.status = s;
            return result;
        }
        if (!frameMatchesFilter(evt, rxFilterId)) continue;
        if (m_ShouldSkipRxEvent(evt)) continue;
        break;
    }

    const uint32_t rawId = evt.tagData.msg.id & CAN_EFF_MASK;
    const bool     ext   = (evt.tagData.msg.id & XL_CAN_EXT_MSG_ID) != 0U;
    dumpFrame(CommDir::Rx, rawId, ext,
              std::span<const uint8_t>(evt.tagData.msg.data, evt.tagData.msg.dlc));

    const size_t toCopy = std::min<size_t>(evt.tagData.msg.dlc, buffer.size());
    if (toCopy > 0) {
        std::memcpy(buffer.data(), evt.tagData.msg.data, toCopy);
    }
    if (toCopy < evt.tagData.msg.dlc) {
        LOG_PRINT(LOG_WARNING, LOG_HDR;
                  LOG_STRING("readOneFrame_locked: frame truncated, buffer too small"));
    }

    result.status     = Status::SUCCESS;
    result.bytes_read = toCopy;
    return result;
}

ICommDriver::ReadResult Vector::readDispatch_locked(uint32_t           u32ReadTimeout,
                                                    std::span<uint8_t> buffer,
                                                    const ReadOptions& options,
                                                    std::string_view   xtra_params,
                                                    std::stop_token    stop_tok) const
{
    // ASSUMES m_mutex IS ALREADY HELD (see class comment / RawIo).
    ReadResult result;

    const uint32_t timeout    = u32ReadTimeout;
    const uint32_t rxFilterId = resolveRxId(xtra_params);

    switch (options.mode) {

        case ReadMode::Exact: {
            size_t bytesRead = 0;
            result.status         = readExact(timeout, buffer, bytesRead, rxFilterId, stop_tok);
            result.bytes_read     = bytesRead;
            result.found_terminator = false;
            break;
        }

        case ReadMode::UntilDelimiter: {
            size_t bytesRead = 0;
            result.status         = readUntilDelimiter(timeout, buffer,
                                                       options.delimiter,
                                                       bytesRead, rxFilterId, stop_tok);
            result.bytes_read       = bytesRead;
            result.found_terminator = (result.status == Status::SUCCESS);
            break;
        }

        case ReadMode::UntilToken: {
            result.status           = readUntilToken(timeout, options.token, rxFilterId, stop_tok);
            result.bytes_read       = 0;
            result.found_terminator = (result.status == Status::SUCCESS);
            break;
        }

        default:
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("tout_read: unknown ReadMode"));
            result.status = Status::INVALID_PARAM;
            break;
    }

    return result;
}


ICommDriver::ReadResult Vector::tout_read(uint32_t           u32ReadTimeout,
                                          std::span<uint8_t> buffer,
                                          const ReadOptions& options,
                                          std::string_view   xtra_params,
                                          std::stop_token    stop_tok) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_bOpen) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("tout_read: channel not open"));
        ReadResult result;
        result.status = Status::PORT_ACCESS;
        return result;
    }

    if (m_eTpProtocol == TpProtocol::NONE || options.mode != ReadMode::Exact) {
        return readDispatch_locked(u32ReadTimeout, buffer, options, xtra_params, stop_tok);
    }

    auto upTp = make_transport_protocol(m_eTpProtocol, m_sTpConfig);
    if (!upTp) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to instantiate transport protocol"));
        ReadResult result;
        result.status = Status::OPERATION_FAILED;
        return result;
    }

    char szRxId[16];
    char szTxId[16];
    std::snprintf(szRxId, sizeof(szRxId), "0x%X", resolveTpRxId(xtra_params));
    std::snprintf(szTxId, sizeof(szTxId), "0x%X", m_u32DefaultTxId);

    return upTp->receive(m_rawIo, u32ReadTimeout, buffer, szRxId, szTxId);
}


ICommDriver::WriteResult Vector::writeFragmented_locked(uint32_t                 u32WriteTimeout,
                                                         std::span<const uint8_t> buffer,
                                                         std::string_view         xtra_params) const
{
    // ASSUMES m_mutex IS ALREADY HELD (see class comment / RawIo).
    WriteResult result;

    (void)u32WriteTimeout;  // xlCanTransmit is non-blocking; timeout reserved for future use.

    const uint32_t u32TxId    = resolveTxId(xtra_params);
    const bool     bExtended  = m_bExtendedId || (u32TxId & CAN_EFF_FLAG) != 0U ||
                                 ((u32TxId & CAN_EFF_MASK) > CAN_SFF_MASK);
    const uint32_t u32RawTxId = u32TxId & (bExtended ? CAN_EFF_MASK : CAN_SFF_MASK);
    const size_t   maxPayload = VECTOR_MAX_PAYLOAD;

    size_t offset = 0;
    while (offset < buffer.size()) {
        size_t frameLen = std::min(maxPayload, buffer.size() - offset);
        Status s = sendFrame(u32RawTxId, bExtended, buffer.subspan(offset, frameLen));
        if (s != Status::SUCCESS) {
            result.status       = s;
            result.bytes_written = offset;
            return result;
        }
        offset += frameLen;
    }

    result.status        = Status::SUCCESS;
    result.bytes_written = buffer.size();

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("tout_write: sent"); LOG_SIZET(result.bytes_written);
              LOG_STRING("bytes, TX ID:"); LOG_HEX32(u32RawTxId));

    return result;
}


ICommDriver::WriteResult Vector::tout_write(uint32_t                 u32WriteTimeout,
                                            std::span<const uint8_t> buffer,
                                            std::string_view         xtra_params,
                                            std::stop_token          /*stop_tok*/) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_bOpen) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("tout_write: channel not open"));
        WriteResult result;
        result.status = Status::PORT_ACCESS;
        return result;
    }

    if (buffer.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("tout_write: empty buffer"));
        WriteResult result;
        result.status = Status::INVALID_PARAM;
        return result;
    }

    if (m_eTpProtocol == TpProtocol::NONE) {
        return writeFragmented_locked(u32WriteTimeout, buffer, xtra_params);
    }

    auto upTp = make_transport_protocol(m_eTpProtocol, m_sTpConfig);
    if (!upTp) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to instantiate transport protocol"));
        WriteResult result;
        result.status = Status::OPERATION_FAILED;
        return result;
    }

    char szTxId[16];
    char szRxId[16];
    std::snprintf(szTxId, sizeof(szTxId), "0x%X", m_u32DefaultTxId);
    std::snprintf(szRxId, sizeof(szRxId), "0x%X", resolveTpRxId(xtra_params));

    return upTp->send(m_rawIo, u32WriteTimeout, buffer, szTxId, szRxId);
}
