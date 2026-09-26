#include "SdoLoopbackServer.hpp"

#include "ICommDriver.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <stddef.h>

namespace {
    using Frame                      = std::array<uint8_t, 8>;

    constexpr uint8_t kCcsDlSegment  = 0;
    constexpr uint8_t kCcsDlInitiate = 1;
    constexpr uint8_t kCcsUlInitiate = 2;
    constexpr uint8_t kCcsUlSegment  = 3;

    constexpr uint8_t kScsUlSegment  = 0;
    constexpr uint8_t kScsDlSegment  = 1;
    constexpr uint8_t kScsUlInitiate = 2;
    constexpr uint8_t kScsDlInitiate = 3;

    bool read_frame(const ICommDriver &bus, uint32_t u32TimeoutMs, std::string_view rxId, Frame &sOut)
    {
        ICommDriver::ReadOptions opts;
        opts.mode = ICommDriver::ReadMode::Exact;
        auto rr   = bus.tout_read(u32TimeoutMs, std::span<uint8_t>(sOut.data(), sOut.size()), opts, rxId);
        return rr.status == ICommDriver::Status::SUCCESS;
    }

    bool write_frame(const ICommDriver &bus, uint32_t u32TimeoutMs, std::string_view txId, const Frame &sF)
    {
        auto wr = bus.tout_write(u32TimeoutMs, std::span<const uint8_t>(sF.data(), sF.size()), txId);
        return wr.status == ICommDriver::Status::SUCCESS;
    }

    // Body of serve_download(), split out so serve_one() can hand in an
    // Initiate frame it already read (see SdoLoopbackServer.hpp) instead of
    // this function reading its own — reading a second frame here would
    // silently drop whatever the peer sends next.
    bool serve_download_from(const ICommDriver &bus, std::string_view rxId, std::string_view txId,
                             uint32_t u32TimeoutMs, const Frame &sReq, std::vector<uint8_t> &vOutData)
    {
        vOutData.clear();
        if ((sReq[0] >> 5) != kCcsDlInitiate) {
            return false;
        }

        const bool e = ((sReq[0] >> 1) & 1) != 0;
        const bool s = (sReq[0] & 1) != 0;

        Frame ack{};
        ack[1] = sReq[1];
        ack[2] = sReq[2];
        ack[3] = sReq[3]; // echo index/sub-index

        if (e) {
            // ---- Expedited download: 1-4 bytes, one round trip. ----
            const uint8_t n  = s ? static_cast<uint8_t>((sReq[0] >> 2) & 0x03) : 0;
            const size_t len = s ? static_cast<size_t>(4 - n) : 4;
            vOutData.assign(sReq.begin() + 4, sReq.begin() + 4 + static_cast<long>(len));

            ack[0] = static_cast<uint8_t>((kScsDlInitiate << 5) | 1);
            return write_frame(bus, u32TimeoutMs, txId, ack);
        }

        // ---- Segmented download: size announced up front, then N segments. ----
        if (!s) {
            return false; // this emulator, like the client, requires a size-indicated initiate
        }

        const size_t totalLen = static_cast<size_t>(sReq[4]) | (static_cast<size_t>(sReq[5]) << 8) |
                                (static_cast<size_t>(sReq[6]) << 16) | (static_cast<size_t>(sReq[7]) << 24);
        vOutData.reserve(totalLen);

        ack[0] = static_cast<uint8_t>((kScsDlInitiate << 5) | 1);
        if (!write_frame(bus, u32TimeoutMs, txId, ack)) {
            return false;
        }

        while (vOutData.size() < totalLen) {
            Frame seg{};
            if (!read_frame(bus, u32TimeoutMs, rxId, seg)) {
                return false;
            }
            if ((seg[0] >> 5) != kCcsDlSegment) {
                return false;
            }

            const uint8_t toggle = (seg[0] >> 4) & 1;
            const uint8_t n      = (seg[0] >> 1) & 0x07;
            const bool last      = (seg[0] & 1) != 0;
            const size_t chunk   = 7 - n;

            vOutData.insert(vOutData.end(), seg.begin() + 1, seg.begin() + 1 + static_cast<long>(chunk));

            Frame segAck{};
            segAck[0] = static_cast<uint8_t>((kScsDlSegment << 5) | (toggle << 4));
            if (!write_frame(bus, u32TimeoutMs, txId, segAck)) {
                return false;
            }

            if (last) {
                break;
            }
        }

        return vOutData.size() == totalLen;
    }

    // Body of serve_upload(); see serve_download_from() above for why this split exists.
    bool serve_upload_from(const ICommDriver &bus, std::string_view rxId, std::string_view txId,
                           uint32_t u32TimeoutMs, const Frame &sReq, const std::vector<uint8_t> &vData)
    {
        if ((sReq[0] >> 5) != kCcsUlInitiate) {
            return false;
        }

        Frame resp{};
        resp[1] = sReq[1];
        resp[2] = sReq[2];
        resp[3] = sReq[3]; // echo index/sub-index

        if (vData.size() <= 4) {
            // ---- Expedited upload: 1-4 bytes, one round trip. ----
            const uint8_t n = static_cast<uint8_t>(4 - vData.size());
            resp[0]         = static_cast<uint8_t>((kScsUlInitiate << 5) | (n << 2) | (1 << 1) | 1); // e=1,s=1
            std::copy(vData.begin(), vData.end(), resp.begin() + 4);
            return write_frame(bus, u32TimeoutMs, txId, resp);
        }

        // ---- Segmented upload: announce size, then stream segments on request. ----
        resp[0]           = static_cast<uint8_t>((kScsUlInitiate << 5) | 1); // e=0,s=1
        const uint32_t sz = static_cast<uint32_t>(vData.size());
        resp[4]           = static_cast<uint8_t>(sz & 0xFF);
        resp[5]           = static_cast<uint8_t>((sz >> 8) & 0xFF);
        resp[6]           = static_cast<uint8_t>((sz >> 16) & 0xFF);
        resp[7]           = static_cast<uint8_t>((sz >> 24) & 0xFF);
        if (!write_frame(bus, u32TimeoutMs, txId, resp)) {
            return false;
        }

        size_t sent = 0;
        while (sent < vData.size()) {
            Frame segReq{};
            if (!read_frame(bus, u32TimeoutMs, rxId, segReq)) {
                return false;
            }
            if ((segReq[0] >> 5) != kCcsUlSegment) {
                return false;
            }
            const uint8_t toggle = (segReq[0] >> 4) & 1;

            const size_t chunk   = std::min<size_t>(7, vData.size() - sent);
            const bool last      = (sent + chunk == vData.size());
            const uint8_t n      = static_cast<uint8_t>(7 - chunk);

            Frame segResp{};
            segResp[0] = static_cast<uint8_t>((kScsUlSegment << 5) | (toggle << 4) | (n << 1) | (last ? 1 : 0));
            std::copy(vData.begin() + static_cast<long>(sent), vData.begin() + static_cast<long>(sent + chunk), segResp.begin() + 1);
            if (!write_frame(bus, u32TimeoutMs, txId, segResp)) {
                return false;
            }

            sent += chunk;
        }

        return true;
    }
} // namespace

bool SdoLoopbackServer::serve_download(const ICommDriver &bus, std::string_view rxId, std::string_view txId,
                                       uint32_t u32TimeoutMs, std::vector<uint8_t> &vOutData)
{
    Frame req{};
    if (!read_frame(bus, u32TimeoutMs, rxId, req)) {
        return false;
    }
    return serve_download_from(bus, rxId, txId, u32TimeoutMs, req, vOutData);
}

bool SdoLoopbackServer::serve_upload(const ICommDriver &bus, std::string_view rxId, std::string_view txId,
                                     uint32_t u32TimeoutMs, const std::vector<uint8_t> &vData)
{
    Frame req{};
    if (!read_frame(bus, u32TimeoutMs, rxId, req)) {
        return false;
    }
    return serve_upload_from(bus, rxId, txId, u32TimeoutMs, req, vData);
}

bool SdoLoopbackServer::serve_one(const ICommDriver &bus, std::string_view rxId, std::string_view txId,
                                  uint32_t u32TimeoutMs, std::vector<uint8_t> &vStored,
                                  const std::function<void(bool bIsDownload)> &onDirectionKnown)
{
    Frame req{};
    if (!read_frame(bus, u32TimeoutMs, rxId, req)) {
        return false;
    }

    const uint8_t ccs = req[0] >> 5;
    if (ccs == kCcsDlInitiate) {
        if (onDirectionKnown) {
            onDirectionKnown(true);
        }
        return serve_download_from(bus, rxId, txId, u32TimeoutMs, req, vStored);
    }
    if (ccs == kCcsUlInitiate) {
        if (onDirectionKnown) {
            onDirectionKnown(false);
        }
        return serve_upload_from(bus, rxId, txId, u32TimeoutMs, req, vStored);
    }

    return false; // not an Initiate frame — unexpected as the first frame of a transaction
}
