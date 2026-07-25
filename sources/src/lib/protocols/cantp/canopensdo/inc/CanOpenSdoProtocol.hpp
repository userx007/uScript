#ifndef CAN_TP_CANOPEN_SDO_PROTOCOL_HPP
#define CAN_TP_CANOPEN_SDO_PROTOCOL_HPP

#include "ITransportProtocol.hpp"
#include "TpConfig.hpp"

#include <array>
#include <cstdint>

/**
 * @file CanOpenSdoProtocol.hpp
 * @brief CANopen SDO transfer (CiA 301 §7.2.4) — expedited, segmented, and
 *        block transfer, over classic 8-byte CAN frames.
 *
 * Unlike ISO-TP/J1939/Fast Packet, SDO is not a generic "send N bytes"
 * pipe — it is a client/server protocol for reading and writing a specific
 * Object Dictionary (OD) entry, addressed by a 16-bit index and 8-bit
 * sub-index. Those two numbers have to come from somewhere, and since
 * ITransportProtocol::send()/receive() only take a raw byte buffer (no
 * addressing fields), they live in TpConfig (see TpConfig::canOpenIndex /
 * canOpenSubIndex) — the same place every other protocol-specific knob
 * lives, and consistent with how J1939's own addressing (PGN/SA/DA baked
 * into the caller-resolved arbitration id) is kept out of the generic
 * interface too.
 *
 * send() = SDO download (client writes data to the server's OD entry).
 * receive() = SDO upload (client reads the server's OD entry into buffer).
 * "Client" is always us; the peer addressed by txId/rxId is the SDO server.
 *
 * Transfer variant selection:
 *   - size <= 4 bytes: always expedited (one request/response), regardless
 *     of TpConfig::canOpenUseBlock — mirrors how IsoTpProtocol::send()
 *     degrades a short payload to a Single Frame.
 *   - size > 4 bytes: segmented (default) or block transfer, per
 *     TpConfig::canOpenUseBlock. For receive(), block transfer is
 *     requested first; a server that doesn't support it replies with a
 *     plain "Initiate Upload" response instead of "Initiate Block Upload",
 *     which this implementation detects and falls back to the
 *     segmented/expedited continuation transparently.
 *
 * Scope / simplifications (documented, not hidden):
 *   - CRC on block transfers is never negotiated (we always advertise "no
 *     CRC support"), which is spec-legal — a transfer proceeds without CRC
 *     whenever either side doesn't support it.
 *   - No gap/retransmission recovery: a missing or out-of-order segment
 *     during a segmented or block transfer ends the transfer with
 *     Status::PROTOCOL_ERROR rather than requesting a resend.
 *   - receive() requires the server to indicate the transfer size up front
 *     (the "s" bit in its Initiate Upload response) for anything beyond
 *     expedited; a server that omits it is treated as a protocol error
 *     rather than read-until-something-looks-like-the-end.
 */
class CanOpenSdoProtocol final : public ITransportProtocol
{
    public:

        explicit CanOpenSdoProtocol(const TpConfig& cfg = {}) : m_cfg(cfg) {}

        ICommDriver::WriteResult send(
            const ICommDriver& driver,
            uint32_t u32WriteTimeout,
            std::span<const uint8_t> data,
            std::string_view txId,
            std::string_view rxId = {}) const override;

        ICommDriver::ReadResult receive(
            const ICommDriver& driver,
            uint32_t u32ReadTimeout,
            std::span<uint8_t> buffer,
            std::string_view rxId,
            std::string_view txId = {}) const override;

        TpProtocol id() const override { return TpProtocol::CANOPEN_SDO; }

    private:

        TpConfig m_cfg;

        using Frame = std::array<uint8_t, 8>;

        // ---- send() (download) helpers ----
        ICommDriver::WriteResult sendExpedited(const ICommDriver&, uint32_t, std::span<const uint8_t>,
                                               std::string_view, std::string_view) const;
        ICommDriver::WriteResult sendSegmented(const ICommDriver&, uint32_t, std::span<const uint8_t>,
                                               std::string_view, std::string_view) const;
        ICommDriver::WriteResult sendBlock(const ICommDriver&, uint32_t, std::span<const uint8_t>,
                                           std::string_view, std::string_view) const;

        // ---- receive() (upload) helpers ----
        ICommDriver::ReadResult receiveNormal(const ICommDriver&, uint32_t, std::span<uint8_t>,
                                              std::string_view, std::string_view) const;
        ICommDriver::ReadResult receiveBlock(const ICommDriver&, uint32_t, std::span<uint8_t>,
                                             std::string_view, std::string_view) const;
        /** Finishes an upload given an already-received Initiate-Upload-style response
         *  (scs=UL_INITIATE) — shared by receiveNormal() and receiveBlock()'s
         *  fallback when the server declines block transfer. */
        ICommDriver::ReadResult finishUploadFromInitiateResponse(const Frame& resp,
            const ICommDriver&, uint32_t, std::span<uint8_t>, std::string_view, std::string_view) const;

        // ---- shared wire-format helpers ----
        void packIndex(Frame& f) const;
        static void sendAbort(const ICommDriver& driver, uint32_t timeout, std::string_view txId,
                              const Frame& ctx, uint32_t abortCode);
};

#endif // CAN_TP_CANOPEN_SDO_PROTOCOL_HPP
