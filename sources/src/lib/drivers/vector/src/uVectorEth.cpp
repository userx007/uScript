#include "uVectorEth.hpp"
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

#define LT_HDR   "VECTORETH_DRV|"
#define LOG_HDR  LOG_STRING(LT_HDR)

namespace {
    /** Ethernet frames carry EtherType/length fields in network (big-endian) byte
     *  order; every XL-API host this driver targets (x86/x64 Windows) is
     *  little-endian, so this swap is unconditional - there is no htons()
     *  dependency to pull in winsock2.h for. */
    constexpr uint16_t hostToNetU16(uint16_t v)
    {
        return static_cast<uint16_t>((v << 8) | (v >> 8));
    }
}


// ============================================================================
// MAC ADDRESS HELPERS
// ============================================================================

bool VectorEth::parseMac(std::string_view sv, MacAddress& out)
{
    MacAddress result{};
    size_t     byteIdx = 0;
    size_t     pos     = 0;

    while (byteIdx < 6) {
        if (pos + 2 > sv.size()) return false;

        auto [ptr, ec] = std::from_chars(sv.data() + pos, sv.data() + pos + 2, result[byteIdx], 16);
        if (ec != std::errc{} || ptr != sv.data() + pos + 2) return false;

        pos += 2;
        ++byteIdx;

        if (byteIdx == 6) break;
        if (pos >= sv.size() || (sv[pos] != ':' && sv[pos] != '-')) return false;
        ++pos;
    }

    if (pos != sv.size()) return false;

    out = result;
    return true;
}


std::string VectorEth::formatMac(const MacAddress& mac)
{
    char buf[18];
    std::snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buf;
}


void VectorEth::resolveDest(std::string_view xtra_params, MacAddress& outMac, uint16_t& outEtherType) const
{
    outMac       = m_defaultDestMac;
    outEtherType = m_u16DefaultEtherType;

    if (xtra_params.empty()) return;

    const auto slashPos = xtra_params.find('/');
    const std::string_view macPart  = xtra_params.substr(0, slashPos);
    const std::string_view typePart = (slashPos == std::string_view::npos)
                                       ? std::string_view{}
                                       : xtra_params.substr(slashPos + 1);

    if (!macPart.empty()) {
        MacAddress mac;
        if (parseMac(macPart, mac)) {
            outMac = mac;
        } else {
            LOG_PRINT(LOG_WARNING, LOG_HDR;
                      LOG_STRING("resolveDest: cannot parse MAC in xtra_params, using default:");
                      LOG_STRING(macPart.data()));
        }
    }

    if (!typePart.empty()) {
        uint32_t u32Type = 0;
        int base = 10;
        std::string_view sv = typePart;
        if (sv.size() > 2 && sv[0] == '0' && (sv[1] == 'x' || sv[1] == 'X')) {
            sv = sv.substr(2);
            base = 16;
        }
        auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), u32Type, base);
        if (ec == std::errc{} && ptr == sv.data() + sv.size() && u32Type <= 0xFFFFU) {
            outEtherType = static_cast<uint16_t>(u32Type);
        } else {
            LOG_PRINT(LOG_WARNING, LOG_HDR;
                      LOG_STRING("resolveDest: cannot parse EtherType in xtra_params, using default:");
                      LOG_STRING(typePart.data()));
        }
    }
}


// ============================================================================
// STATIC HELPERS
// ============================================================================

ICommDriver::Status VectorEth::mapXlError(XLstatus sts)
{
    if (sts == XL_SUCCESS)             return Status::SUCCESS;
    if (sts == XL_ERR_QUEUE_IS_EMPTY)  return Status::READ_TIMEOUT;
    if (sts == XL_ERR_INVALID_ACCESS)  return Status::PORT_ACCESS;
    if (sts == XL_ERR_PORT_IS_OFFLINE) return Status::PORT_ACCESS;
    return Status::READ_ERROR;
}


void VectorEth::dumpFrame(CommDir dir, const MacAddress& peerMac, uint16_t u16EtherType, std::span<const uint8_t> data) const
{
    if (!gui_mode_active()) {
        return;
    }
    char label[k_labelSize];
    std::snprintf(label, sizeof(label), "%s %s=%s type=0x%04X",
                  m_strIdentityLabel.empty() ? "VectorEth" : m_strIdentityLabel.c_str(),
                  (dir == CommDir::Tx) ? "dst" : "src",
                  formatMac(peerMac).c_str(), u16EtherType);
    gui_notify_comm_dump(m_strInstanceName, commdump_details(CommFamily::NET, label),
                          dir, data.data(), static_cast<uint32_t>(data.size()));
}


bool VectorEth::frameMatchesFilter(const VectorEthRxFrame& frame) const
{
    if (m_rxFilterSrcMac.has_value() && frame.srcMac != m_rxFilterSrcMac.value()) {
        return false;
    }
    if (m_rxFilterEtherType.has_value() && frame.u16EtherType != m_rxFilterEtherType.value()) {
        return false;
    }
    return true;
}


std::vector<Vector::ChannelInfo> VectorEth::matchChannels(const Vector::DeviceSelector& sel)
{
    std::vector<Vector::ChannelInfo> vResult;

    for (auto& info : Vector::enumerateChannels()) {
        if (!info.bSupportsEthernet) continue;
        if (sel.i32HwType >= 0 && info.u32HwType != static_cast<uint32_t>(sel.i32HwType)) continue;
        if (sel.u32SerialNumber != 0 && info.u32SerialNumber != sel.u32SerialNumber) continue;
        if (!sel.strChannelName.empty() && info.strName != sel.strChannelName) continue;
        if (sel.i32HwIndex >= 0 && info.u32HwIndex != static_cast<uint32_t>(sel.i32HwIndex)) continue;
        if (sel.i32HwChannel >= 0 && info.u32HwChannel != static_cast<uint32_t>(sel.i32HwChannel)) continue;

        vResult.push_back(std::move(info));
    }

    return vResult;
}


// ============================================================================
// LIFECYCLE
// ============================================================================

ICommDriver::Status VectorEth::m_OpenWithMask_locked(XLaccess accessMask)
{
    // ASSUMES m_mutex IS ALREADY HELD and VectorDriverHandle::Acquire() has
    // ALREADY been called by the caller (open() / openDirect()).

    if (accessMask == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("m_OpenWithMask_locked: empty access mask"));
        return Status::PORT_ACCESS;
    }

    XLportHandle portHandle = XL_INVALID_PORTHANDLE;
    XLaccess     permissionMask = accessMask;

    XLstatus sts = xlOpenPort(&portHandle, const_cast<char*>("VectorEth"), accessMask, &permissionMask,
                              VECTOR_ETH_RX_QUEUE_SIZE, XL_INTERFACE_VERSION_V4, XL_BUS_TYPE_ETHERNET);
    if (sts != XL_SUCCESS || portHandle == XL_INVALID_PORTHANDLE) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("xlOpenPort failed:"); LOG_STRING(xlGetErrorString(sts)));
        return Status::PORT_ACCESS;
    }

    if ((permissionMask & accessMask) == accessMask) {
        T_XL_ETH_CONFIG cfg;
        std::memset(&cfg, 0, sizeof(cfg));
        cfg.speed     = m_phyConfig.u32Speed;
        cfg.duplex    = m_phyConfig.u32Duplex;
        cfg.connector = m_phyConfig.u32Connector;
        cfg.phy       = m_phyConfig.u32Phy;
        cfg.clockMode = m_phyConfig.u32ClockMode;
        cfg.mdiMode   = m_phyConfig.u32MdiMode;
        cfg.brPairs   = m_phyConfig.u32BrPairs;

        sts = xlEthSetConfig(portHandle, accessMask, 0, &cfg);
        if (sts != XL_SUCCESS) {
            // Non-fatal: some Ethernet channels (e.g. plain RJ-45-only boards)
            // reject settings that only make sense for BroadR-Reach and
            // similar — log and continue with whatever the hardware defaults
            // to, same tolerance uVector's bitrate path has for a
            // permission-denied bitrate set.
            LOG_PRINT(LOG_WARNING, LOG_HDR;
                      LOG_STRING("xlEthSetConfig failed, continuing with hardware defaults:");
                      LOG_STRING(xlGetErrorString(sts)));
        }
    } else {
        LOG_PRINT(LOG_WARNING, LOG_HDR;
                  LOG_STRING("init access not granted for this channel - PHY config left as configured by another application"));
    }

    HANDLE hEvent = nullptr;
    sts = xlSetNotification(portHandle, &hEvent, 1);
    if (sts != XL_SUCCESS || hEvent == nullptr) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("xlSetNotification failed:"); LOG_STRING(xlGetErrorString(sts)));
        xlClosePort(portHandle);
        return Status::PORT_ACCESS;
    }

    sts = xlActivateChannel(portHandle, accessMask, XL_BUS_TYPE_ETHERNET, XL_ACTIVATE_RESET_CLOCK);
    if (sts != XL_SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("xlActivateChannel failed:"); LOG_STRING(xlGetErrorString(sts)));
        xlClosePort(portHandle);
        return Status::PORT_ACCESS;
    }

    m_xlPort       = portHandle;
    m_xlAccessMask = accessMask;
    m_hRxEvent     = hEvent;
    m_bOpen        = true;

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("VectorEth channel opened, access mask:"); LOG_HEX32(static_cast<uint32_t>(accessMask));
              LOG_STRING("default dst:"); LOG_STRING(formatMac(m_defaultDestMac).c_str());
              LOG_STRING("default EtherType:"); LOG_HEX32(m_u16DefaultEtherType));

    return Status::SUCCESS;
}


ICommDriver::Status VectorEth::open(const std::string& strAppName, uint32_t u32AppChannel)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (strAppName.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("open: invalid parameter, app is empty"));
        return Status::INVALID_PARAM;
    }

    Status s = VectorDriverHandle::Acquire();
    if (s != Status::SUCCESS) {
        return s;
    }

    unsigned int hwType = 0, hwIndex = 0, hwChannel = 0;
    std::vector<char> vAppName(strAppName.begin(), strAppName.end());
    vAppName.push_back('\0');

    XLstatus sts = xlGetApplConfig(vAppName.data(), u32AppChannel,
                                   &hwType, &hwIndex, &hwChannel, XL_BUS_TYPE_ETHERNET);
    if (sts != XL_SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("xlGetApplConfig failed - check that");
                  LOG_STRING(strAppName.c_str());
                  LOG_STRING("is configured with an Ethernet channel in Vector Hardware Config:");
                  LOG_STRING(xlGetErrorString(sts)));
        VectorDriverHandle::Release();
        return Status::PORT_ACCESS;
    }

    XLaccess accessMask = xlGetChannelMask(static_cast<int>(hwType),
                                           static_cast<int>(hwIndex),
                                           static_cast<int>(hwChannel));
    if (accessMask == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("xlGetChannelMask returned an empty mask"));
        VectorDriverHandle::Release();
        return Status::PORT_ACCESS;
    }

    Status openSts = m_OpenWithMask_locked(accessMask);
    if (openSts != Status::SUCCESS) {
        VectorDriverHandle::Release();
        return openSts;
    }

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("(via Vector Hardware Config) app:"); LOG_STRING(strAppName.c_str());
              LOG_STRING("index:"); LOG_UINT32(u32AppChannel));

    return Status::SUCCESS;
}


ICommDriver::Status VectorEth::openDirect(const Vector::DeviceSelector& sel)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (sel.i32HwType < 0 && sel.u32SerialNumber == 0 && sel.strChannelName.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("openDirect: DeviceSelector is empty (set at least one of "
                             "i32HwType / u32SerialNumber / strChannelName) - refusing to "
                             "match every Ethernet-capable channel in the system"));
        return Status::INVALID_PARAM;
    }

    std::vector<Vector::ChannelInfo> vMatches = matchChannels(sel);

    if (vMatches.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("openDirect: no Ethernet-capable channel matched the given selector "
                             "- run VECTOR_ETH.DEVICES to see what's currently visible to XL-API"));
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

    const Vector::ChannelInfo& matched = vMatches.front();

    Status s = VectorDriverHandle::Acquire();
    if (s != Status::SUCCESS) {
        return s;
    }

    Status openSts = m_OpenWithMask_locked(matched.xlChannelMask);
    if (openSts != Status::SUCCESS) {
        VectorDriverHandle::Release();
        return openSts;
    }

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("(direct selection) matched:"); LOG_STRING(matched.strName.c_str());
              LOG_STRING(matched.strHwType.c_str());
              LOG_STRING("serial:"); LOG_UINT32(matched.u32SerialNumber));

    return Status::SUCCESS;
}


ICommDriver::Status VectorEth::close()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_bOpen) {
        xlDeactivateChannel(m_xlPort, m_xlAccessMask);
        xlClosePort(m_xlPort);
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("VectorEth channel closed"));
        m_xlPort       = XL_INVALID_PORTHANDLE;
        m_xlAccessMask = 0;
        m_hRxEvent     = nullptr;
        m_bOpen        = false;
        VectorDriverHandle::Release();
    }
    return Status::SUCCESS;
}


bool VectorEth::is_open() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_bOpen;
}


// ============================================================================
// FRAME-LEVEL PRIMITIVES
// ============================================================================

ICommDriver::Status VectorEth::recvFrame(uint32_t u32TimeoutMs, VectorEthRxFrame& out, std::stop_token stop_tok) const
{
    const DWORD dwWaitTimeout = (u32TimeoutMs == 0) ? INFINITE : static_cast<DWORD>(u32TimeoutMs);

    std::stop_callback onStop(stop_tok, [this]() {
        SetEvent(m_hRxEvent);
    });

    for (;;) {
        if (stop_tok.stop_requested()) {
            return Status::READ_TIMEOUT;
        }

        T_XL_ETH_EVENT evt;
        std::memset(&evt, 0, sizeof(evt));
        evt.size = sizeof(evt);

        XLstatus sts = xlEthReceive(m_xlPort, &evt);

        if (sts == XL_SUCCESS) {
            if (evt.tag == XL_ETH_EVENT_TAG_FRAMERX) {
                const auto& rx = evt.tagData.frameRxOk;

                out.u16Len = static_cast<uint16_t>(std::min<size_t>(VECTOR_ETH_MAX_PAYLOAD, rx.dataLen));
                std::memcpy(out.destMac.data(), rx.destMAC, out.destMac.size());
                std::memcpy(out.srcMac.data(),  rx.sourceMAC, out.srcMac.size());
                out.u16EtherType = hostToNetU16(rx.frameData.ethFrame.etherType); // network -> host order
                std::memcpy(out.data.data(), rx.frameData.ethFrame.payload, out.u16Len);
                return Status::SUCCESS;
            }
            // Any other tag (TX ACK/ERROR variants, CONFIGRESULT, CHANNEL_STATUS,
            // LOSTEVENT, SYNC_PULSE, FRAMERX_ERROR, ...) - not a genuine inbound
            // data frame, keep draining without re-waiting.
            continue;
        }

        if (sts != XL_ERR_QUEUE_IS_EMPTY) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("xlEthReceive error:"); LOG_STRING(xlGetErrorString(sts)));
            return Status::READ_ERROR;
        }

        DWORD dwWait = WaitForSingleObject(m_hRxEvent, dwWaitTimeout);

        if (stop_tok.stop_requested()) {
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
        // Event fired -- loop back and drain xlEthReceive() again.
    }
}


ICommDriver::Status VectorEth::sendFrame(const MacAddress& destMac, uint16_t u16EtherType, std::span<const uint8_t> data) const
{
    if (data.size() > VECTOR_ETH_MAX_PAYLOAD) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("sendFrame: payload too large"); LOG_SIZET(data.size()));
        return Status::INVALID_PARAM;
    }

    T_XL_ETH_DATAFRAME_TX tx;
    std::memset(&tx, 0, sizeof(tx));
    tx.frameIdentifier = ++m_u32TxFrameId;
    tx.flags           = 0; // never XL_ETH_DATAFRAME_FLAGS_USE_SOURCE_MAC - hardware fills the source MAC
    tx.dataLen          = static_cast<unsigned short>(data.size());
    std::memcpy(tx.destMAC, destMac.data(), destMac.size());
    tx.frameData.ethFrame.etherType = hostToNetU16(u16EtherType); // host -> network order
    std::memcpy(tx.frameData.ethFrame.payload, data.data(), data.size());

    XLstatus sts = xlEthTransmit(m_xlPort, m_xlAccessMask, 0, &tx);
    if (sts != XL_SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("xlEthTransmit failed:"); LOG_STRING(xlGetErrorString(sts));
                  LOG_STRING("dst:"); LOG_STRING(formatMac(destMac).c_str()));
        return Status::WRITE_ERROR;
    }

    dumpFrame(CommDir::Tx, destMac, u16EtherType, data);
    return Status::SUCCESS;
}


// ============================================================================
// READ-MODE IMPLEMENTATIONS (identical structure to uVector.cpp)
// ============================================================================

void VectorEth::buildKmpTable(std::span<const uint8_t> pattern, std::vector<int>& viLps)
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


ICommDriver::Status VectorEth::readExact(uint32_t           u32TimeoutMs,
                                         std::span<uint8_t> buffer,
                                         size_t&            szBytesRead,
                                         std::stop_token    stop_tok) const
{
    szBytesRead = 0;
    VectorEthRxFrame frame;

    while (szBytesRead < buffer.size()) {
        Status s = recvFrame(u32TimeoutMs, frame, stop_tok);
        if (s != Status::SUCCESS) return s;
        if (!frameMatchesFilter(frame)) continue;

        dumpFrame(CommDir::Rx, frame.srcMac, frame.u16EtherType,
                  std::span<const uint8_t>(frame.data.data(), frame.u16Len));

        size_t toCopy = std::min<size_t>(frame.u16Len, buffer.size() - szBytesRead);
        std::memcpy(buffer.data() + szBytesRead, frame.data.data(), toCopy);
        szBytesRead += toCopy;
    }

    return Status::SUCCESS;
}


ICommDriver::Status VectorEth::readUntilDelimiter(uint32_t           u32TimeoutMs,
                                                   std::span<uint8_t> buffer,
                                                   uint8_t            cDelimiter,
                                                   size_t&            szBytesRead,
                                                   std::stop_token    stop_tok) const
{
    if (buffer.size() < 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("readUntilDelimiter: buffer too small"));
        return Status::INVALID_PARAM;
    }

    szBytesRead = 0;
    VectorEthRxFrame frame;

    while (true) {
        Status s = recvFrame(u32TimeoutMs, frame, stop_tok);
        if (s != Status::SUCCESS) return s;
        if (!frameMatchesFilter(frame)) continue;

        dumpFrame(CommDir::Rx, frame.srcMac, frame.u16EtherType,
                  std::span<const uint8_t>(frame.data.data(), frame.u16Len));

        for (size_t i = 0; i < frame.u16Len; ++i) {
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


ICommDriver::Status VectorEth::readUntilToken(uint32_t                 u32TimeoutMs,
                                              std::span<const uint8_t> token,
                                              std::stop_token          stop_tok) const
{
    if (token.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("readUntilToken: empty token"));
        return Status::INVALID_PARAM;
    }

    std::vector<int> viLps;
    buildKmpTable(token, viLps);

    VectorEthRxFrame frame;
    size_t           szMatched = 0;

    while (true) {
        Status s = recvFrame(u32TimeoutMs, frame, stop_tok);
        if (s != Status::SUCCESS) return s;
        if (!frameMatchesFilter(frame)) continue;

        dumpFrame(CommDir::Rx, frame.srcMac, frame.u16EtherType,
                  std::span<const uint8_t>(frame.data.data(), frame.u16Len));

        for (size_t i = 0; i < frame.u16Len; ++i) {
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

ICommDriver::ReadResult VectorEth::tout_read(uint32_t           u32ReadTimeout,
                                             std::span<uint8_t> buffer,
                                             const ReadOptions& options,
                                             std::string_view   /*xtra_params*/,
                                             std::stop_token    stop_tok) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    ReadResult result;

    if (!m_bOpen) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("tout_read: channel not open"));
        result.status = Status::PORT_ACCESS;
        return result;
    }

    switch (options.mode) {

        case ReadMode::Exact: {
            size_t bytesRead = 0;
            result.status           = readExact(u32ReadTimeout, buffer, bytesRead, stop_tok);
            result.bytes_read       = bytesRead;
            result.found_terminator = false;
            break;
        }

        case ReadMode::UntilDelimiter: {
            size_t bytesRead = 0;
            result.status           = readUntilDelimiter(u32ReadTimeout, buffer, options.delimiter, bytesRead, stop_tok);
            result.bytes_read       = bytesRead;
            result.found_terminator = (result.status == Status::SUCCESS);
            break;
        }

        case ReadMode::UntilToken: {
            result.status           = readUntilToken(u32ReadTimeout, options.token, stop_tok);
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


ICommDriver::WriteResult VectorEth::tout_write(uint32_t                 u32WriteTimeout,
                                               std::span<const uint8_t> buffer,
                                               std::string_view         xtra_params,
                                               std::stop_token          /*stop_tok*/) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    WriteResult result;

    if (!m_bOpen) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("tout_write: channel not open"));
        result.status = Status::PORT_ACCESS;
        return result;
    }

    if (buffer.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("tout_write: empty buffer"));
        result.status = Status::INVALID_PARAM;
        return result;
    }

    (void)u32WriteTimeout; // xlEthTransmit is non-blocking; timeout reserved for future use.

    MacAddress destMac;
    uint16_t   etherType;
    resolveDest(xtra_params, destMac, etherType);

    size_t offset = 0;
    while (offset < buffer.size()) {
        size_t frameLen = std::min(VECTOR_ETH_MAX_PAYLOAD, buffer.size() - offset);
        Status s = sendFrame(destMac, etherType, buffer.subspan(offset, frameLen));
        if (s != Status::SUCCESS) {
            result.status        = s;
            result.bytes_written = offset;
            return result;
        }
        offset += frameLen;
    }

    result.status        = Status::SUCCESS;
    result.bytes_written = buffer.size();

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("tout_write: sent"); LOG_SIZET(result.bytes_written);
              LOG_STRING("bytes to"); LOG_STRING(formatMac(destMac).c_str()));

    return result;
}
