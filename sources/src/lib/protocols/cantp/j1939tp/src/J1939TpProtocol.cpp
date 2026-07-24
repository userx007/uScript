#include "J1939TpProtocol.hpp"

#include <array>
#include <algorithm>
#include <thread>
#include <chrono>
#include <cstring>

namespace
{
    constexpr size_t kFrameLen  = 8;
    constexpr size_t kDtMaxLen  = 7;   // TP.DT: 1 sequence byte + up to 7 data bytes

    constexpr uint8_t kCtrlBam       = 0x20;
    constexpr uint8_t kCtrlRts       = 0x10;
    constexpr uint8_t kCtrlCts       = 0x11;
    constexpr uint8_t kCtrlEndOfMsg  = 0x13;
    constexpr uint8_t kCtrlAbort     = 0xFF;

    // BAM's minimum broadcast gap per SAE J1939-21 (50-200ms, use the
    // commonly-used 50ms floor when the caller hasn't tuned timeouts).
    constexpr uint32_t kBamInterPacketGapMs = 50;

    inline void pack_size_pgn(std::array<uint8_t, kFrameLen>& f, uint8_t ctrl,
                               size_t totalSize, uint8_t totalPackets, uint8_t byte4)
    {
        f[0] = ctrl;
        f[1] = static_cast<uint8_t>(totalSize & 0xFF);
        f[2] = static_cast<uint8_t>((totalSize >> 8) & 0xFF);
        f[3] = totalPackets;
        f[4] = byte4;
        // Bytes 5-7 (PGN of the data message) are intentionally left 0: the
        // caller distinguishes streams via txId/rxId, not the PGN payload
        // field, so this implementation does not require the data PGN to
        // decide anything. Fill with 0xFF per convention for unused bytes.
        f[5] = f[6] = f[7] = 0xFF;
    }
}


// ============================================================================
// SEND (dispatch)
// ============================================================================

ICommDriver::WriteResult J1939TpProtocol::send(
    const ICommDriver& driver,
    uint32_t u32WriteTimeout,
    std::span<const uint8_t> data,
    std::string_view txId,
    std::string_view rxId) const
{
    ICommDriver::WriteResult result;

    if (data.empty() || data.size() > m_cfg.j1939MaxMessageLen)
    {
        result.status = ICommDriver::Status::INVALID_PARAM;
        return result;
    }

    // Messages that fit in one frame never need TP.CM/TP.DT at all.
    if (data.size() <= kFrameLen)
    {
        auto wr = driver.tout_write(u32WriteTimeout, data, txId);
        result.status = wr.status;
        result.bytes_written = (wr.status == ICommDriver::Status::SUCCESS) ? data.size() : 0;
        return result;
    }

    return m_cfg.j1939UseBam ? send_bam(driver, u32WriteTimeout, data, txId)
                              : send_rts_cts(driver, u32WriteTimeout, data, txId, rxId);
}


ICommDriver::WriteResult J1939TpProtocol::send_bam(
    const ICommDriver& driver, uint32_t timeout,
    std::span<const uint8_t> data, std::string_view txId) const
{
    ICommDriver::WriteResult result;

    const uint8_t totalPackets = static_cast<uint8_t>((data.size() + kDtMaxLen - 1) / kDtMaxLen);

    std::array<uint8_t, kFrameLen> bam{};
    pack_size_pgn(bam, kCtrlBam, data.size(), totalPackets, 0xFF);

    auto wrBam = driver.tout_write(timeout, std::span<const uint8_t>(bam.data(), kFrameLen), txId);
    if (wrBam.status != ICommDriver::Status::SUCCESS)
    {
        result.status = wrBam.status;
        return result;
    }

    size_t sent = 0;
    for (uint8_t seq = 1; sent < data.size(); ++seq)
    {
        std::array<uint8_t, kFrameLen> dt{};
        dt[0] = seq;
        const size_t chunk = std::min(kDtMaxLen, data.size() - sent);
        std::copy(data.begin() + static_cast<long>(sent),
                  data.begin() + static_cast<long>(sent + chunk), dt.begin() + 1);
        std::fill(dt.begin() + 1 + static_cast<long>(chunk), dt.end(), 0xFF);

        auto wrDt = driver.tout_write(timeout, std::span<const uint8_t>(dt.data(), kFrameLen), txId);
        if (wrDt.status != ICommDriver::Status::SUCCESS)
        {
            result.status = wrDt.status;
            return result;
        }

        sent += chunk;
        if (sent < data.size())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(kBamInterPacketGapMs));
        }
    }

    result.status = ICommDriver::Status::SUCCESS;
    result.bytes_written = data.size();
    return result;
}


ICommDriver::WriteResult J1939TpProtocol::send_rts_cts(
    const ICommDriver& driver, uint32_t timeout,
    std::span<const uint8_t> data, std::string_view txId, std::string_view rxId) const
{
    ICommDriver::WriteResult result;

    const uint8_t totalPackets = static_cast<uint8_t>((data.size() + kDtMaxLen - 1) / kDtMaxLen);

    std::array<uint8_t, kFrameLen> rts{};
    pack_size_pgn(rts, kCtrlRts, data.size(), totalPackets, m_cfg.j1939MaxPackets);

    auto wrRts = driver.tout_write(timeout, std::span<const uint8_t>(rts.data(), kFrameLen), txId);
    if (wrRts.status != ICommDriver::Status::SUCCESS)
    {
        result.status = wrRts.status;
        return result;
    }

    size_t sent = 0;
    uint8_t nextSeq = 1;

    while (sent < data.size())
    {
        // ---- Wait for CTS (or Abort) from the peer. ----
        std::array<uint8_t, kFrameLen> cts{};
        ICommDriver::ReadOptions opts;
        opts.mode = ICommDriver::ReadMode::Exact;

        auto rr = driver.tout_read(m_cfg.timeoutT1_ms, std::span<uint8_t>(cts.data(), cts.size()), opts, rxId);
        if (rr.status != ICommDriver::Status::SUCCESS || rr.bytes_read < 5)
        {
            result.status = (rr.status == ICommDriver::Status::READ_TIMEOUT)
                             ? ICommDriver::Status::WRITE_TIMEOUT
                             : ICommDriver::Status::PROTOCOL_ERROR;
            return result;
        }

        if (cts[0] == kCtrlAbort)
        {
            result.status = ICommDriver::Status::NACK;
            return result;
        }
        if (cts[0] != kCtrlCts)
        {
            result.status = ICommDriver::Status::PROTOCOL_ERROR;
            return result;
        }

        const uint8_t packetsToSend = cts[1]; // 0 == "hold on", per spec
        const uint8_t startPacket   = cts[2];

        if (packetsToSend == 0)
        {
            continue; // peer asked us to wait for the next CTS
        }

        nextSeq = startPacket;
        for (uint8_t i = 0; i < packetsToSend && sent < data.size(); ++i)
        {
            std::array<uint8_t, kFrameLen> dt{};
            dt[0] = nextSeq;
            const size_t chunk = std::min(kDtMaxLen, data.size() - sent);
            std::copy(data.begin() + static_cast<long>(sent),
                      data.begin() + static_cast<long>(sent + chunk), dt.begin() + 1);
            std::fill(dt.begin() + 1 + static_cast<long>(chunk), dt.end(), 0xFF);

            auto wrDt = driver.tout_write(timeout, std::span<const uint8_t>(dt.data(), kFrameLen), txId);
            if (wrDt.status != ICommDriver::Status::SUCCESS)
            {
                result.status = wrDt.status;
                return result;
            }

            sent += chunk;
            ++nextSeq;
        }
    }

    // ---- Wait for End-Of-Message Ack. ----
    std::array<uint8_t, kFrameLen> eom{};
    ICommDriver::ReadOptions opts;
    opts.mode = ICommDriver::ReadMode::Exact;
    auto rrEom = driver.tout_read(m_cfg.timeoutT3_ms, std::span<uint8_t>(eom.data(), eom.size()), opts, rxId);
    if (rrEom.status != ICommDriver::Status::SUCCESS || eom[0] != kCtrlEndOfMsg)
    {
        // Data was fully sent; treat a missing/late EOM as a soft failure —
        // report success on the data transfer but flag it via WRITE_TIMEOUT
        // if the ack never showed up so the caller can decide whether to retry.
        result.status = (rrEom.status == ICommDriver::Status::READ_TIMEOUT)
                         ? ICommDriver::Status::WRITE_TIMEOUT
                         : ICommDriver::Status::SUCCESS;
        result.bytes_written = data.size();
        return result;
    }

    result.status = ICommDriver::Status::SUCCESS;
    result.bytes_written = data.size();
    return result;
}


// ============================================================================
// RECEIVE (dispatch)
// ============================================================================

ICommDriver::ReadResult J1939TpProtocol::receive(
    const ICommDriver& driver,
    uint32_t u32ReadTimeout,
    std::span<uint8_t> buffer,
    std::string_view rxId,
    std::string_view txId) const
{
    ICommDriver::ReadResult result;

    // Peek at the first frame: it is either a plain single-frame message,
    // a BAM, or an RTS — the control byte tells them apart.
    std::array<uint8_t, kFrameLen> frame{};
    ICommDriver::ReadOptions opts;
    opts.mode = ICommDriver::ReadMode::Exact;

    auto rr = driver.tout_read(u32ReadTimeout, std::span<uint8_t>(frame.data(), frame.size()), opts, rxId);
    if (rr.status != ICommDriver::Status::SUCCESS || rr.bytes_read == 0)
    {
        result.status = rr.status;
        return result;
    }

    if (frame[0] == kCtrlBam)
    {
        return receive_bam(driver, u32ReadTimeout, buffer, rxId, frame.data());
    }
    if (frame[0] == kCtrlRts)
    {
        return receive_rts_cts(driver, u32ReadTimeout, buffer, rxId, txId, frame.data());
    }

    // Not a TP.CM control frame: treat as a single-frame message, same as
    // the current (TpProtocol::NONE) behaviour.
    const size_t copyLen = std::min(rr.bytes_read, buffer.size());
    if (copyLen < rr.bytes_read)
    {
        result.status = ICommDriver::Status::BUFFER_OVERFLOW;
        return result;
    }
    std::memcpy(buffer.data(), frame.data(), copyLen);
    result.status = ICommDriver::Status::SUCCESS;
    result.bytes_read = copyLen;
    return result;
}


ICommDriver::ReadResult J1939TpProtocol::receive_bam(
    const ICommDriver& driver, uint32_t /*timeout*/,
    std::span<uint8_t> buffer, std::string_view rxId, const uint8_t firstFrame[8]) const
{
    ICommDriver::ReadResult result;

    const size_t totalLen = static_cast<size_t>(firstFrame[1]) | (static_cast<size_t>(firstFrame[2]) << 8);
    const uint8_t totalPackets = firstFrame[3];

    if (totalLen == 0 || totalPackets == 0)
    {
        result.status = ICommDriver::Status::PROTOCOL_ERROR;
        return result;
    }
    if (totalLen > buffer.size())
    {
        result.status = ICommDriver::Status::BUFFER_OVERFLOW;
        return result;
    }

    size_t received = 0;
    ICommDriver::ReadOptions opts;
    opts.mode = ICommDriver::ReadMode::Exact;

    for (uint8_t expectedSeq = 1; expectedSeq <= totalPackets && received < totalLen; ++expectedSeq)
    {
        std::array<uint8_t, kFrameLen> dt{};
        auto rrDt = driver.tout_read(m_cfg.timeoutTh_ms, std::span<uint8_t>(dt.data(), dt.size()), opts, rxId);
        if (rrDt.status != ICommDriver::Status::SUCCESS || rrDt.bytes_read == 0)
        {
            result.status = (rrDt.status == ICommDriver::Status::READ_TIMEOUT)
                             ? ICommDriver::Status::READ_TIMEOUT
                             : ICommDriver::Status::PROTOCOL_ERROR;
            return result;
        }
        if (dt[0] != expectedSeq)
        {
            result.status = ICommDriver::Status::PROTOCOL_ERROR;
            return result;
        }

        const size_t chunk = std::min<size_t>({kDtMaxLen, totalLen - received, rrDt.bytes_read - 1});
        std::copy(dt.begin() + 1, dt.begin() + 1 + chunk, buffer.begin() + static_cast<long>(received));
        received += chunk;
    }

    result.status = ICommDriver::Status::SUCCESS;
    result.bytes_read = received;
    return result;
}


ICommDriver::ReadResult J1939TpProtocol::receive_rts_cts(
    const ICommDriver& driver, uint32_t timeout,
    std::span<uint8_t> buffer, std::string_view rxId, std::string_view txId, const uint8_t firstFrame[8]) const
{
    ICommDriver::ReadResult result;

    const size_t totalLen = static_cast<size_t>(firstFrame[1]) | (static_cast<size_t>(firstFrame[2]) << 8);
    const uint8_t totalPackets = firstFrame[3];

    if (totalLen == 0 || totalPackets == 0)
    {
        result.status = ICommDriver::Status::PROTOCOL_ERROR;
        return result;
    }

    if (totalLen > buffer.size())
    {
        std::array<uint8_t, kFrameLen> abort{};
        abort[0] = kCtrlAbort;
        abort[1] = 0x02; // "insufficient buffer" — see SAE J1939-21 abort reasons
        std::fill(abort.begin() + 2, abort.end(), 0xFF);
        driver.tout_write(timeout, std::span<const uint8_t>(abort.data(), kFrameLen), txId);
        result.status = ICommDriver::Status::BUFFER_OVERFLOW;
        return result;
    }

    const uint8_t grantPackets = std::min<uint8_t>(totalPackets, m_cfg.j1939MaxPackets);

    size_t received = 0;
    uint8_t nextSeq = 1;
    ICommDriver::ReadOptions opts;
    opts.mode = ICommDriver::ReadMode::Exact;

    while (received < totalLen)
    {
        const uint8_t remainingPackets =
            static_cast<uint8_t>(std::min<size_t>(grantPackets, totalPackets - (nextSeq - 1)));

        std::array<uint8_t, kFrameLen> cts{};
        cts[0] = kCtrlCts;
        cts[1] = remainingPackets;
        cts[2] = nextSeq;
        cts[3] = 0xFF;
        cts[4] = 0xFF;
        cts[5] = cts[6] = cts[7] = 0xFF;

        auto wrCts = driver.tout_write(timeout, std::span<const uint8_t>(cts.data(), kFrameLen), txId);
        if (wrCts.status != ICommDriver::Status::SUCCESS)
        {
            result.status = wrCts.status;
            return result;
        }

        for (uint8_t i = 0; i < remainingPackets && received < totalLen; ++i)
        {
            std::array<uint8_t, kFrameLen> dt{};
            auto rrDt = driver.tout_read(m_cfg.timeoutT2_ms, std::span<uint8_t>(dt.data(), dt.size()), opts, rxId);
            if (rrDt.status != ICommDriver::Status::SUCCESS || rrDt.bytes_read == 0)
            {
                result.status = (rrDt.status == ICommDriver::Status::READ_TIMEOUT)
                                 ? ICommDriver::Status::READ_TIMEOUT
                                 : ICommDriver::Status::PROTOCOL_ERROR;
                return result;
            }
            if (dt[0] != nextSeq)
            {
                result.status = ICommDriver::Status::PROTOCOL_ERROR;
                return result;
            }

            const size_t chunk = std::min<size_t>({kDtMaxLen, totalLen - received, rrDt.bytes_read - 1});
            std::copy(dt.begin() + 1, dt.begin() + 1 + chunk, buffer.begin() + static_cast<long>(received));
            received += chunk;
            ++nextSeq;
        }
    }

    // ---- Send End-Of-Message Ack. ----
    std::array<uint8_t, kFrameLen> eom{};
    pack_size_pgn(eom, kCtrlEndOfMsg, totalLen, totalPackets, 0xFF);
    driver.tout_write(timeout, std::span<const uint8_t>(eom.data(), kFrameLen), txId);

    result.status = ICommDriver::Status::SUCCESS;
    result.bytes_read = received;
    return result;
}
