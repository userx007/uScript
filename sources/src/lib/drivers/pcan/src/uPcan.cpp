#include "uPcan.hpp"
#include "uLogger.hpp"

#include <cstring>
#include <charconv>
#include <algorithm>

#if defined(_WIN32)
#  include <windows.h>   // WaitForSingleObject, WAIT_OBJECT_0, etc.
#else
#  include <poll.h>      // poll(), POLLIN
#  include <sys/eventfd.h>
#endif


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
#  undef LT_HDR
#endif
#ifdef LOG_HDR
#  undef LOG_HDR
#endif

#define LT_HDR   "PCAN_DRV    |"
#define LOG_HDR  LOG_STRING(LT_HDR)


// ============================================================================
// STATIC HELPERS
// ============================================================================

bool PCAN::parseUint32(std::string_view sv, uint32_t& out)
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


uint32_t PCAN::resolveTxId(std::string_view xtra_params) const
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


uint32_t PCAN::resolveRxId(std::string_view xtra_params) const
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


uint32_t PCAN::resolveTpRxId(std::string_view xtra_params) const
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


void PCAN::dumpFrame(CommDir dir, uint32_t u32Id, bool bExtended, std::span<const uint8_t> data) const
{
    if (!gui_mode_active()) {
        return;
    }
    char label[k_labelSize];
    std::snprintf(label, sizeof(label), "%s id=0x%X%s",
                  m_strIdentityLabel.empty() ? "PCAN" : m_strIdentityLabel.c_str(),
                  u32Id, bExtended ? " (ext)" : "");
    gui_notify_comm_dump("PCAN", commdump_details(CommFamily::CAN, label),
                          dir, data.data(), static_cast<uint32_t>(data.size()));
}


bool PCAN::frameMatchesFilter(const TPCANMsg& msg, uint32_t u32RxFilterId) const
{
    if (u32RxFilterId == 0) {
        return true;   // accept-all
    }

    // Normalise the SocketCAN canid_t convention: bit 31 = CAN_EFF_FLAG,
    // rest is either an 11-bit or 29-bit numeric id. PCANBasic's msg.ID
    // never carries that flag bit — extended-ness lives in MSGTYPE — so
    // both the flag and the numeric id must be handled separately here.
    const bool     bWantExtended = (u32RxFilterId & CAN_EFF_FLAG) != 0U ||
                                    ((u32RxFilterId & CAN_EFF_MASK) > CAN_SFF_MASK);
    const uint32_t u32WantId     = u32RxFilterId & (bWantExtended ? CAN_EFF_MASK : CAN_SFF_MASK);
    const bool     bFrameExtended = (msg.MSGTYPE & PCAN_MESSAGE_EXTENDED) != 0U;

    return (bFrameExtended == bWantExtended) && (msg.ID == u32WantId);
}


ICommDriver::Status PCAN::mapPcanError(TPCANStatus sts)
{
    if (sts == PCAN_ERROR_OK)          return Status::SUCCESS;
    if (sts == PCAN_ERROR_QRCVEMPTY)   return Status::READ_TIMEOUT;  // RX queue empty
    if (sts == PCAN_ERROR_INITIALIZE)  return Status::PORT_ACCESS;
    if (sts == PCAN_ERROR_ILLOPERATION) return Status::INVALID_PARAM;
    return Status::READ_ERROR;
}


TPCANBaudrate PCAN::mapBitrate(uint32_t u32Bitrate)
{
    switch (u32Bitrate) {
        case 1000000: return PCAN_BAUD_1M;
        case  800000: return PCAN_BAUD_800K;
        case  500000: return PCAN_BAUD_500K;
        case  250000: return PCAN_BAUD_250K;
        case  125000: return PCAN_BAUD_125K;
        case  100000: return PCAN_BAUD_100K;
        case   95000: return PCAN_BAUD_95K;
        case   83000: return PCAN_BAUD_83K;
        case   50000: return PCAN_BAUD_50K;
        case   47000: return PCAN_BAUD_47K;
        case   33000: return PCAN_BAUD_33K;
        case   20000: return PCAN_BAUD_20K;
        case   10000: return PCAN_BAUD_10K;
        case    5000: return PCAN_BAUD_5K;
        default:
            return 0;   // caller must check
    }
}


// ============================================================================
// LIFECYCLE
// ============================================================================

ICommDriver::Status PCAN::open(const std::string& strChannel,
                               uint32_t           u32Bitrate,
                               uint32_t           u32TxId,
                               bool               bExtended,
                               bool               bFD)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (strChannel.empty() || u32Bitrate == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("open: invalid parameter(s)");
                  LOG_STRING(strChannel.c_str());
                  LOG_STRING("bitrate:"); LOG_UINT32(u32Bitrate));
        return Status::INVALID_PARAM;
    }

    // Parse channel handle
    uint32_t u32Channel = 0;
    if (!parseUint32(strChannel, u32Channel)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("open: cannot parse channel string:"); LOG_STRING(strChannel.c_str()));
        return Status::INVALID_PARAM;
    }

    TPCANHandle hChannel = static_cast<TPCANHandle>(u32Channel);

    TPCANBaudrate baudrate = mapBitrate(u32Bitrate);
    if (baudrate == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("open: unsupported bitrate"); LOG_UINT32(u32Bitrate));
        return Status::INVALID_PARAM;
    }

    TPCANStatus sts = CAN_Initialize(hChannel, baudrate, 0, 0, 0);
    if (sts != PCAN_ERROR_OK) {
        char szErr[256] = {0};
        CAN_GetErrorText(sts, 0x00, szErr);
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("CAN_Initialize failed:");
                  LOG_STRING(szErr);
                  LOG_STRING("channel:"); LOG_HEX32(hChannel);
                  LOG_STRING("bitrate:"); LOG_UINT32(u32Bitrate));
        return Status::PORT_ACCESS;
    }

    m_hChannel            = hChannel;
    m_bOpen               = true;
    m_bFD                 = bFD;
    m_bExtendedId         = bExtended;
    m_u32DefaultTxId      = u32TxId;

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("PCAN channel opened:");
              LOG_HEX32(static_cast<uint32_t>(m_hChannel));
              LOG_STRING("bitrate:"); LOG_UINT32(u32Bitrate);
              LOG_STRING("TX ID:"); LOG_HEX32(m_u32DefaultTxId));

    return Status::SUCCESS;
}


ICommDriver::Status PCAN::close()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_bOpen) {
        CAN_Uninitialize(m_hChannel);
        LOG_PRINT(LOG_DEBUG, LOG_HDR;
                  LOG_STRING("PCAN channel closed:"); LOG_HEX32(static_cast<uint32_t>(m_hChannel)));
        m_hChannel = PCAN_NONEBUS;
        m_bOpen    = false;
    }
    return Status::SUCCESS;
}


bool PCAN::is_open() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_bOpen;
}


// ============================================================================
// FRAME-LEVEL PRIMITIVES
// ============================================================================

ICommDriver::Status PCAN::recvFrame(uint32_t        u32TimeoutMs,
                                    TPCANMsg&       msg,
                                    TPCANTimestamp& ts) const
{
    // First attempt a non-blocking read from the RX queue.
    TPCANStatus sts = CAN_Read(m_hChannel, &msg, &ts);

    if (sts == PCAN_ERROR_OK) {
        return Status::SUCCESS;
    }

    if (sts != PCAN_ERROR_QRCVEMPTY) {
        // Genuine bus / hardware error.
        char szErr[256] = {0};
        CAN_GetErrorText(sts, 0x00, szErr);
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("CAN_Read error:"); LOG_STRING(szErr));
        return Status::READ_ERROR;
    }

    // Queue was empty — wait for an event then retry once.
    // ---- Windows path ----
#if defined(_WIN32)
    HANDLE hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (hEvent == nullptr) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CreateEvent failed"));
        return Status::READ_ERROR;
    }

    sts = CAN_SetValue(m_hChannel, PCAN_RECEIVE_EVENT, &hEvent, sizeof(hEvent));
    if (sts != PCAN_ERROR_OK) {
        CloseHandle(hEvent);
        return Status::READ_ERROR;
    }

    DWORD dwWait = WaitForSingleObject(hEvent, static_cast<DWORD>(u32TimeoutMs));
    CAN_SetValue(m_hChannel, PCAN_RECEIVE_EVENT, nullptr, 0);
    CloseHandle(hEvent);

    if (dwWait == WAIT_TIMEOUT) {
        return Status::READ_TIMEOUT;
    }
    if (dwWait != WAIT_OBJECT_0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("WaitForSingleObject returned:"); LOG_UINT32(dwWait));
        return Status::READ_ERROR;
    }

    // ---- Linux path ----
#else
    // On Linux PCAN_RECEIVE_EVENT yields a file descriptor we can poll on.
    int iFd = -1;
    DWORD dwFdSize = sizeof(iFd);
    sts = CAN_GetValue(m_hChannel, PCAN_RECEIVE_EVENT,
                       reinterpret_cast<void*>(&iFd), dwFdSize);
    if (sts != PCAN_ERROR_OK || iFd < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("PCAN_RECEIVE_EVENT fd unavailable"));
        return Status::READ_ERROR;
    }

    struct pollfd pfd;
    pfd.fd      = iFd;
    pfd.events  = POLLIN;
    pfd.revents = 0;

    int iPollRet = poll(&pfd, 1, static_cast<int>(u32TimeoutMs));
    if (iPollRet == 0) {
        return Status::READ_TIMEOUT;
    }
    if (iPollRet < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("poll() failed on PCAN event fd"));
        return Status::READ_ERROR;
    }
#endif

    // Event fired — read the frame.
    sts = CAN_Read(m_hChannel, &msg, &ts);
    if (sts == PCAN_ERROR_OK) {
        return Status::SUCCESS;
    }

    char szErr[256] = {0};
    CAN_GetErrorText(sts, 0x00, szErr);
    LOG_PRINT(LOG_ERROR, LOG_HDR;
              LOG_STRING("CAN_Read after event failed:"); LOG_STRING(szErr));
    return Status::READ_ERROR;
}


ICommDriver::Status PCAN::sendFrame(uint32_t                 u32Id,
                                    bool                     bExtended,
                                    std::span<const uint8_t> data) const
{
    if (data.size() > PCAN_MAX_PAYLOAD) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("sendFrame: payload too large"); LOG_SIZET(data.size()));
        return Status::INVALID_PARAM;
    }

    TPCANMsg msg;
    std::memset(&msg, 0, sizeof(msg));
    msg.ID      = u32Id;
    msg.LEN     = static_cast<BYTE>(data.size());
    msg.MSGTYPE = bExtended ? PCAN_MESSAGE_EXTENDED : PCAN_MESSAGE_STANDARD;
    std::memcpy(msg.DATA, data.data(), data.size());

    TPCANStatus sts = CAN_Write(m_hChannel, &msg);
    if (sts != PCAN_ERROR_OK) {
        char szErr[256] = {0};
        CAN_GetErrorText(sts, 0x00, szErr);
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("CAN_Write failed:"); LOG_STRING(szErr);
                  LOG_STRING("ID:"); LOG_HEX32(u32Id));
        return Status::WRITE_ERROR;
    }

    dumpFrame(CommDir::Tx, u32Id, bExtended, data);

    return Status::SUCCESS;
}


// ============================================================================
// READ-MODE IMPLEMENTATIONS
// ============================================================================

void PCAN::buildKmpTable(std::span<const uint8_t> pattern, std::vector<int>& viLps)
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


ICommDriver::Status PCAN::readExact(uint32_t           u32TimeoutMs,
                                    std::span<uint8_t> buffer,
                                    size_t&            szBytesRead,
                                    uint32_t           u32RxFilterId) const
{
    szBytesRead = 0;
    TPCANMsg       msg;
    TPCANTimestamp ts;

    while (szBytesRead < buffer.size()) {
        Status s = recvFrame(u32TimeoutMs, msg, ts);
        if (s != Status::SUCCESS) return s;

        // Optional single-ID filter (0 = accept all).
        if (!frameMatchesFilter(msg, u32RxFilterId)) continue;

        // Skip error frames.
        if (msg.MSGTYPE & PCAN_MESSAGE_STATUS) continue;

        dumpFrame(CommDir::Rx, msg.ID, (msg.MSGTYPE & PCAN_MESSAGE_EXTENDED) != 0U,
                  std::span<const uint8_t>(msg.DATA, msg.LEN));

        size_t toCopy = std::min<size_t>(msg.LEN, buffer.size() - szBytesRead);
        std::memcpy(buffer.data() + szBytesRead, msg.DATA, toCopy);
        szBytesRead += toCopy;
    }

    return Status::SUCCESS;
}


ICommDriver::Status PCAN::readUntilDelimiter(uint32_t           u32TimeoutMs,
                                             std::span<uint8_t> buffer,
                                             uint8_t            cDelimiter,
                                             size_t&            szBytesRead,
                                             uint32_t           u32RxFilterId) const
{
    if (buffer.size() < 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("readUntilDelimiter: buffer too small"));
        return Status::INVALID_PARAM;
    }

    szBytesRead = 0;
    TPCANMsg       msg;
    TPCANTimestamp ts;

    while (true) {
        Status s = recvFrame(u32TimeoutMs, msg, ts);
        if (s != Status::SUCCESS) return s;

        if (!frameMatchesFilter(msg, u32RxFilterId)) continue;
        if (msg.MSGTYPE & PCAN_MESSAGE_STATUS) continue;

        dumpFrame(CommDir::Rx, msg.ID, (msg.MSGTYPE & PCAN_MESSAGE_EXTENDED) != 0U,
                  std::span<const uint8_t>(msg.DATA, msg.LEN));

        for (size_t i = 0; i < msg.LEN; ++i) {
            uint8_t ch = msg.DATA[i];
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


ICommDriver::Status PCAN::readUntilToken(uint32_t                 u32TimeoutMs,
                                         std::span<const uint8_t> token,
                                         uint32_t                 u32RxFilterId) const
{
    if (token.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("readUntilToken: empty token"));
        return Status::INVALID_PARAM;
    }

    std::vector<int> viLps;
    buildKmpTable(token, viLps);

    TPCANMsg       msg;
    TPCANTimestamp ts;
    size_t         szMatched = 0;

    while (true) {
        Status s = recvFrame(u32TimeoutMs, msg, ts);
        if (s != Status::SUCCESS) return s;

        if (!frameMatchesFilter(msg, u32RxFilterId)) continue;
        if (msg.MSGTYPE & PCAN_MESSAGE_STATUS) continue;

        dumpFrame(CommDir::Rx, msg.ID, (msg.MSGTYPE & PCAN_MESSAGE_EXTENDED) != 0U,
                  std::span<const uint8_t>(msg.DATA, msg.LEN));

        for (size_t i = 0; i < msg.LEN; ++i) {
            uint8_t ch = msg.DATA[i];

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

ICommDriver::ReadResult PCAN::readOneFrame_locked(uint32_t           u32TimeoutMs,
                                                   std::span<uint8_t> buffer,
                                                   std::string_view   xtra_params) const
{
    // ASSUMES m_mutex IS ALREADY HELD (see class comment / RawIo).
    ReadResult result;
    const uint32_t rxFilterId = resolveRxId(xtra_params);
    TPCANMsg       msg;
    TPCANTimestamp ts;

    while (true) {
        Status s = recvFrame(u32TimeoutMs, msg, ts);
        if (s != Status::SUCCESS) {
            result.status = s;
            return result;
        }
        if (!frameMatchesFilter(msg, rxFilterId)) continue;
        if (msg.MSGTYPE & PCAN_MESSAGE_STATUS) continue;
        break;
    }

    dumpFrame(CommDir::Rx, msg.ID, (msg.MSGTYPE & PCAN_MESSAGE_EXTENDED) != 0U,
              std::span<const uint8_t>(msg.DATA, msg.LEN));

    const size_t toCopy = std::min<size_t>(msg.LEN, buffer.size());
    if (toCopy > 0) {
        std::memcpy(buffer.data(), msg.DATA, toCopy);
    }
    if (toCopy < msg.LEN) {
        LOG_PRINT(LOG_WARNING, LOG_HDR;
                  LOG_STRING("readOneFrame_locked: frame truncated, buffer too small:");
                  LOG_SIZET(buffer.size()); LOG_STRING("<"); LOG_UINT32(static_cast<uint32_t>(msg.LEN)));
    }

    result.status     = Status::SUCCESS;
    result.bytes_read = toCopy;
    return result;
}

ICommDriver::ReadResult PCAN::readDispatch_locked(uint32_t           u32ReadTimeout,
                                                  std::span<uint8_t> buffer,
                                                  const ReadOptions& options,
                                                  std::string_view   xtra_params) const
{
    // ASSUMES m_mutex IS ALREADY HELD (see class comment / RawIo).
    ReadResult result;

    const uint32_t timeout    = (u32ReadTimeout == 0) ? PCAN_READ_DEFAULT_TIMEOUT : u32ReadTimeout;
    const uint32_t rxFilterId = resolveRxId(xtra_params);

    switch (options.mode) {

        case ReadMode::Exact: {
            size_t bytesRead = 0;
            result.status         = readExact(timeout, buffer, bytesRead, rxFilterId);
            result.bytes_read     = bytesRead;
            result.found_terminator = false;
            break;
        }

        case ReadMode::UntilDelimiter: {
            size_t bytesRead = 0;
            result.status         = readUntilDelimiter(timeout, buffer,
                                                       options.delimiter,
                                                       bytesRead, rxFilterId);
            result.bytes_read       = bytesRead;
            result.found_terminator = (result.status == Status::SUCCESS);
            break;
        }

        case ReadMode::UntilToken: {
            result.status           = readUntilToken(timeout, options.token, rxFilterId);
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


ICommDriver::ReadResult PCAN::tout_read(uint32_t           u32ReadTimeout,
                                        std::span<uint8_t> buffer,
                                        const ReadOptions& options,
                                        std::string_view   xtra_params) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_bOpen) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("tout_read: channel not open"));
        ReadResult result;
        result.status = Status::PORT_ACCESS;
        return result;
    }

    // Delimiter/token reads are an ASCII-stream concept that a segmented
    // binary transport has no notion of, so they always take the legacy
    // path regardless of m_eTpProtocol — same rule KVCAN/SLCAN apply.
    if (m_eTpProtocol == TpProtocol::NONE || options.mode != ReadMode::Exact) {
        return readDispatch_locked(u32ReadTimeout, buffer, options, xtra_params);
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

    // m_rawIo routes every physical frame this receive() performs back
    // through readOneFrame_locked()/writeFragmented_locked() (for the FC/CTS
    // frames we send back) — each one already calls dumpFrame() itself, so
    // no extra dumping is needed here.
    return upTp->receive(m_rawIo, u32ReadTimeout, buffer, szRxId, szTxId);
}


ICommDriver::WriteResult PCAN::writeFragmented_locked(uint32_t                 u32WriteTimeout,
                                                       std::span<const uint8_t> buffer,
                                                       std::string_view         xtra_params) const
{
    // ASSUMES m_mutex IS ALREADY HELD (see class comment / RawIo).
    WriteResult result;

    (void)u32WriteTimeout;  // CAN_Write is non-blocking; timeout reserved for future use.

    const uint32_t u32TxId    = resolveTxId(xtra_params);
    const bool     bExtended  = m_bExtendedId || (u32TxId & CAN_EFF_FLAG) != 0U ||
                                 ((u32TxId & CAN_EFF_MASK) > CAN_SFF_MASK);
    // sendFrame()/TPCANMsg::ID hold only the raw 11/29-bit value — PCANBasic
    // has no equivalent of the CAN_EFF_FLAG bit (extended-ness is carried
    // separately via MSGTYPE) — so it must be stripped here, not forwarded.
    const uint32_t u32RawTxId = u32TxId & (bExtended ? CAN_EFF_MASK : CAN_SFF_MASK);
    const size_t   maxPayload = m_bFD ? PCAN_FD_MAX_PAYLOAD : PCAN_MAX_PAYLOAD;

    size_t offset = 0;
    while (offset < buffer.size()) {
        size_t frameLen = std::min(maxPayload, buffer.size() - offset);
        Status s = sendFrame(u32RawTxId, bExtended,
                             buffer.subspan(offset, frameLen));
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


ICommDriver::WriteResult PCAN::tout_write(uint32_t                 u32WriteTimeout,
                                          std::span<const uint8_t> buffer,
                                          std::string_view         xtra_params) const
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

    // m_rawIo routes every physical frame this send() performs back through
    // writeFragmented_locked()/readOneFrame_locked() (for the Flow Control
    // frame we wait for) — each one already calls dumpFrame() itself, so no
    // extra dumping is needed here.
    return upTp->send(m_rawIo, u32WriteTimeout, buffer, szTxId, szRxId);
}
