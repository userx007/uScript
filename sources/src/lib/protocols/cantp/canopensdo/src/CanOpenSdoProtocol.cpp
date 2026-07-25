#include "CanOpenSdoProtocol.hpp"

#include <algorithm>
#include <cstring>

namespace
{
    // ---- Client Command Specifiers (byte0 bits 7-5), sent BY US as client ----
    constexpr uint8_t kCcsDlSegment  = 0; // Download segment
    constexpr uint8_t kCcsDlInitiate = 1; // Initiate download (expedited or normal)
    constexpr uint8_t kCcsUlInitiate = 2; // Initiate upload
    constexpr uint8_t kCcsUlSegment  = 3; // Upload segment request
    constexpr uint8_t kCcsBlockUl    = 5; // Block upload sub-commands
    constexpr uint8_t kCcsBlockDl    = 6; // Block download sub-commands

    // ---- Server Command Specifiers (byte0 bits 7-5), sent BY THE PEER ----
    constexpr uint8_t kScsUlSegment  = 0; // Upload segment response (carries data)
    constexpr uint8_t kScsDlSegment  = 1; // Download segment response (ack)
    constexpr uint8_t kScsUlInitiate = 2; // Initiate upload response
    constexpr uint8_t kScsDlInitiate = 3; // Initiate download response (ack)
    constexpr uint8_t kScsBlock      = 5; // Block download sub-commands (ack/end)
    constexpr uint8_t kScsBlockUl    = 6; // Block upload sub-commands (initiate/end)

    constexpr uint8_t kSdoAbort = 0x80;

    // Abort codes (CiA 301 Table 23, partial set used here).
    constexpr uint32_t kAbortLengthMismatch  = 0x06070010u; // data length does not match
    constexpr uint32_t kAbortGeneralError    = 0x08000000u;

    inline uint8_t cmdByte0(uint8_t specifier3, uint8_t rest5) { return static_cast<uint8_t>((specifier3 << 5) | (rest5 & 0x1F)); }
}


void CanOpenSdoProtocol::packIndex(Frame& f) const
{
    f[1] = static_cast<uint8_t>(m_cfg.canOpenIndex & 0xFF);
    f[2] = static_cast<uint8_t>((m_cfg.canOpenIndex >> 8) & 0xFF);
    f[3] = m_cfg.canOpenSubIndex;
}


void CanOpenSdoProtocol::sendAbort(const ICommDriver& driver, uint32_t timeout, std::string_view txId,
                                   const Frame& ctx, uint32_t abortCode)
{
    Frame abort{};
    abort[0] = kSdoAbort;
    abort[1] = ctx[1];
    abort[2] = ctx[2];
    abort[3] = ctx[3];
    abort[4] = static_cast<uint8_t>(abortCode & 0xFF);
    abort[5] = static_cast<uint8_t>((abortCode >> 8) & 0xFF);
    abort[6] = static_cast<uint8_t>((abortCode >> 16) & 0xFF);
    abort[7] = static_cast<uint8_t>((abortCode >> 24) & 0xFF);
    driver.tout_write(timeout, std::span<const uint8_t>(abort.data(), abort.size()), txId);
}


// ============================================================================
// SEND (download) — dispatch
// ============================================================================

ICommDriver::WriteResult CanOpenSdoProtocol::send(
    const ICommDriver& driver,
    uint32_t u32WriteTimeout,
    std::span<const uint8_t> data,
    std::string_view txId,
    std::string_view rxId) const
{
    ICommDriver::WriteResult result;

    if (data.empty() || data.size() > m_cfg.canOpenMaxMessageLen)
    {
        result.status = ICommDriver::Status::INVALID_PARAM;
        return result;
    }

    if (data.size() <= 4)
    {
        return sendExpedited(driver, u32WriteTimeout, data, txId, rxId);
    }

    return m_cfg.canOpenUseBlock ? sendBlock(driver, u32WriteTimeout, data, txId, rxId)
                                  : sendSegmented(driver, u32WriteTimeout, data, txId, rxId);
}


ICommDriver::WriteResult CanOpenSdoProtocol::sendExpedited(
    const ICommDriver& driver, uint32_t timeout, std::span<const uint8_t> data,
    std::string_view txId, std::string_view rxId) const
{
    ICommDriver::WriteResult result;
    ICommDriver::ReadOptions opts;
    opts.mode = ICommDriver::ReadMode::Exact;

    const uint8_t n = static_cast<uint8_t>(4 - data.size());
    Frame req{};
    req[0] = static_cast<uint8_t>((kCcsDlInitiate << 5) | (n << 2) | (1 << 1) | 1); // e=1,s=1
    packIndex(req);
    std::copy(data.begin(), data.end(), req.begin() + 4);

    auto wr = driver.tout_write(timeout, std::span<const uint8_t>(req.data(), req.size()), txId);
    if (wr.status != ICommDriver::Status::SUCCESS) { result.status = wr.status; return result; }

    Frame resp{};
    auto rr = driver.tout_read(timeout, std::span<uint8_t>(resp.data(), resp.size()), opts, rxId);
    if (rr.status != ICommDriver::Status::SUCCESS) { result.status = rr.status; return result; }

    if (resp[0] == kSdoAbort) { result.status = ICommDriver::Status::NACK; return result; }
    if ((resp[0] >> 5) != kScsDlInitiate) { result.status = ICommDriver::Status::PROTOCOL_ERROR; return result; }

    result.status = ICommDriver::Status::SUCCESS;
    result.bytes_written = data.size();
    return result;
}


ICommDriver::WriteResult CanOpenSdoProtocol::sendSegmented(
    const ICommDriver& driver, uint32_t timeout, std::span<const uint8_t> data,
    std::string_view txId, std::string_view rxId) const
{
    ICommDriver::WriteResult result;
    ICommDriver::ReadOptions opts;
    opts.mode = ICommDriver::ReadMode::Exact;

    // ---- Initiate normal (segmented) download, size indicated ----
    Frame req{};
    req[0] = static_cast<uint8_t>((kCcsDlInitiate << 5) | 1); // e=0,s=1
    packIndex(req);
    const uint32_t sz = static_cast<uint32_t>(data.size());
    req[4] = static_cast<uint8_t>(sz & 0xFF);
    req[5] = static_cast<uint8_t>((sz >> 8) & 0xFF);
    req[6] = static_cast<uint8_t>((sz >> 16) & 0xFF);
    req[7] = static_cast<uint8_t>((sz >> 24) & 0xFF);

    auto wr = driver.tout_write(timeout, std::span<const uint8_t>(req.data(), req.size()), txId);
    if (wr.status != ICommDriver::Status::SUCCESS) { result.status = wr.status; return result; }

    Frame resp{};
    auto rr = driver.tout_read(timeout, std::span<uint8_t>(resp.data(), resp.size()), opts, rxId);
    if (rr.status != ICommDriver::Status::SUCCESS) { result.status = rr.status; return result; }
    if (resp[0] == kSdoAbort) { result.status = ICommDriver::Status::NACK; return result; }
    if ((resp[0] >> 5) != kScsDlInitiate) { result.status = ICommDriver::Status::PROTOCOL_ERROR; return result; }

    // ---- Download segments, alternating toggle bit ----
    size_t sent = 0;
    uint8_t toggle = 0;

    while (sent < data.size())
    {
        const size_t chunk = std::min<size_t>(7, data.size() - sent);
        const bool   last  = (sent + chunk == data.size());
        const uint8_t n    = static_cast<uint8_t>(7 - chunk);

        Frame seg{};
        seg[0] = static_cast<uint8_t>((kCcsDlSegment << 5) | (toggle << 4) | (n << 1) | (last ? 1 : 0));
        std::copy(data.begin() + static_cast<long>(sent), data.begin() + static_cast<long>(sent + chunk), seg.begin() + 1);
        std::fill(seg.begin() + 1 + static_cast<long>(chunk), seg.end(), 0);

        auto wrSeg = driver.tout_write(timeout, std::span<const uint8_t>(seg.data(), seg.size()), txId);
        if (wrSeg.status != ICommDriver::Status::SUCCESS) { result.status = wrSeg.status; return result; }

        Frame segResp{};
        auto rrSeg = driver.tout_read(timeout, std::span<uint8_t>(segResp.data(), segResp.size()), opts, rxId);
        if (rrSeg.status != ICommDriver::Status::SUCCESS) { result.status = rrSeg.status; return result; }
        if (segResp[0] == kSdoAbort) { result.status = ICommDriver::Status::NACK; return result; }
        if ((segResp[0] >> 5) != kScsDlSegment) { result.status = ICommDriver::Status::PROTOCOL_ERROR; return result; }
        if (((segResp[0] >> 4) & 1) != toggle) { result.status = ICommDriver::Status::PROTOCOL_ERROR; return result; }

        sent += chunk;
        toggle ^= 1;
    }

    result.status = ICommDriver::Status::SUCCESS;
    result.bytes_written = data.size();
    return result;
}


ICommDriver::WriteResult CanOpenSdoProtocol::sendBlock(
    const ICommDriver& driver, uint32_t timeout, std::span<const uint8_t> data,
    std::string_view txId, std::string_view rxId) const
{
    ICommDriver::WriteResult result;
    ICommDriver::ReadOptions opts;
    opts.mode = ICommDriver::ReadMode::Exact;

    // ---- Initiate Block Download: cc=0 (no CRC), s=1 (size indicated), cs=0 ----
    Frame req{};
    req[0] = static_cast<uint8_t>((kCcsBlockDl << 5) | (0 << 2) | (1 << 1) | 0);
    packIndex(req);
    const uint32_t sz = static_cast<uint32_t>(data.size());
    req[4] = static_cast<uint8_t>(sz & 0xFF);
    req[5] = static_cast<uint8_t>((sz >> 8) & 0xFF);
    req[6] = static_cast<uint8_t>((sz >> 16) & 0xFF);
    req[7] = static_cast<uint8_t>((sz >> 24) & 0xFF);

    auto wr = driver.tout_write(timeout, std::span<const uint8_t>(req.data(), req.size()), txId);
    if (wr.status != ICommDriver::Status::SUCCESS) { result.status = wr.status; return result; }

    Frame resp{};
    auto rr = driver.tout_read(timeout, std::span<uint8_t>(resp.data(), resp.size()), opts, rxId);
    if (rr.status != ICommDriver::Status::SUCCESS) { result.status = rr.status; return result; }
    if (resp[0] == kSdoAbort) { result.status = ICommDriver::Status::NACK; return result; }
    // Only the top 3 bits (scs) are checked: the low bits aren't a reliable
    // sub-command discriminator on this specific response.
    if ((resp[0] >> 5) != kScsBlock) { result.status = ICommDriver::Status::PROTOCOL_ERROR; return result; }

    uint8_t blksize = resp[4];
    if (blksize == 0) { blksize = m_cfg.canOpenBlockSize; }
    if (blksize == 0) { blksize = 1; }

    // ---- Stream segments (raw, no command byte tagging) ----
    // Per CiA 301, the segment sequence number resets to 1 at the start of
    // EVERY block (it is not a single counter running for the whole
    // transfer) — the peer's per-block ack (ackseq) is scoped the same way.
    size_t  sent  = 0;
    uint8_t lastN = 0;

    while (sent < data.size())
    {
        uint8_t seq            = 0;
        uint8_t framesInBlock  = 0;

        while (sent < data.size() && framesInBlock < blksize)
        {
            ++seq;
            const size_t chunk    = std::min<size_t>(7, data.size() - sent);
            const bool   lastOfAll = (sent + chunk == data.size());
            lastN = static_cast<uint8_t>(7 - chunk);

            Frame seg{};
            seg[0] = static_cast<uint8_t>((lastOfAll ? 0x80 : 0x00) | (seq & 0x7F));
            std::copy(data.begin() + static_cast<long>(sent), data.begin() + static_cast<long>(sent + chunk), seg.begin() + 1);
            std::fill(seg.begin() + 1 + static_cast<long>(chunk), seg.end(), 0);

            auto wrSeg = driver.tout_write(timeout, std::span<const uint8_t>(seg.data(), seg.size()), txId);
            if (wrSeg.status != ICommDriver::Status::SUCCESS) { result.status = wrSeg.status; return result; }

            sent += chunk;
            ++framesInBlock;
        }

        // ---- Wait for the per-block acknowledgement ----
        Frame ackResp{};
        auto rrAck = driver.tout_read(timeout, std::span<uint8_t>(ackResp.data(), ackResp.size()), opts, rxId);
        if (rrAck.status != ICommDriver::Status::SUCCESS) { result.status = rrAck.status; return result; }
        if (ackResp[0] == kSdoAbort) { result.status = ICommDriver::Status::NACK; return result; }
        if ((ackResp[0] >> 5) != kScsBlock || (ackResp[0] & 0x03) != 2) { result.status = ICommDriver::Status::PROTOCOL_ERROR; return result; }

        const uint8_t ackseq = ackResp[1];
        if (ackseq != seq)
        {
            // Server is missing (part of) the block — no retransmission
            // support in this implementation (documented simplification).
            result.status = ICommDriver::Status::PROTOCOL_ERROR;
            return result;
        }

        blksize = ackResp[2];
        if (blksize == 0 && sent < data.size())
        {
            // Spec allows blksize=0 to mean "retransmit from ackseq+1";
            // not supported here.
            result.status = ICommDriver::Status::PROTOCOL_ERROR;
            return result;
        }
    }

    // ---- End Block Download ----
    Frame endReq{};
    endReq[0] = static_cast<uint8_t>((kCcsBlockDl << 5) | (lastN << 2) | 1);
    // bytes1-2 would carry the CRC if cc had been negotiated; left 0.

    auto wrEnd = driver.tout_write(timeout, std::span<const uint8_t>(endReq.data(), endReq.size()), txId);
    if (wrEnd.status != ICommDriver::Status::SUCCESS) { result.status = wrEnd.status; return result; }

    Frame endResp{};
    auto rrEnd = driver.tout_read(timeout, std::span<uint8_t>(endResp.data(), endResp.size()), opts, rxId);
    if (rrEnd.status != ICommDriver::Status::SUCCESS) { result.status = rrEnd.status; return result; }
    if (endResp[0] == kSdoAbort) { result.status = ICommDriver::Status::NACK; return result; }
    if ((endResp[0] >> 5) != kScsBlock || (endResp[0] & 0x03) != 1) { result.status = ICommDriver::Status::PROTOCOL_ERROR; return result; }

    result.status = ICommDriver::Status::SUCCESS;
    result.bytes_written = data.size();
    return result;
}


// ============================================================================
// RECEIVE (upload) — dispatch
// ============================================================================

ICommDriver::ReadResult CanOpenSdoProtocol::receive(
    const ICommDriver& driver,
    uint32_t u32ReadTimeout,
    std::span<uint8_t> buffer,
    std::string_view rxId,
    std::string_view txId) const
{
    return m_cfg.canOpenUseBlock ? receiveBlock(driver, u32ReadTimeout, buffer, rxId, txId)
                                  : receiveNormal(driver, u32ReadTimeout, buffer, rxId, txId);
}


ICommDriver::ReadResult CanOpenSdoProtocol::receiveNormal(
    const ICommDriver& driver, uint32_t timeout, std::span<uint8_t> buffer,
    std::string_view rxId, std::string_view txId) const
{
    ICommDriver::ReadResult result;
    ICommDriver::ReadOptions opts;
    opts.mode = ICommDriver::ReadMode::Exact;

    Frame req{};
    req[0] = static_cast<uint8_t>(kCcsUlInitiate << 5);
    packIndex(req);

    auto wr = driver.tout_write(timeout, std::span<const uint8_t>(req.data(), req.size()), txId);
    if (wr.status != ICommDriver::Status::SUCCESS) { result.status = wr.status; return result; }

    Frame resp{};
    auto rr = driver.tout_read(timeout, std::span<uint8_t>(resp.data(), resp.size()), opts, rxId);
    if (rr.status != ICommDriver::Status::SUCCESS) { result.status = rr.status; return result; }
    if (resp[0] == kSdoAbort) { result.status = ICommDriver::Status::NACK; return result; }
    if ((resp[0] >> 5) != kScsUlInitiate) { result.status = ICommDriver::Status::PROTOCOL_ERROR; return result; }

    return finishUploadFromInitiateResponse(resp, driver, timeout, buffer, rxId, txId);
}


ICommDriver::ReadResult CanOpenSdoProtocol::finishUploadFromInitiateResponse(
    const Frame& resp, const ICommDriver& driver, uint32_t timeout, std::span<uint8_t> buffer,
    std::string_view rxId, std::string_view txId) const
{
    ICommDriver::ReadResult result;
    ICommDriver::ReadOptions opts;
    opts.mode = ICommDriver::ReadMode::Exact;

    const bool e = ((resp[0] >> 1) & 1) != 0;
    const bool s = (resp[0] & 1) != 0;

    if (e)
    {
        const uint8_t n   = s ? static_cast<uint8_t>((resp[0] >> 2) & 0x03) : 0;
        const size_t  len = s ? static_cast<size_t>(4 - n) : 4;
        if (len > buffer.size()) { result.status = ICommDriver::Status::BUFFER_OVERFLOW; return result; }
        std::copy(resp.begin() + 4, resp.begin() + 4 + static_cast<long>(len), buffer.begin());
        result.status = ICommDriver::Status::SUCCESS;
        result.bytes_read = len;
        return result;
    }

    if (!s)
    {
        // Normal transfer without a size hint — this implementation
        // requires size-indicated servers (documented simplification).
        result.status = ICommDriver::Status::PROTOCOL_ERROR;
        return result;
    }

    const size_t totalLen = static_cast<size_t>(resp[4]) | (static_cast<size_t>(resp[5]) << 8) |
                             (static_cast<size_t>(resp[6]) << 16) | (static_cast<size_t>(resp[7]) << 24);

    if (totalLen == 0) { result.status = ICommDriver::Status::PROTOCOL_ERROR; return result; }

    if (totalLen > buffer.size())
    {
        sendAbort(driver, timeout, txId, resp, kAbortLengthMismatch);
        result.status = ICommDriver::Status::BUFFER_OVERFLOW;
        return result;
    }

    // ---- Upload segments, alternating toggle bit ----
    size_t  received = 0;
    uint8_t toggle   = 0;

    while (received < totalLen)
    {
        Frame segReq{};
        segReq[0] = static_cast<uint8_t>((kCcsUlSegment << 5) | (toggle << 4));

        auto wrSeg = driver.tout_write(timeout, std::span<const uint8_t>(segReq.data(), segReq.size()), txId);
        if (wrSeg.status != ICommDriver::Status::SUCCESS) { result.status = wrSeg.status; return result; }

        Frame segResp{};
        auto rrSeg = driver.tout_read(timeout, std::span<uint8_t>(segResp.data(), segResp.size()), opts, rxId);
        if (rrSeg.status != ICommDriver::Status::SUCCESS) { result.status = rrSeg.status; return result; }
        if (segResp[0] == kSdoAbort) { result.status = ICommDriver::Status::NACK; return result; }
        if ((segResp[0] >> 5) != kScsUlSegment) { result.status = ICommDriver::Status::PROTOCOL_ERROR; return result; }
        if (((segResp[0] >> 4) & 1) != toggle) { result.status = ICommDriver::Status::PROTOCOL_ERROR; return result; }

        const uint8_t n     = static_cast<uint8_t>((segResp[0] >> 1) & 0x07);
        const size_t  chunk = std::min<size_t>({static_cast<size_t>(7 - n), totalLen - received, 7});
        std::copy(segResp.begin() + 1, segResp.begin() + 1 + static_cast<long>(chunk), buffer.begin() + static_cast<long>(received));

        received += chunk;
        toggle   ^= 1;

        const bool c = (segResp[0] & 1) != 0; // server-signalled "last segment"
        if (c && received < totalLen)
        {
            // Server thinks it's done but the announced size says otherwise.
            result.status = ICommDriver::Status::PROTOCOL_ERROR;
            return result;
        }
    }

    result.status = ICommDriver::Status::SUCCESS;
    result.bytes_read = received;
    return result;
}


ICommDriver::ReadResult CanOpenSdoProtocol::receiveBlock(
    const ICommDriver& driver, uint32_t timeout, std::span<uint8_t> buffer,
    std::string_view rxId, std::string_view txId) const
{
    ICommDriver::ReadResult result;
    ICommDriver::ReadOptions opts;
    opts.mode = ICommDriver::ReadMode::Exact;

    // ---- Initiate Block Upload: cc=0 (no CRC), cs=0 ----
    Frame req{};
    req[0] = static_cast<uint8_t>((kCcsBlockUl << 5) | (0 << 2) | 0);
    packIndex(req);
    req[4] = m_cfg.canOpenBlockSize == 0 ? 127 : m_cfg.canOpenBlockSize; // requested block size
    req[5] = 0; // pst (protocol switch threshold) disabled

    auto wr = driver.tout_write(timeout, std::span<const uint8_t>(req.data(), req.size()), txId);
    if (wr.status != ICommDriver::Status::SUCCESS) { result.status = wr.status; return result; }

    Frame resp{};
    auto rr = driver.tout_read(timeout, std::span<uint8_t>(resp.data(), resp.size()), opts, rxId);
    if (rr.status != ICommDriver::Status::SUCCESS) { result.status = rr.status; return result; }
    if (resp[0] == kSdoAbort) { result.status = ICommDriver::Status::NACK; return result; }

    if ((resp[0] >> 5) == kScsUlInitiate)
    {
        // Server declined block transfer and answered as a plain Initiate
        // Upload instead — spec-legal graceful fallback.
        return finishUploadFromInitiateResponse(resp, driver, timeout, buffer, rxId, txId);
    }

    // Only the top 3 bits (scs) are checked here — bit1 doubles as the
    // size-indicated flag on this specific response, so it can legitimately
    // be set, unlike the strict sub-command check used on End Block Upload below.
    if ((resp[0] >> 5) != kScsBlockUl) { result.status = ICommDriver::Status::PROTOCOL_ERROR; return result; }

    const bool sInd = ((resp[0] >> 1) & 1) != 0;
    if (!sInd) { result.status = ICommDriver::Status::PROTOCOL_ERROR; return result; } // size required, see class comment

    const size_t totalLen = static_cast<size_t>(resp[4]) | (static_cast<size_t>(resp[5]) << 8) |
                             (static_cast<size_t>(resp[6]) << 16) | (static_cast<size_t>(resp[7]) << 24);

    if (totalLen == 0) { result.status = ICommDriver::Status::PROTOCOL_ERROR; return result; }

    if (totalLen > buffer.size())
    {
        sendAbort(driver, timeout, txId, resp, kAbortLengthMismatch);
        result.status = ICommDriver::Status::BUFFER_OVERFLOW;
        return result;
    }

    const uint8_t blksizeRequested = req[4];

    // ---- Tell the server to start streaming segments ----
    Frame startReq{};
    startReq[0] = static_cast<uint8_t>((kCcsBlockUl << 5) | 3); // cs=3: start upload
    auto wrStart = driver.tout_write(timeout, std::span<const uint8_t>(startReq.data(), startReq.size()), txId);
    if (wrStart.status != ICommDriver::Status::SUCCESS) { result.status = wrStart.status; return result; }

    // ---- Receive segments (raw, no command byte tagging) ----
    size_t  received     = 0;
    uint8_t expectedSeq  = 1;

    while (received < totalLen)
    {
        Frame seg{};
        auto rrSeg = driver.tout_read(timeout, std::span<uint8_t>(seg.data(), seg.size()), opts, rxId);
        if (rrSeg.status != ICommDriver::Status::SUCCESS) { result.status = rrSeg.status; return result; }

        const uint8_t gotSeq = static_cast<uint8_t>(seg[0] & 0x7F);
        if (gotSeq != expectedSeq)
        {
            // No retransmission support (documented simplification).
            result.status = ICommDriver::Status::PROTOCOL_ERROR;
            return result;
        }

        const size_t chunk = std::min<size_t>(7, totalLen - received);
        std::copy(seg.begin() + 1, seg.begin() + 1 + static_cast<long>(chunk), buffer.begin() + static_cast<long>(received));
        received += chunk;

        if (received >= totalLen) { break; }

        if (expectedSeq >= blksizeRequested)
        {
            // Block complete — acknowledge and request the next one.
            Frame ackReq{};
            ackReq[0] = static_cast<uint8_t>((kCcsBlockUl << 5) | 2); // cs=2: block ack
            ackReq[1] = expectedSeq;
            ackReq[2] = blksizeRequested;
            auto wrAck = driver.tout_write(timeout, std::span<const uint8_t>(ackReq.data(), ackReq.size()), txId);
            if (wrAck.status != ICommDriver::Status::SUCCESS) { result.status = wrAck.status; return result; }
            expectedSeq = 1;
        }
        else
        {
            ++expectedSeq;
        }
    }

    // ---- Final block acknowledgement (covers the last, possibly partial, block) ----
    Frame ackReq{};
    ackReq[0] = static_cast<uint8_t>((kCcsBlockUl << 5) | 2);
    ackReq[1] = expectedSeq;
    ackReq[2] = blksizeRequested;
    auto wrAck = driver.tout_write(timeout, std::span<const uint8_t>(ackReq.data(), ackReq.size()), txId);
    if (wrAck.status != ICommDriver::Status::SUCCESS) { result.status = wrAck.status; return result; }

    // ---- End Block Upload ----
    Frame endResp{};
    auto rrEnd = driver.tout_read(timeout, std::span<uint8_t>(endResp.data(), endResp.size()), opts, rxId);
    if (rrEnd.status != ICommDriver::Status::SUCCESS) { result.status = rrEnd.status; return result; }
    if (endResp[0] == kSdoAbort) { result.status = ICommDriver::Status::NACK; return result; }
    if ((endResp[0] >> 5) != kScsBlockUl || (endResp[0] & 0x03) != 1) { result.status = ICommDriver::Status::PROTOCOL_ERROR; return result; }

    // ---- Final client acknowledgement, ends the transfer ----
    Frame finalAck{};
    finalAck[0] = static_cast<uint8_t>((kCcsBlockUl << 5) | 1); // cs=1
    driver.tout_write(timeout, std::span<const uint8_t>(finalAck.data(), finalAck.size()), txId);

    result.status = ICommDriver::Status::SUCCESS;
    result.bytes_read = received;
    return result;
}
