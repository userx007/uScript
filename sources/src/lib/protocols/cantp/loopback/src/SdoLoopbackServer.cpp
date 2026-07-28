#include "SdoLoopbackServer.hpp"

#include <algorithm>
#include <array>

namespace
{
    using Frame = std::array<uint8_t, 8>;

    constexpr uint8_t kCcsDlSegment  = 0;
    constexpr uint8_t kCcsDlInitiate = 1;
    constexpr uint8_t kCcsUlInitiate = 2;
    constexpr uint8_t kCcsUlSegment  = 3;

    constexpr uint8_t kScsUlSegment  = 0;
    constexpr uint8_t kScsDlSegment  = 1;
    constexpr uint8_t kScsUlInitiate = 2;
    constexpr uint8_t kScsDlInitiate = 3;

    bool read_frame(const ICommDriver &bus, uint32_t timeoutMs, std::string_view rxId, Frame &out)
    {
        ICommDriver::ReadOptions opts;
        opts.mode = ICommDriver::ReadMode::Exact;
        auto rr = bus.tout_read(timeoutMs, std::span<uint8_t>(out.data(), out.size()), opts, rxId);
        return rr.status == ICommDriver::Status::SUCCESS;
    }

    bool write_frame(const ICommDriver &bus, uint32_t timeoutMs, std::string_view txId, const Frame &f)
    {
        auto wr = bus.tout_write(timeoutMs, std::span<const uint8_t>(f.data(), f.size()), txId);
        return wr.status == ICommDriver::Status::SUCCESS;
    }
}

bool SdoLoopbackServer::serve_download(const ICommDriver &bus, std::string_view rxId, std::string_view txId,
                                        uint32_t timeoutMs, std::vector<uint8_t> &outData)
{
    outData.clear();

    Frame req{};
    if (!read_frame(bus, timeoutMs, rxId, req)) return false;
    if ((req[0] >> 5) != kCcsDlInitiate) return false;

    const bool e = ((req[0] >> 1) & 1) != 0;
    const bool s = (req[0] & 1) != 0;

    Frame ack{};
    ack[1] = req[1]; ack[2] = req[2]; ack[3] = req[3]; // echo index/sub-index

    if (e)
    {
        // ---- Expedited download: 1-4 bytes, one round trip. ----
        const uint8_t n   = s ? static_cast<uint8_t>((req[0] >> 2) & 0x03) : 0;
        const size_t  len = s ? static_cast<size_t>(4 - n) : 4;
        outData.assign(req.begin() + 4, req.begin() + 4 + static_cast<long>(len));

        ack[0] = static_cast<uint8_t>((kScsDlInitiate << 5) | 1);
        return write_frame(bus, timeoutMs, txId, ack);
    }

    // ---- Segmented download: size announced up front, then N segments. ----
    if (!s) return false; // this emulator, like the client, requires a size-indicated initiate

    const size_t totalLen = static_cast<size_t>(req[4]) | (static_cast<size_t>(req[5]) << 8) |
                             (static_cast<size_t>(req[6]) << 16) | (static_cast<size_t>(req[7]) << 24);
    outData.reserve(totalLen);

    ack[0] = static_cast<uint8_t>((kScsDlInitiate << 5) | 1);
    if (!write_frame(bus, timeoutMs, txId, ack)) return false;

    while (outData.size() < totalLen)
    {
        Frame seg{};
        if (!read_frame(bus, timeoutMs, rxId, seg)) return false;
        if ((seg[0] >> 5) != kCcsDlSegment) return false;

        const uint8_t toggle = (seg[0] >> 4) & 1;
        const uint8_t n       = (seg[0] >> 1) & 0x07;
        const bool    last    = (seg[0] & 1) != 0;
        const size_t  chunk   = 7 - n;

        outData.insert(outData.end(), seg.begin() + 1, seg.begin() + 1 + static_cast<long>(chunk));

        Frame segAck{};
        segAck[0] = static_cast<uint8_t>((kScsDlSegment << 5) | (toggle << 4));
        if (!write_frame(bus, timeoutMs, txId, segAck)) return false;

        if (last) break;
    }

    return outData.size() == totalLen;
}

bool SdoLoopbackServer::serve_upload(const ICommDriver &bus, std::string_view rxId, std::string_view txId,
                                      uint32_t timeoutMs, const std::vector<uint8_t> &data)
{
    Frame req{};
    if (!read_frame(bus, timeoutMs, rxId, req)) return false;
    if ((req[0] >> 5) != kCcsUlInitiate) return false;

    Frame resp{};
    resp[1] = req[1]; resp[2] = req[2]; resp[3] = req[3]; // echo index/sub-index

    if (data.size() <= 4)
    {
        // ---- Expedited upload: 1-4 bytes, one round trip. ----
        const uint8_t n = static_cast<uint8_t>(4 - data.size());
        resp[0] = static_cast<uint8_t>((kScsUlInitiate << 5) | (n << 2) | (1 << 1) | 1); // e=1,s=1
        std::copy(data.begin(), data.end(), resp.begin() + 4);
        return write_frame(bus, timeoutMs, txId, resp);
    }

    // ---- Segmented upload: announce size, then stream segments on request. ----
    resp[0] = static_cast<uint8_t>((kScsUlInitiate << 5) | 1); // e=0,s=1
    const uint32_t sz = static_cast<uint32_t>(data.size());
    resp[4] = static_cast<uint8_t>(sz & 0xFF);
    resp[5] = static_cast<uint8_t>((sz >> 8) & 0xFF);
    resp[6] = static_cast<uint8_t>((sz >> 16) & 0xFF);
    resp[7] = static_cast<uint8_t>((sz >> 24) & 0xFF);
    if (!write_frame(bus, timeoutMs, txId, resp)) return false;

    size_t sent = 0;
    while (sent < data.size())
    {
        Frame segReq{};
        if (!read_frame(bus, timeoutMs, rxId, segReq)) return false;
        if ((segReq[0] >> 5) != kCcsUlSegment) return false;
        const uint8_t toggle = (segReq[0] >> 4) & 1;

        const size_t  chunk = std::min<size_t>(7, data.size() - sent);
        const bool    last  = (sent + chunk == data.size());
        const uint8_t n     = static_cast<uint8_t>(7 - chunk);

        Frame segResp{};
        segResp[0] = static_cast<uint8_t>((kScsUlSegment << 5) | (toggle << 4) | (n << 1) | (last ? 1 : 0));
        std::copy(data.begin() + static_cast<long>(sent), data.begin() + static_cast<long>(sent + chunk), segResp.begin() + 1);
        if (!write_frame(bus, timeoutMs, txId, segResp)) return false;

        sent += chunk;
    }

    return true;
}
