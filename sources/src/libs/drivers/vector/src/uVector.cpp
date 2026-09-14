#include "uVector.hpp"

#include "uLogger.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
#undef LT_HDR
#endif
#ifdef LOG_HDR
#undef LOG_HDR
#endif

#define LT_HDR  "VECTOR_DRV  |"
#define LOG_HDR LOG_STRING(LT_HDR)

// ============================================================================
// STATIC HELPERS
// ============================================================================

bool Vector::parseUint32(std::string_view sv, uint32_t &out)
{
    if (sv.empty()) {
        return false;
    }

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
    if (xtra_params.empty()) {
        return m_u32DefaultTxId;
    }

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
    if (xtra_params.empty()) {
        return m_u32DefaultRxFilterId;
    }

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
    std::snprintf(label, sizeof(label), "%s id=0x%X%s%s",
                  m_strIdentityLabel.empty() ? "Vector" : m_strIdentityLabel.c_str(),
                  u32Id, bExtended ? " (ext)" : "", m_bFD ? " (FD)" : "");
    gui_notify_comm_dump(m_strInstanceName, commdump_details(CommFamily::CAN, label),
                         dir, data.data(), static_cast<uint32_t>(data.size()));
}

bool Vector::frameMatchesFilter(const VectorRxFrame &frame, uint32_t u32RxFilterId) const
{
    if (u32RxFilterId == 0) {
        return true; // accept-all
    }

    // Normalise the SocketCAN canid_t convention (bit 31 = CAN_EFF_FLAG) the
    // same way PCAN/KVCAN/SLCAN do.
    const bool bWantExtended = (u32RxFilterId & CAN_EFF_FLAG) != 0U ||
                               ((u32RxFilterId & CAN_EFF_MASK) > CAN_SFF_MASK);
    const uint32_t u32WantId = u32RxFilterId & (bWantExtended ? CAN_EFF_MASK : CAN_SFF_MASK);

    return (frame.bExtended == bWantExtended) && (frame.u32Id == u32WantId);
}

ICommDriver::Status Vector::mapXlError(XLstatus sts)
{
    if (sts == XL_SUCCESS) {
        return Status::SUCCESS;
    }
    if (sts == XL_ERR_QUEUE_IS_EMPTY) {
        return Status::READ_TIMEOUT;
    }
    if (sts == XL_ERR_INVALID_ACCESS) {
        return Status::PORT_ACCESS;
    }
    if (sts == XL_ERR_PORT_IS_OFFLINE) {
        return Status::PORT_ACCESS;
    }
    return Status::READ_ERROR;
}

// ============================================================================
// LIFECYCLE
// ============================================================================

ICommDriver::Status Vector::m_OpenWithMask_locked(XLaccess accessMask,
                                                  uint32_t u32Bitrate,
                                                  uint32_t u32TxId,
                                                  bool bExtended,
                                                  bool bFD)
{
    // ASSUMES m_mutex IS ALREADY HELD and VectorDriverHandle::Acquire() has
    // ALREADY been called by the caller (open() / openDirect()).

    if (accessMask == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("m_OpenWithMask_locked: empty access mask"));
        return Status::PORT_ACCESS;
    }

    XLportHandle portHandle             = XL_INVALID_PORTHANDLE;
    XLaccess permissionMask             = 0;

    // CAN FD requires the V4 interface (XLcanTxEvent/XLcanRxEvent, 64-byte
    // payloads); classic CAN keeps using V3 for the smallest behavioural
    // delta against the existing (pre-FD) wire format/event semantics.
    const unsigned int interfaceVersion = bFD ? XL_INTERFACE_VERSION_V4 : XL_INTERFACE_VERSION_V3;

#if defined(_WIN32)

    permissionMask = accessMask;

    // "Vector" as the userName here is just a label XL-API surfaces in its
    // own diagnostics (Vector Hardware Config's port list, etc.) - unrelated
    // to the Vector Hardware Config "application name" used by open()'s
    // xlGetApplConfig() path; openDirect() never touches that indirection.
    XLstatus sts   = xlOpenPort(&portHandle, const_cast<char *>("Vector"), accessMask, &permissionMask,
                                VECTOR_RX_QUEUE_SIZE, interfaceVersion, XL_BUS_TYPE_CAN);
    if (sts != XL_SUCCESS || portHandle == XL_INVALID_PORTHANDLE) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("xlOpenPort failed:"); LOG_STRING(xlGetErrorString(sts)));
        return Status::PORT_ACCESS;
    }

#elif defined(__linux__)

    // The Linux port of XL-API (see vxlapi_linux.h) doesn't implement
    // xlOpenPort() at all — confirmed against the actual exported-symbol
    // list of the vendored libXlApi.so.26.20.14, not just absence from the
    // header. The real replacement is a three-call sequence that builds a
    // port up one channel at a time instead of from a ready-made accessMask:
    //   xlCreatePort()       - allocate an (as yet channel-less) port
    //   xlAddChannelToPort() - add exactly one channel, by INDEX not mask
    //   xlFinalizePort()     - no more channels can be added after this
    // accessMask is always a single-bit mask here (exactly one channel was
    // resolved by open()/openDirect() before calling this function), so
    // recovering the channel index XL-API's own documented invariant
    // (channelMask = 1 << channelIndex, see e.g. XLchannelConfig's own
    // field comment) is exact, not a heuristic: __builtin_ctzll() is
    // "count trailing zero bits", i.e. exactly log2() of a single set bit.
    const unsigned int channelIndex = static_cast<unsigned int>(__builtin_ctzll(static_cast<unsigned long long>(accessMask)));

    XLstatus sts                    = xlCreatePort(&portHandle, "Vector", VECTOR_RX_QUEUE_SIZE, interfaceVersion, XL_BUS_TYPE_CAN);
    if (sts != XL_SUCCESS || portHandle == XL_INVALID_PORTHANDLE) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("xlCreatePort failed:"); LOG_STRING(xlGetErrorString(sts)));
        return Status::PORT_ACCESS;
    }

    unsigned int permission = 0;
    sts                     = xlAddChannelToPort(portHandle, channelIndex, /*initAccess=*/1, &permission, XL_BUS_TYPE_CAN);
    if (sts != XL_SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("xlAddChannelToPort failed:"); LOG_STRING(xlGetErrorString(sts));
                  LOG_STRING("channel index:"); LOG_UINT32(channelIndex));
        xlClosePort(portHandle);
        return Status::PORT_ACCESS;
    }
    // Mirrors xlOpenPort()'s permissionMask output on Windows: a non-zero
    // permission means we were granted init access for this channel, same
    // meaning the (permissionMask & accessMask) == accessMask check below
    // already expects.
    permissionMask = (permission != 0U) ? accessMask : 0U;

    sts            = xlFinalizePort(portHandle);
    if (sts != XL_SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("xlFinalizePort failed:"); LOG_STRING(xlGetErrorString(sts)));
        xlClosePort(portHandle);
        return Status::PORT_ACCESS;
    }

#endif

    // Only the port that was granted init access for this channel is allowed
    // to configure the bitrate — a second application sharing the same
    // channel is not. permissionMask reflects what xlOpenPort() actually
    // granted, which may be a subset of accessMask.
    if ((permissionMask & accessMask) == accessMask) {

        if (bFD) {
            XLcanFdConf canFdConf;
            std::memset(&canFdConf, 0, sizeof(canFdConf));
            canFdConf.arbitrationBitRate = u32Bitrate;
            canFdConf.dataBitRate        = m_u32FdDataBitrate;
            // sjw/tseg1/tseg2 left at 0 for both phases: XL-API computes a
            // standard-compliant sample-point automatically when they're
            // zero, same as Vector's own XLCanFdDemo sample does — this
            // driver has no bit-timing tuning UI, so let the driver pick.
            canFdConf.options            = m_bFdIso ? 0U : CANFD_CONFOPT_NO_ISO;

            sts                          = xlCanFdSetConfiguration(portHandle, accessMask, &canFdConf);
            if (sts != XL_SUCCESS) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("xlCanFdSetConfiguration failed:"); LOG_STRING(xlGetErrorString(sts));
                          LOG_STRING("arb bitrate:"); LOG_UINT32(u32Bitrate);
                          LOG_STRING("data bitrate:"); LOG_UINT32(m_u32FdDataBitrate));
                xlClosePort(portHandle);
                return Status::PORT_ACCESS;
            }
        } else {
            sts = xlCanSetChannelBitrate(portHandle, accessMask, u32Bitrate);
            if (sts != XL_SUCCESS) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("xlCanSetChannelBitrate failed:"); LOG_STRING(xlGetErrorString(sts)));
                xlClosePort(portHandle);
                return Status::PORT_ACCESS;
            }
        }
    } else {
        LOG_PRINT(LOG_WARNING, LOG_HDR;
                  LOG_STRING("init access not granted for this channel - bitrate left as configured by another application"));
    }

    Status s = m_notifyWaiter.open(portHandle);
    if (s != Status::SUCCESS) {
        xlClosePort(portHandle);
        return s;
    }

    sts = xlActivateChannel(portHandle, accessMask, XL_BUS_TYPE_CAN, XL_ACTIVATE_RESET_CLOCK);
    if (sts != XL_SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("xlActivateChannel failed:"); LOG_STRING(xlGetErrorString(sts)));
        m_notifyWaiter.close();
        xlClosePort(portHandle);
        return Status::PORT_ACCESS;
    }

    m_xlPort         = portHandle;
    m_xlAccessMask   = accessMask;
    m_bOpen          = true;
    m_bExtendedId    = bExtended;
    m_bFD            = bFD;
    m_u32DefaultTxId = u32TxId;

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("Vector channel opened, access mask:"); LOG_HEX32(static_cast<uint32_t>(accessMask));
              LOG_STRING(bFD ? "arb bitrate:" : "bitrate:"); LOG_UINT32(u32Bitrate);
              LOG_STRING("FD:"); LOG_UINT32(bFD ? 1U : 0U);
              LOG_STRING("TX ID:"); LOG_HEX32(m_u32DefaultTxId));

    return Status::SUCCESS;
}

ICommDriver::Status Vector::open(const std::string &strAppName,
                                 uint32_t u32AppChannel,
                                 uint32_t u32Bitrate,
                                 uint32_t u32TxId,
                                 bool bExtended,
                                 bool bFD,
                                 const FdOptions &fdOpts)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (strAppName.empty() || u32Bitrate == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("open: invalid parameter(s), app:"); LOG_STRING(strAppName.c_str());
                  LOG_STRING("bitrate:"); LOG_UINT32(u32Bitrate));
        return Status::INVALID_PARAM;
    }

    if (bFD) {
        if (fdOpts.u32DataBitrate == 0) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("open: CAN FD data bitrate must be non-zero"));
            return Status::INVALID_PARAM;
        }
        m_u32FdDataBitrate = fdOpts.u32DataBitrate;
        m_bFdIso           = fdOpts.bIso;
        m_bFdBrs           = fdOpts.bBrs;
        m_u8FdPaddingByte  = fdOpts.u8PaddingByte;
    }

    Status s = VectorDriverHandle::Acquire();
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
        VectorDriverHandle::Release();
        return Status::PORT_ACCESS;
    }

    // xlGetChannelMask() is confirmed absent from the Linux port's actual
    // exports (same class of gap as xlOpenPort()/xlGetDriverConfig() - see
    // m_OpenWithMask_locked()/enumerateChannels()). xlGetChannelIndex() is
    // confirmed present on both platforms and is explicitly documented as
    // an alternative to xlGetChannelMask() for this exact purpose (see
    // vxlapi.h's own "This values can be used in a subsequent call to
    // xlGetChannelMask or xlGetChannelIndex" comment), so it's used
    // unconditionally here rather than branching by platform - accessMask
    // is always exactly (1 << channelIndex), the same invariant
    // enumerateChannels() relies on for its own Linux path.
    int channelIndex = xlGetChannelIndex(static_cast<int>(hwType),
                                         static_cast<int>(hwIndex),
                                         static_cast<int>(hwChannel));
    if (channelIndex < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("xlGetChannelIndex returned an invalid index"));
        VectorDriverHandle::Release();
        return Status::PORT_ACCESS;
    }
    XLaccess accessMask = static_cast<XLaccess>(1) << channelIndex;

    Status openSts      = m_OpenWithMask_locked(accessMask, u32Bitrate, u32TxId, bExtended, bFD);
    if (openSts != Status::SUCCESS) {
        VectorDriverHandle::Release();
        return openSts;
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("(via Vector Hardware Config) app:"); LOG_STRING(strAppName.c_str());
              LOG_STRING("index:"); LOG_UINT32(u32AppChannel));

    return Status::SUCCESS;
}

ICommDriver::Status Vector::openDirect(const DeviceSelector &sel,
                                       uint32_t u32Bitrate,
                                       uint32_t u32TxId,
                                       bool bExtended,
                                       bool bFD,
                                       const FdOptions &fdOpts)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (u32Bitrate == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("openDirect: invalid bitrate"));
        return Status::INVALID_PARAM;
    }

    if (bFD) {
        if (fdOpts.u32DataBitrate == 0) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("openDirect: CAN FD data bitrate must be non-zero"));
            return Status::INVALID_PARAM;
        }
        m_u32FdDataBitrate = fdOpts.u32DataBitrate;
        m_bFdIso           = fdOpts.bIso;
        m_bFdBrs           = fdOpts.bBrs;
        m_u8FdPaddingByte  = fdOpts.u8PaddingByte;
    }

    if (sel.i32HwType < 0 && sel.u32SerialNumber == 0 && sel.strChannelName.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("openDirect: DeviceSelector is empty (set at least one of "
                             "i32HwType / u32SerialNumber / strChannelName) - refusing to "
                             "match every CAN-capable channel in the system"));
        return Status::INVALID_PARAM;
    }

    // matchChannels() opens/releases the process-wide driver handle itself
    // (see enumerateChannels()) - independent of the VectorDriverHandle::Acquire()
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
        for (const auto &info : vMatches) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("  candidate:"); LOG_STRING(info.strName.c_str());
                      LOG_STRING(info.strHwType.c_str());
                      LOG_STRING("hwIndex:"); LOG_UINT32(info.u32HwIndex);
                      LOG_STRING("hwChannel:"); LOG_UINT32(info.u32HwChannel);
                      LOG_STRING("serial:"); LOG_UINT32(info.u32SerialNumber));
        }
        return Status::INVALID_PARAM;
    }

    const ChannelInfo &matched = vMatches.front();

    if (bFD && !matched.bSupportsCanFdIso && !matched.bSupportsCanFdBosch) {
        LOG_PRINT(LOG_WARNING, LOG_HDR;
                  LOG_STRING("openDirect: matched channel does not advertise CAN FD support - "
                             "attempting to open anyway, xlCanFdSetConfiguration will fail if it truly can't:");
                  LOG_STRING(matched.strName.c_str()));
    }

    Status s = VectorDriverHandle::Acquire();
    if (s != Status::SUCCESS) {
        return s;
    }

    Status openSts = m_OpenWithMask_locked(matched.xlChannelMask, u32Bitrate, u32TxId, bExtended, bFD);
    if (openSts != Status::SUCCESS) {
        VectorDriverHandle::Release();
        return openSts;
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
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

    if (VectorDriverHandle::Acquire() != Status::SUCCESS) {
        return vResult;
    }

#if defined(_WIN32)

    XLdriverConfig cfg;
    std::memset(&cfg, 0, sizeof(cfg));

    XLstatus sts = xlGetDriverConfig(&cfg);
    if (sts != XL_SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("xlGetDriverConfig failed:"); LOG_STRING(xlGetErrorString(sts)));
        VectorDriverHandle::Release();
        return vResult;
    }

    const uint32_t count = std::min<uint32_t>(cfg.channelCount,
                                              static_cast<uint32_t>(XL_CONFIG_MAX_CHANNELS));

    for (uint32_t i = 0; i < count; ++i) {
        const auto &ch = cfg.channel[i];

        ChannelInfo info;
        info.strName             = std::string(ch.name, strnlen(ch.name, sizeof(ch.name)));
        info.u32HwType           = ch.hwType;
        info.strHwType           = hwTypeToString(ch.hwType);
        info.u32HwIndex          = ch.hwIndex;
        info.u32HwChannel        = ch.hwChannel;
        info.u32ChannelIndex     = ch.channelIndex;
        info.xlChannelMask       = ch.channelMask;
        info.u32SerialNumber     = ch.serialNumber;
        info.bIsOnBus            = (ch.isOnBus != 0);
        info.bSupportsCan        = (ch.channelBusCapabilities & XL_BUS_ACTIVE_CAP_CAN) != 0U;
        info.bSupportsCanFdIso   = (ch.channelCapabilities & XL_CHANNEL_FLAG_CANFD_ISO_SUPPORT) != 0U;
        info.bSupportsCanFdBosch = (ch.channelCapabilities & XL_CHANNEL_FLAG_CANFD_BOSCH_SUPPORT) != 0U;
        info.bSupportsEthernet   = (ch.channelBusCapabilities & XL_BUS_ACTIVE_CAP_ETHERNET) != 0U;
        info.bSupportsLin        = (ch.channelBusCapabilities & XL_BUS_ACTIVE_CAP_LIN) != 0U;

        vResult.push_back(std::move(info));
    }

#elif defined(__linux__)

    // The Linux port doesn't implement xlGetDriverConfig() either (confirmed
    // against the actual exported symbols, same as xlOpenPort() - see
    // m_OpenWithMask_locked()'s comment). The replacement is a small
    // versioned interface: xlCreateDriverConfig() fills in a context handle
    // plus a table of accessor function pointers (fctGetChannelConfig()/
    // fctGetDeviceConfig()/...) for the requested XLIdriverConfigVersion -
    // XL_IDRIVER_CONFIG_VERSION_1 corresponds to the XLapiIDriverConfigV1
    // struct layout we fill below (see vxlapi_linux.h's own struct s_xlapi_driver_config_v1
    // and its "[OUT] the context handle and the function pointer table of
    // the requested version" doc comment).
    //
    // Unlike XLchannelConfig (Windows), a V1 channel entry has no name or
    // serial number of its own - those live on the owning device (cross-
    // referenced here via ch.deviceIndex into the device list) - so
    // ChannelInfo::strName is synthesised as "<device name> Channel <n>"
    // to match Vector's own Windows channel-naming convention.
    XLapiIDriverConfigV1 configIface;
    std::memset(&configIface, 0, sizeof(configIface));

    XLstatus sts = xlCreateDriverConfig(XL_IDRIVER_CONFIG_VERSION_1,
                                        reinterpret_cast<struct XLIDriverConfig *>(&configIface));
    if (sts != XL_SUCCESS || configIface.fctGetChannelConfig == nullptr || configIface.fctGetDeviceConfig == nullptr) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("xlCreateDriverConfig failed:"); LOG_STRING(xlGetErrorString(sts)));
        VectorDriverHandle::Release();
        return vResult;
    }

    XLchannelDrvConfigListV1 channelList;
    std::memset(&channelList, 0, sizeof(channelList));
    sts = configIface.fctGetChannelConfig(configIface.configHandle, &channelList);
    if (sts != XL_SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("fctGetChannelConfig failed:"); LOG_STRING(xlGetErrorString(sts)));
        xlDestroyDriverConfig(configIface.configHandle);
        VectorDriverHandle::Release();
        return vResult;
    }

    XLdeviceDrvConfigListV1 deviceList;
    std::memset(&deviceList, 0, sizeof(deviceList));
    sts = configIface.fctGetDeviceConfig(configIface.configHandle, &deviceList);
    if (sts != XL_SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("fctGetDeviceConfig failed:"); LOG_STRING(xlGetErrorString(sts)));
        xlDestroyDriverConfig(configIface.configHandle);
        VectorDriverHandle::Release();
        return vResult;
    }

    for (unsigned int i = 0; i < channelList.count; ++i) {
        const auto &ch = channelList.item[i];

        ChannelInfo info;
        info.u32HwChannel         = ch.hwChannel;
        info.u32ChannelIndex      = ch.channelIndex;
        info.xlChannelMask        = static_cast<XLaccess>(1) << ch.channelIndex;
        info.bIsOnBus             = (ch.isOnBus != 0);
        info.bSupportsCan         = (ch.channelBusActiveCapabilities & XL_BUS_ACTIVE_CAP_CAN) != 0U;
        info.bSupportsCanFdIso    = (ch.channelCapabilities & XL_CHANNEL_FLAG_CANFD_ISO_SUPPORT) != 0U;
        info.bSupportsCanFdBosch  = (ch.channelCapabilities & XL_CHANNEL_FLAG_CANFD_BOSCH_SUPPORT) != 0U;
        info.bSupportsEthernet    = (ch.channelBusActiveCapabilities & XL_BUS_ACTIVE_CAP_ETHERNET) != 0U;
        info.bSupportsLin         = (ch.channelBusActiveCapabilities & XL_BUS_ACTIVE_CAP_LIN) != 0U;

        std::string strDeviceName = "?";
        if (ch.deviceIndex < deviceList.count) {
            const auto &dev      = deviceList.item[ch.deviceIndex];
            info.u32HwType       = dev.hwType;
            info.u32HwIndex      = dev.hwIndex;
            info.u32SerialNumber = dev.serialNumber;
            if (dev.name != nullptr) {
                strDeviceName = dev.name;
            }
        } else {
            LOG_PRINT(LOG_WARNING, LOG_HDR;
                      LOG_STRING("channel reports a deviceIndex past the end of the device list:");
                      LOG_UINT32(ch.deviceIndex));
        }

        info.strHwType = hwTypeToString(info.u32HwType);
        info.strName   = strDeviceName + " Channel " + std::to_string(ch.hwChannel + 1);

        vResult.push_back(std::move(info));
    }

    xlDestroyDriverConfig(configIface.configHandle);

#endif

    VectorDriverHandle::Release();
    return vResult;
}

std::vector<Vector::ChannelInfo> Vector::matchChannels(const DeviceSelector &sel)
{
    std::vector<ChannelInfo> vResult;

    for (auto &info : enumerateChannels()) {
        if (!info.bSupportsCan) {
            continue;
        }
        if (sel.i32HwType >= 0 && info.u32HwType != static_cast<uint32_t>(sel.i32HwType)) {
            continue;
        }
        if (sel.u32SerialNumber != 0 && info.u32SerialNumber != sel.u32SerialNumber) {
            continue;
        }
        if (!sel.strChannelName.empty() && info.strName != sel.strChannelName) {
            continue;
        }
        if (sel.i32HwIndex >= 0 && info.u32HwIndex != static_cast<uint32_t>(sel.i32HwIndex)) {
            continue;
        }
        if (sel.i32HwChannel >= 0 && info.u32HwChannel != static_cast<uint32_t>(sel.i32HwChannel)) {
            continue;
        }

        vResult.push_back(std::move(info));
    }

    return vResult;
}

namespace {
// Name <-> XL_HWTYPE_* lookup table, covering every CAN-relevant
// XL_HWTYPE_* constant declared in Vector's real vxlapi.h (some very old
// ISA/PCI-era types and a few non-CAN-only types are included too, since
// hwTypeToString()/hwTypeFromString() are also used by VECTOR.DEVICES'
// listing, independent of whether the channel actually supports CAN).
struct HwTypeEntry
{
    const char *name;
    uint32_t value;
};

constexpr HwTypeEntry k_hwTypeTable[] = {
    {"NONE", XL_HWTYPE_NONE},
    {"VIRTUAL", XL_HWTYPE_VIRTUAL},
    {"CANCARDX", XL_HWTYPE_CANCARDX},
    {"CANAC2PCI", XL_HWTYPE_CANAC2PCI},
    {"CANCARDY", XL_HWTYPE_CANCARDY},
    {"CANCARDXL", XL_HWTYPE_CANCARDXL},
    {"CANCASEXL", XL_HWTYPE_CANCASEXL},
    {"CANBOARDXL", XL_HWTYPE_CANBOARDXL},
    {"CANBOARDXL_PXI", XL_HWTYPE_CANBOARDXL_PXI},
    {"VN2600", XL_HWTYPE_VN2600},
    {"VN3300", XL_HWTYPE_VN3300},
    {"VN3600", XL_HWTYPE_VN3600},
    {"VN7600", XL_HWTYPE_VN7600},
    {"CANCARDXLE", XL_HWTYPE_CANCARDXLE},
    {"VN8900", XL_HWTYPE_VN8900},
    {"VN8950", XL_HWTYPE_VN8950},
    {"VN2640", XL_HWTYPE_VN2640},
    {"VN1610", XL_HWTYPE_VN1610},
    {"VN1614", XL_HWTYPE_VN1614},
    {"VN1630", XL_HWTYPE_VN1630},
    {"VN1615", XL_HWTYPE_VN1615},
    {"VN1640", XL_HWTYPE_VN1640},
    {"VN8970", XL_HWTYPE_VN8970},
    {"VN1611", XL_HWTYPE_VN1611},
    {"VN5240", XL_HWTYPE_VN5240},
    {"VN5610", XL_HWTYPE_VN5610},
    {"VN5620", XL_HWTYPE_VN5620},
    {"VN7570", XL_HWTYPE_VN7570},
    {"VN5650", XL_HWTYPE_VN5650},
    {"VN5611", XL_HWTYPE_VN5611},
    {"VN5612", XL_HWTYPE_VN5612},
    {"VX1121", XL_HWTYPE_VX1121},
    {"VX1131", XL_HWTYPE_VX1131},
    {"VT6204", XL_HWTYPE_VT6204},
    {"VN5614", XL_HWTYPE_VN5614},
    {"VN1630_LOG", XL_HWTYPE_VN1630_LOG},
    {"VN7610", XL_HWTYPE_VN7610},
    {"VN7572", XL_HWTYPE_VN7572},
    {"VN8972", XL_HWTYPE_VN8972},
    {"VN1641", XL_HWTYPE_VN1641},
    {"VN0601", XL_HWTYPE_VN0601},
    {"VT6104B", XL_HWTYPE_VT6104B},
    {"VN5640", XL_HWTYPE_VN5640},
    {"VT6204B", XL_HWTYPE_VT6204B},
    {"VX0312", XL_HWTYPE_VX0312},
    {"VH6501", XL_HWTYPE_VH6501},
    {"VN8800", XL_HWTYPE_VN8800},
    {"VN5610A", XL_HWTYPE_VN5610A},
    {"VN7640", XL_HWTYPE_VN7640},
    {"VX1135", XL_HWTYPE_VX1135},
    {"VN4610", XL_HWTYPE_VN4610},
    {"VT6306", XL_HWTYPE_VT6306},
    {"VT6104A", XL_HWTYPE_VT6104A},
    {"VN5430", XL_HWTYPE_VN5430},
    {"VN1530", XL_HWTYPE_VN1530},
    {"VN1531", XL_HWTYPE_VN1531},
    {"VX1161A", XL_HWTYPE_VX1161A},
    {"VX1161B", XL_HWTYPE_VX1161B},
    {"VN1670", XL_HWTYPE_VN1670},
    {"VN5620A", XL_HWTYPE_VN5620A},
};
} // namespace

std::string Vector::hwTypeToString(uint32_t u32HwType)
{
    for (const auto &e : k_hwTypeTable) {
        if (e.value == u32HwType) {
            return e.name;
        }
    }
    char buf[24];
    std::snprintf(buf, sizeof(buf), "HWTYPE_%u", u32HwType);
    return buf;
}

bool Vector::hwTypeFromString(const std::string &strName, uint32_t &u32HwType)
{
    if (strName.empty()) {
        return false;
    }

    // Try a raw numeric value first (decimal or 0x-hex), same convention as
    // resolveTxId()/resolveRxId().
    if (parseUint32(strName, u32HwType)) {
        return true;
    }

    std::string strUpper(strName);
    std::transform(strUpper.begin(), strUpper.end(), strUpper.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    for (const auto &e : k_hwTypeTable) {
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
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Vector channel closed"));
        m_xlPort       = XL_INVALID_PORTHANDLE;
        m_xlAccessMask = 0;
        m_notifyWaiter.close();
        m_bOpen = false;
        VectorDriverHandle::Release();
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

ICommDriver::Status Vector::recvFrame(uint32_t u32TimeoutMs, VectorRxFrame &out, std::stop_token stop_tok) const
{
    // Registered once for the whole call (not per wait-iteration below) so
    // a stop request at any point during this recvFrame() wakes whichever
    // wait() happens to be blocked at the time — see VectorNotifyWaiter's
    // class comment for why forceWake() is safe on both platforms and why
    // every return point below re-checks stop_tok.stop_requested() rather
    // than trusting a signalled wait alone.
    std::stop_callback onStop(stop_tok, [this]() {
        m_notifyWaiter.forceWake();
    });

    for (;;) {
        if (stop_tok.stop_requested()) {
            return Status::READ_TIMEOUT;
        }

        if (m_bFD) {
            // ---- CAN FD path: xlCanReceive()/XLcanRxEvent -----------------
            XLcanRxEvent evt;
            XLstatus sts = xlCanReceive(m_xlPort, &evt);

            if (sts == XL_SUCCESS) {
                if (evt.tag == XL_CAN_EV_TAG_RX_OK) {
                    const auto &msg = evt.tagData.canRxOkMsg;
                    const bool ext  = (msg.canId & XL_CAN_EXT_MSG_ID) != 0U;

                    out.u32Id       = msg.canId & CAN_EFF_MASK;
                    out.bExtended   = ext;
                    out.u8Len       = static_cast<uint8_t>(
                        std::min<size_t>(VECTOR_FD_MAX_PAYLOAD,
                                         CANFD_GET_NUM_DATABYTES(msg.dlc,
                                                                 (msg.msgFlags & XL_CAN_RXMSG_FLAG_EDL) != 0U,
                                                                 (msg.msgFlags & XL_CAN_RXMSG_FLAG_RTR) != 0U)));
                    std::memcpy(out.data.data(), msg.data, out.u8Len);
                    return Status::SUCCESS;
                }
                // Any other tag (TX_OK echo, TX_REQUEST, RX_ERROR, TX_ERROR,
                // CHIP_STATE, SYNC_PULSE, ...) - not real received data, keep
                // draining without re-waiting, there may be more queued behind it.
                continue;
            }

            if (sts != XL_ERR_QUEUE_IS_EMPTY) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("xlCanReceive error:"); LOG_STRING(xlGetErrorString(sts)));
                return Status::READ_ERROR;
            }
            // fall through to the shared wait-and-retry below

        } else {
            // ---- Classic CAN path: xlReceive()/XLevent ---------------------
            XLevent evt;
            unsigned int msgCount = 1;
            XLstatus sts          = xlReceive(m_xlPort, &msgCount, &evt);

            if (sts == XL_SUCCESS && msgCount > 0) {
                if (evt.tag == XL_RECEIVE_MSG) {
                    static constexpr uint16_t k_invalidDataFlags = XL_CAN_MSG_FLAG_ERROR_FRAME | XL_CAN_MSG_FLAG_OVERRUN | XL_CAN_MSG_FLAG_NERR;
                    // XL_CAN_MSG_FLAG_TX_COMPLETED - XL-API echoes every frame
                    // THIS port transmits back through xlReceive() as a
                    // confirmation event; skipping it is what stops
                    // tout_read() from reading back its own just-sent request
                    // instead of waiting for a peer's response.
                    if ((evt.tagData.msg.flags & k_invalidDataFlags) ||
                        (evt.tagData.msg.flags & XL_CAN_MSG_FLAG_TX_COMPLETED)) {
                        continue;
                    }

                    const bool ext = (evt.tagData.msg.id & XL_CAN_EXT_MSG_ID) != 0U;
                    out.u32Id      = evt.tagData.msg.id & CAN_EFF_MASK;
                    out.bExtended  = ext;
                    out.u8Len      = static_cast<uint8_t>(std::min<uint16_t>(VECTOR_MAX_PAYLOAD, evt.tagData.msg.dlc));
                    std::memcpy(out.data.data(), evt.tagData.msg.data, out.u8Len);
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
            // fall through to the shared wait-and-retry below
        }

        // Queue empty — wait for the notification, then retry.
        VectorNotifyWaiter::WaitResult waitResult = m_notifyWaiter.wait(u32TimeoutMs, stop_tok);

        if (stop_tok.stop_requested()) {
            // waitResult may be SIGNALLED because of our own forceWake()
            // above rather than a real frame arriving — treat it as a
            // (harmless, cooperative) timeout either way.
            return Status::READ_TIMEOUT;
        }

        if (waitResult == VectorNotifyWaiter::WaitResult::TIMEOUT) {
            return Status::READ_TIMEOUT;
        }
        if (waitResult != VectorNotifyWaiter::WaitResult::SIGNALLED) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("notification wait failed"));
            return Status::READ_ERROR;
        }
        // Signalled -- loop back and drain the receive call again.
    }
}

ICommDriver::Status Vector::sendFrame(uint32_t u32Id,
                                      bool bExtended,
                                      std::span<const uint8_t> data) const
{
    const size_t maxPayload = m_bFD ? VECTOR_FD_MAX_PAYLOAD : VECTOR_MAX_PAYLOAD;

    if (data.size() > maxPayload) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("sendFrame: payload too large"); LOG_SIZET(data.size()));
        return Status::INVALID_PARAM;
    }

    if (m_bFD) {
        // ---- CAN FD path: xlCanTransmitEx()/XLcanTxEvent -------------------
        const uint8_t u8Dlc     = canFdLenToDlc(data.size());
        const size_t szFrameLen = canFdDlcToLen(u8Dlc);

        XLcanTxEvent evt;
        std::memset(&evt, 0, sizeof(evt));
        evt.tag                     = XL_CAN_EV_TAG_TX_MSG;
        evt.tagData.canMsg.canId    = bExtended ? (u32Id | XL_CAN_EXT_MSG_ID) : u32Id;
        evt.tagData.canMsg.dlc      = u8Dlc;
        evt.tagData.canMsg.msgFlags = XL_CAN_TXMSG_FLAG_EDL | (m_bFdBrs ? XL_CAN_TXMSG_FLAG_BRS : 0U);

        std::memset(evt.tagData.canMsg.data, m_u8FdPaddingByte, sizeof(evt.tagData.canMsg.data));
        std::memcpy(evt.tagData.canMsg.data, data.data(), data.size());

        unsigned int msgCount = 1, msgCountSent = 0;
        XLstatus sts = xlCanTransmitEx(m_xlPort, m_xlAccessMask, msgCount, &msgCountSent, &evt);
        if (sts != XL_SUCCESS || msgCountSent == 0) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("xlCanTransmitEx failed:"); LOG_STRING(xlGetErrorString(sts));
                      LOG_STRING("ID:"); LOG_HEX32(u32Id));
            return Status::WRITE_ERROR;
        }

        // Dump exactly what the caller asked to send, not the DLC-padded
        // on-wire length — the padding bytes are a wire-format artefact, not
        // application payload.
        dumpFrame(CommDir::Tx, u32Id, bExtended, data);
        (void)szFrameLen;

    } else {
        // ---- Classic CAN path: xlCanTransmit()/XLevent ----------------------
        XLevent evt;
        std::memset(&evt, 0, sizeof(evt));
        evt.tag               = XL_TRANSMIT_MSG;
        evt.tagData.msg.id    = bExtended ? (u32Id | XL_CAN_EXT_MSG_ID) : u32Id;
        evt.tagData.msg.dlc   = static_cast<uint16_t>(data.size());
        evt.tagData.msg.flags = 0;
        std::memcpy(evt.tagData.msg.data, data.data(), data.size());

        unsigned int msgCount = 1;
        XLstatus sts          = xlCanTransmit(m_xlPort, m_xlAccessMask, &msgCount, &evt);
        if (sts != XL_SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("xlCanTransmit failed:"); LOG_STRING(xlGetErrorString(sts));
                      LOG_STRING("ID:"); LOG_HEX32(u32Id));
            return Status::WRITE_ERROR;
        }

        dumpFrame(CommDir::Tx, u32Id, bExtended, data);
    }

    return Status::SUCCESS;
}

// ============================================================================
// READ-MODE IMPLEMENTATIONS (identical structure to uPcan.cpp)
// ============================================================================

void Vector::buildKmpTable(std::span<const uint8_t> pattern, std::vector<int> &viLps)
{
    const size_t n = pattern.size();
    viLps.assign(n, 0);
    int len = 0;

    for (size_t i = 1; i < n;) {
        if (pattern[i] == pattern[len]) {
            viLps[i++] = ++len;
        } else if (len != 0) {
            len = viLps[len - 1];
        } else {
            viLps[i++] = 0;
        }
    }
}

ICommDriver::Status Vector::readExact(uint32_t u32TimeoutMs,
                                      std::span<uint8_t> buffer,
                                      size_t &szBytesRead,
                                      uint32_t u32RxFilterId,
                                      std::stop_token stop_tok) const
{
    szBytesRead = 0;
    VectorRxFrame frame;

    while (szBytesRead < buffer.size()) {
        Status s = recvFrame(u32TimeoutMs, frame, stop_tok);
        if (s != Status::SUCCESS) {
            return s;
        }

        if (!frameMatchesFilter(frame, u32RxFilterId)) {
            continue;
        }

        dumpFrame(CommDir::Rx, frame.u32Id, frame.bExtended,
                  std::span<const uint8_t>(frame.data.data(), frame.u8Len));

        size_t toCopy = std::min<size_t>(frame.u8Len, buffer.size() - szBytesRead);
        std::memcpy(buffer.data() + szBytesRead, frame.data.data(), toCopy);
        szBytesRead += toCopy;
    }

    return Status::SUCCESS;
}

ICommDriver::Status Vector::readUntilDelimiter(uint32_t u32TimeoutMs,
                                               std::span<uint8_t> buffer,
                                               uint8_t cDelimiter,
                                               size_t &szBytesRead,
                                               uint32_t u32RxFilterId,
                                               std::stop_token stop_tok) const
{
    if (buffer.size() < 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("readUntilDelimiter: buffer too small"));
        return Status::INVALID_PARAM;
    }

    szBytesRead = 0;
    VectorRxFrame frame;

    while (true) {
        Status s = recvFrame(u32TimeoutMs, frame, stop_tok);
        if (s != Status::SUCCESS) {
            return s;
        }

        if (!frameMatchesFilter(frame, u32RxFilterId)) {
            continue;
        }

        dumpFrame(CommDir::Rx, frame.u32Id, frame.bExtended,
                  std::span<const uint8_t>(frame.data.data(), frame.u8Len));

        for (size_t i = 0; i < frame.u8Len; ++i) {
            uint8_t ch = frame.data[i];
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

ICommDriver::Status Vector::readUntilToken(uint32_t u32TimeoutMs,
                                           std::span<const uint8_t> token,
                                           uint32_t u32RxFilterId,
                                           std::stop_token stop_tok) const
{
    if (token.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("readUntilToken: empty token"));
        return Status::INVALID_PARAM;
    }

    std::vector<int> viLps;
    buildKmpTable(token, viLps);

    VectorRxFrame frame;
    size_t szMatched = 0;

    while (true) {
        Status s = recvFrame(u32TimeoutMs, frame, stop_tok);
        if (s != Status::SUCCESS) {
            return s;
        }

        if (!frameMatchesFilter(frame, u32RxFilterId)) {
            continue;
        }

        dumpFrame(CommDir::Rx, frame.u32Id, frame.bExtended,
                  std::span<const uint8_t>(frame.data.data(), frame.u8Len));

        for (size_t i = 0; i < frame.u8Len; ++i) {
            uint8_t ch = frame.data[i];

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

ICommDriver::ReadResult Vector::readOneFrame_locked(uint32_t u32TimeoutMs,
                                                    std::span<uint8_t> buffer,
                                                    std::string_view xtra_params) const
{
    // ASSUMES m_mutex IS ALREADY HELD (see class comment / RawIo).
    ReadResult result;
    const uint32_t rxFilterId = resolveRxId(xtra_params);
    VectorRxFrame frame;

    while (true) {
        // TODO(stop-token): readOneFrame_locked() is invoked as a callback
        // through the transport-protocol interface (RawIo), which doesn't
        // carry a stop_token — same gap PCAN::readOneFrame_locked() documents.
        // A segmented Vector transfer (ISO-TP/J1939) is therefore not yet
        // cancellable via the STOP button.
        Status s = recvFrame(u32TimeoutMs, frame);
        if (s != Status::SUCCESS) {
            result.status = s;
            return result;
        }
        if (!frameMatchesFilter(frame, rxFilterId)) {
            continue;
        }
        break;
    }

    dumpFrame(CommDir::Rx, frame.u32Id, frame.bExtended,
              std::span<const uint8_t>(frame.data.data(), frame.u8Len));

    const size_t toCopy = std::min<size_t>(frame.u8Len, buffer.size());
    if (toCopy > 0) {
        std::memcpy(buffer.data(), frame.data.data(), toCopy);
    }
    if (toCopy < frame.u8Len) {
        LOG_PRINT(LOG_WARNING, LOG_HDR;
                  LOG_STRING("readOneFrame_locked: frame truncated, buffer too small"));
    }

    result.status     = Status::SUCCESS;
    result.bytes_read = toCopy;
    return result;
}

ICommDriver::ReadResult Vector::readDispatch_locked(uint32_t u32ReadTimeout,
                                                    std::span<uint8_t> buffer,
                                                    const ReadOptions &options,
                                                    std::string_view xtra_params,
                                                    std::stop_token stop_tok) const
{
    // ASSUMES m_mutex IS ALREADY HELD (see class comment / RawIo).
    ReadResult result;

    const uint32_t timeout    = u32ReadTimeout;
    const uint32_t rxFilterId = resolveRxId(xtra_params);

    switch (options.mode) {

    case ReadMode::Exact: {
        size_t bytesRead        = 0;
        result.status           = readExact(timeout, buffer, bytesRead, rxFilterId, stop_tok);
        result.bytes_read       = bytesRead;
        result.found_terminator = false;
        break;
    }

    case ReadMode::UntilDelimiter: {
        size_t bytesRead        = 0;
        result.status           = readUntilDelimiter(timeout, buffer,
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

ICommDriver::ReadResult Vector::tout_read(uint32_t u32ReadTimeout,
                                          std::span<uint8_t> buffer,
                                          const ReadOptions &options,
                                          std::string_view xtra_params,
                                          std::stop_token stop_tok) const
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

ICommDriver::WriteResult Vector::writeFragmented_locked(uint32_t u32WriteTimeout,
                                                        std::span<const uint8_t> buffer,
                                                        std::string_view xtra_params) const
{
    // ASSUMES m_mutex IS ALREADY HELD (see class comment / RawIo).
    WriteResult result;

    (void)u32WriteTimeout; // xlCanTransmit(Ex) is non-blocking; timeout reserved for future use.

    const uint32_t u32TxId    = resolveTxId(xtra_params);
    const bool bExtended      = m_bExtendedId || (u32TxId & CAN_EFF_FLAG) != 0U ||
                                ((u32TxId & CAN_EFF_MASK) > CAN_SFF_MASK);
    const uint32_t u32RawTxId = u32TxId & (bExtended ? CAN_EFF_MASK : CAN_SFF_MASK);
    const size_t maxPayload   = m_bFD ? VECTOR_FD_MAX_PAYLOAD : VECTOR_MAX_PAYLOAD;

    size_t offset             = 0;
    while (offset < buffer.size()) {
        size_t frameLen = std::min(maxPayload, buffer.size() - offset);
        Status s        = sendFrame(u32RawTxId, bExtended, buffer.subspan(offset, frameLen));
        if (s != Status::SUCCESS) {
            result.status        = s;
            result.bytes_written = offset;
            return result;
        }
        offset += frameLen;
    }

    result.status        = Status::SUCCESS;
    result.bytes_written = buffer.size();

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("tout_write: sent"); LOG_SIZET(result.bytes_written);
              LOG_STRING("bytes, TX ID:"); LOG_HEX32(u32RawTxId));

    return result;
}

ICommDriver::WriteResult Vector::tout_write(uint32_t u32WriteTimeout,
                                            std::span<const uint8_t> buffer,
                                            std::string_view xtra_params,
                                            std::stop_token /*stop_tok*/) const
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
