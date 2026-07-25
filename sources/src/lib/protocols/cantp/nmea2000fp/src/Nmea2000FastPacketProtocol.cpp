#include "Nmea2000FastPacketProtocol.hpp"

#include <array>
#include <algorithm>

namespace
{
    constexpr size_t  kFrameLen        = 8;
    constexpr size_t  kFirstFrameLen   = 6; // Frame 0: byte0=seq/idx, byte1=len, 6 data bytes
    constexpr size_t  kContFrameLen    = 7; // Frame N: byte0=seq/idx, 7 data bytes
    constexpr uint8_t kPaddingByte     = 0xFFu; // NMEA 2000 convention for unused trailing bytes
}


// ============================================================================
// SEND
// ============================================================================

ICommDriver::WriteResult Nmea2000FastPacketProtocol::send(
    const ICommDriver& driver,
    uint32_t u32WriteTimeout,
    std::span<const uint8_t> data,
    std::string_view txId,
    std::string_view /*rxId*/) const // Fast Packet is fire-and-forget: no peer handshake to address.
{
    ICommDriver::WriteResult result;

    if (data.empty() || data.size() > m_cfg.fastPacketMaxMessageLen)
    {
        result.status = ICommDriver::Status::INVALID_PARAM;
        return result;
    }

    const uint8_t seq = m_nextSeqCounter;
    m_nextSeqCounter   = static_cast<uint8_t>((m_nextSeqCounter + 1) & 0x07);

    // ---- Frame 0 ----
    std::array<uint8_t, kFrameLen> frame0{};
    frame0[0] = static_cast<uint8_t>((seq << 5) | 0x00); // frame index 0
    frame0[1] = static_cast<uint8_t>(data.size());
    const size_t firstChunk = std::min(kFirstFrameLen, data.size());
    std::copy(data.begin(), data.begin() + static_cast<long>(firstChunk), frame0.begin() + 2);
    std::fill(frame0.begin() + 2 + static_cast<long>(firstChunk), frame0.end(), kPaddingByte);

    auto wr0 = driver.tout_write(u32WriteTimeout, std::span<const uint8_t>(frame0.data(), kFrameLen), txId);
    if (wr0.status != ICommDriver::Status::SUCCESS)
    {
        result.status = wr0.status;
        return result;
    }

    size_t sent = firstChunk;
    uint8_t frameIdx = 1;

    while (sent < data.size())
    {
        std::array<uint8_t, kFrameLen> frame{};
        frame[0] = static_cast<uint8_t>((seq << 5) | (frameIdx & 0x1F));
        const size_t chunk = std::min(kContFrameLen, data.size() - sent);
        std::copy(data.begin() + static_cast<long>(sent),
                  data.begin() + static_cast<long>(sent + chunk),
                  frame.begin() + 1);
        std::fill(frame.begin() + 1 + static_cast<long>(chunk), frame.end(), kPaddingByte);

        auto wr = driver.tout_write(u32WriteTimeout, std::span<const uint8_t>(frame.data(), kFrameLen), txId);
        if (wr.status != ICommDriver::Status::SUCCESS)
        {
            result.status = wr.status;
            result.bytes_written = sent;
            return result;
        }

        sent += chunk;
        ++frameIdx; // fastPacketMaxMessageLen (223) caps this at 31, never overflows the 5-bit field
    }

    result.status = ICommDriver::Status::SUCCESS;
    result.bytes_written = data.size();
    return result;
}


// ============================================================================
// RECEIVE
// ============================================================================

ICommDriver::ReadResult Nmea2000FastPacketProtocol::receive(
    const ICommDriver& driver,
    uint32_t u32ReadTimeout,
    std::span<uint8_t> buffer,
    std::string_view rxId,
    std::string_view /*txId*/) const // no handshake frames to send back
{
    ICommDriver::ReadResult result;

    std::array<uint8_t, kFrameLen> frame{};
    ICommDriver::ReadOptions opts;
    opts.mode = ICommDriver::ReadMode::Exact;

    auto rr = driver.tout_read(u32ReadTimeout, std::span<uint8_t>(frame.data(), frame.size()), opts, rxId);
    if (rr.status != ICommDriver::Status::SUCCESS || rr.bytes_read == 0)
    {
        result.status = rr.status;
        return result;
    }

    const uint8_t seq      = static_cast<uint8_t>((frame[0] >> 5) & 0x07);
    const uint8_t frameIdx = static_cast<uint8_t>(frame[0] & 0x1F);

    if (frameIdx != 0)
    {
        // First frame we saw wasn't frame 0 of a message — either we tuned
        // in mid-transfer, or this rxId isn't carrying Fast Packet data.
        result.status = ICommDriver::Status::PROTOCOL_ERROR;
        return result;
    }

    const size_t totalLen = frame[1];

    if (totalLen == 0)
    {
        result.status = ICommDriver::Status::PROTOCOL_ERROR;
        return result;
    }

    if (totalLen > buffer.size())
    {
        // No handshake channel exists to tell the sender to stop — this is
        // a real Fast Packet limitation, not a shortcut. The remaining
        // frames are simply left unread; the caller's next receive() call
        // on this rxId will desync (frameIdx != 0) and fail cleanly rather
        // than silently splicing two messages together.
        result.status = ICommDriver::Status::BUFFER_OVERFLOW;
        return result;
    }

    size_t received = std::min(kFirstFrameLen, totalLen);
    std::copy(frame.begin() + 2, frame.begin() + 2 + received, buffer.begin());

    uint8_t expectedIdx = 1;

    while (received < totalLen)
    {
        std::array<uint8_t, kFrameLen> cont{};
        auto rrCont = driver.tout_read(m_cfg.timeoutFpInterFrame_ms,
                                        std::span<uint8_t>(cont.data(), cont.size()), opts, rxId);
        if (rrCont.status != ICommDriver::Status::SUCCESS || rrCont.bytes_read == 0)
        {
            result.status = (rrCont.status == ICommDriver::Status::READ_TIMEOUT)
                             ? ICommDriver::Status::READ_TIMEOUT
                             : ICommDriver::Status::PROTOCOL_ERROR;
            return result;
        }

        const uint8_t gotSeq = static_cast<uint8_t>((cont[0] >> 5) & 0x07);
        const uint8_t gotIdx = static_cast<uint8_t>(cont[0] & 0x1F);

        if (gotSeq != seq || gotIdx != expectedIdx)
        {
            // Either a different concurrent message interleaved on this
            // same rxId, or a frame was lost — this implementation doesn't
            // attempt to buffer/reorder, it just reports the desync.
            result.status = ICommDriver::Status::PROTOCOL_ERROR;
            return result;
        }

        const size_t chunk = std::min<size_t>({kContFrameLen, totalLen - received, rrCont.bytes_read - 1});
        std::copy(cont.begin() + 1, cont.begin() + 1 + chunk, buffer.begin() + static_cast<long>(received));
        received += chunk;
        ++expectedIdx;
    }

    result.status = ICommDriver::Status::SUCCESS;
    result.bytes_read = received;
    return result;
}
