#include "IsoTpProtocol.hpp"

#include <array>
#include <algorithm>
#include <thread>
#include <chrono>

namespace
{
    constexpr size_t  kFrameLen   = 8; // classic CAN frame length used for every SF/FF/CF/FC
    constexpr size_t  kSfMaxLen   = 7; // 1 PCI byte + up to 7 data bytes
    constexpr size_t  kFfFirstLen = 6; // FF: 2 PCI bytes + 6 data bytes
    constexpr size_t  kCfMaxLen   = 7; // 1 PCI byte + up to 7 data bytes

    constexpr uint8_t kPciSF = 0x0;
    constexpr uint8_t kPciFF = 0x1;
    constexpr uint8_t kPciCF = 0x2;
    constexpr uint8_t kPciFC = 0x3;

    constexpr uint8_t kFsClearToSend = 0x0;
    constexpr uint8_t kFsWait        = 0x1;
    constexpr uint8_t kFsOverflow    = 0x2;

    inline uint8_t pci_type(uint8_t b0) { return static_cast<uint8_t>((b0 & 0xF0) >> 4); }

    inline void fill_padding(std::array<uint8_t, kFrameLen>& frame, size_t usedLen,
                              bool pad, uint8_t padByte)
    {
        if (pad && usedLen < kFrameLen)
        {
            std::fill(frame.begin() + static_cast<long>(usedLen), frame.end(), padByte);
        }
    }
}


void IsoTpProtocol::sleep_st_min(uint8_t stMin)
{
    if (stMin == 0)
    {
        return;
    }
    if (stMin <= 0x7F)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(stMin));
    }
    else if (stMin >= 0xF1 && stMin <= 0xF9)
    {
        std::this_thread::sleep_for(std::chrono::microseconds(100u * (stMin - 0xF0u)));
    }
    // 0x80-0xF0 and 0xFA-0xFF are reserved by the standard; treat as "no delay".
}


// ============================================================================
// SEND
// ============================================================================

ICommDriver::WriteResult IsoTpProtocol::send(
    const ICommDriver& driver,
    uint32_t u32WriteTimeout,
    std::span<const uint8_t> data,
    std::string_view txId,
    std::string_view rxId) const
{
    ICommDriver::WriteResult result;

    if (data.empty() || data.size() > m_cfg.maxMessageLen)
    {
        result.status = ICommDriver::Status::INVALID_PARAM;
        return result;
    }

    // ---- Single Frame: exactly one physical frame, same call count as
    // ---- TpProtocol::NONE today. --------------------------------------
    if (data.size() <= kSfMaxLen)
    {
        std::array<uint8_t, kFrameLen> frame{};
        frame[0] = static_cast<uint8_t>(data.size() & 0x0F); // PCI=SF, SF_DL=len
        std::copy(data.begin(), data.end(), frame.begin() + 1);
        const size_t usedLen = 1 + data.size();
        fill_padding(frame, usedLen, m_cfg.padFrames, m_cfg.paddingByte);
        const size_t txLen = m_cfg.padFrames ? kFrameLen : usedLen;

        auto wr = driver.tout_write(u32WriteTimeout,
                                     std::span<const uint8_t>(frame.data(), txLen), txId);
        result.status = wr.status;
        result.bytes_written = (wr.status == ICommDriver::Status::SUCCESS) ? data.size() : 0;
        return result;
    }

    // ---- Multi-frame: First Frame, then wait for Flow Control, then
    // ---- Consecutive Frames paced by BS/STmin. --------------------------
    std::array<uint8_t, kFrameLen> ff{};
    ff[0] = static_cast<uint8_t>(0x10 | ((data.size() >> 8) & 0x0F));
    ff[1] = static_cast<uint8_t>(data.size() & 0xFF);
    std::copy(data.begin(), data.begin() + static_cast<long>(kFfFirstLen), ff.begin() + 2);

    auto wrFF = driver.tout_write(u32WriteTimeout, std::span<const uint8_t>(ff.data(), kFrameLen), txId);
    if (wrFF.status != ICommDriver::Status::SUCCESS)
    {
        result.status = wrFF.status;
        return result;
    }

    size_t sent = kFfFirstLen;
    uint8_t seq = 1;

    while (sent < data.size())
    {
        // ---- Wait for a Flow Control frame from the peer. ---------------
        std::array<uint8_t, kFrameLen> fc{};
        ICommDriver::ReadOptions opts;
        opts.mode = ICommDriver::ReadMode::Exact;

        auto rr = driver.tout_read(m_cfg.timeoutNBs_ms,
                                    std::span<uint8_t>(fc.data(), fc.size()), opts, rxId);
        if (rr.status != ICommDriver::Status::SUCCESS || rr.bytes_read < 3)
        {
            result.status = (rr.status == ICommDriver::Status::READ_TIMEOUT)
                             ? ICommDriver::Status::WRITE_TIMEOUT
                             : ICommDriver::Status::PROTOCOL_ERROR;
            return result;
        }

        if (pci_type(fc[0]) != kPciFC)
        {
            result.status = ICommDriver::Status::PROTOCOL_ERROR;
            return result;
        }

        const uint8_t fs = fc[0] & 0x0F;
        if (fs == kFsOverflow)
        {
            result.status = ICommDriver::Status::NACK; // peer aborted the transfer
            return result;
        }
        if (fs == kFsWait)
        {
            continue; // peer needs more time before it can accept data; poll again
        }
        if (fs != kFsClearToSend)
        {
            result.status = ICommDriver::Status::PROTOCOL_ERROR;
            return result;
        }

        const uint8_t blockSize = fc[1];
        const uint8_t stMin     = fc[2];

        uint8_t framesInBlock = 0;
        while (sent < data.size() && (blockSize == 0 || framesInBlock < blockSize))
        {
            std::array<uint8_t, kFrameLen> cf{};
            cf[0] = static_cast<uint8_t>(0x20 | (seq & 0x0F));
            const size_t chunk = std::min(kCfMaxLen, data.size() - sent);
            std::copy(data.begin() + static_cast<long>(sent),
                      data.begin() + static_cast<long>(sent + chunk),
                      cf.begin() + 1);
            const size_t usedLen = 1 + chunk;
            fill_padding(cf, usedLen, m_cfg.padFrames, m_cfg.paddingByte);
            const size_t txLen = m_cfg.padFrames ? kFrameLen : usedLen;

            auto wrCf = driver.tout_write(u32WriteTimeout,
                                           std::span<const uint8_t>(cf.data(), txLen), txId);
            if (wrCf.status != ICommDriver::Status::SUCCESS)
            {
                result.status = wrCf.status;
                return result;
            }

            sent += chunk;
            seq = static_cast<uint8_t>((seq + 1) & 0x0F);
            ++framesInBlock;

            if (sent < data.size())
            {
                sleep_st_min(stMin);
            }
        }
        // blockSize frames sent (or message complete) — loop back for the
        // next Flow Control frame unless the transfer is already done.
    }

    result.status = ICommDriver::Status::SUCCESS;
    result.bytes_written = data.size();
    return result;
}


// ============================================================================
// RECEIVE
// ============================================================================

ICommDriver::ReadResult IsoTpProtocol::receive(
    const ICommDriver& driver,
    uint32_t u32ReadTimeout,
    std::span<uint8_t> buffer,
    std::string_view rxId,
    std::string_view txId) const
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

    const uint8_t type = pci_type(frame[0]);

    // ---- Single Frame: one physical frame in, done. ------------------------
    if (type == kPciSF)
    {
        const uint8_t len = frame[0] & 0x0F;
        if (len == 0 || len > rr.bytes_read - 1)
        {
            result.status = ICommDriver::Status::PROTOCOL_ERROR;
            return result;
        }
        if (len > buffer.size())
        {
            result.status = ICommDriver::Status::BUFFER_OVERFLOW;
            return result;
        }
        std::copy(frame.begin() + 1, frame.begin() + 1 + len, buffer.begin());
        result.status = ICommDriver::Status::SUCCESS;
        result.bytes_read = len;
        return result;
    }

    if (type != kPciFF)
    {
        // A lone CF/FC with no preceding FF for this exchange.
        result.status = ICommDriver::Status::PROTOCOL_ERROR;
        return result;
    }

    // ---- First Frame -------------------------------------------------------
    const size_t totalLen = (static_cast<size_t>(frame[0] & 0x0F) << 8) | frame[1];

    if (totalLen == 0 || totalLen <= kSfMaxLen)
    {
        result.status = ICommDriver::Status::PROTOCOL_ERROR; // malformed FF
        return result;
    }

    if (totalLen > buffer.size())
    {
        // Tell the peer we can't take this message, then stop.
        std::array<uint8_t, kFrameLen> fcAbort{};
        fcAbort[0] = static_cast<uint8_t>(0x30 | kFsOverflow);
        driver.tout_write(u32ReadTimeout, std::span<const uint8_t>(fcAbort.data(), 3), txId);
        result.status = ICommDriver::Status::BUFFER_OVERFLOW;
        return result;
    }

    size_t received = std::min<size_t>(kFfFirstLen, totalLen);
    std::copy(frame.begin() + 2, frame.begin() + 2 + received, buffer.begin());

    // ---- Grant Flow Control (Clear To Send) --------------------------------
    std::array<uint8_t, kFrameLen> fc{};
    fc[0] = static_cast<uint8_t>(0x30 | kFsClearToSend);
    fc[1] = m_cfg.blockSize;
    fc[2] = m_cfg.stMin;
    fill_padding(fc, 3, m_cfg.padFrames, m_cfg.paddingByte);
    const size_t fcLen = m_cfg.padFrames ? kFrameLen : 3;

    auto wrFc = driver.tout_write(u32ReadTimeout, std::span<const uint8_t>(fc.data(), fcLen), txId);
    if (wrFc.status != ICommDriver::Status::SUCCESS)
    {
        result.status = wrFc.status;
        return result;
    }

    uint8_t expectedSeq  = 1;
    uint8_t framesSinceFc = 0;

    while (received < totalLen)
    {
        std::array<uint8_t, kFrameLen> cf{};
        auto rrCf = driver.tout_read(m_cfg.timeoutNCr_ms,
                                      std::span<uint8_t>(cf.data(), cf.size()), opts, rxId);
        if (rrCf.status != ICommDriver::Status::SUCCESS || rrCf.bytes_read == 0)
        {
            result.status = (rrCf.status == ICommDriver::Status::READ_TIMEOUT)
                             ? ICommDriver::Status::READ_TIMEOUT
                             : ICommDriver::Status::PROTOCOL_ERROR;
            return result;
        }

        if (pci_type(cf[0]) != kPciCF || (cf[0] & 0x0F) != expectedSeq)
        {
            result.status = ICommDriver::Status::PROTOCOL_ERROR;
            return result;
        }

        const size_t chunk = std::min<size_t>({kCfMaxLen, totalLen - received, rrCf.bytes_read - 1});
        std::copy(cf.begin() + 1, cf.begin() + 1 + chunk, buffer.begin() + static_cast<long>(received));
        received += chunk;
        expectedSeq = static_cast<uint8_t>((expectedSeq + 1) & 0x0F);
        ++framesSinceFc;

        if (m_cfg.blockSize != 0 && framesSinceFc >= m_cfg.blockSize && received < totalLen)
        {
            auto wrFc2 = driver.tout_write(u32ReadTimeout, std::span<const uint8_t>(fc.data(), fcLen), txId);
            if (wrFc2.status != ICommDriver::Status::SUCCESS)
            {
                result.status = wrFc2.status;
                return result;
            }
            framesSinceFc = 0;
        }
    }

    result.status = ICommDriver::Status::SUCCESS;
    result.bytes_read = received;
    return result;
}
