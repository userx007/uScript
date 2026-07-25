#ifndef CAN_TP_NMEA2000_FAST_PACKET_PROTOCOL_HPP
#define CAN_TP_NMEA2000_FAST_PACKET_PROTOCOL_HPP

#include "ITransportProtocol.hpp"
#include "TpConfig.hpp"

/**
 * @file Nmea2000FastPacketProtocol.hpp
 * @brief NMEA 2000 "Fast Packet" transport protocol over classic 8-byte CAN frames.
 *
 * Fast Packet is the multi-frame scheme NMEA 2000 uses for PGNs whose data is
 * 9-223 bytes. It has no handshake: the sender streams frames and the
 * receiver reassembles what it gets.
 *
 * Frame layout (identical for every frame, all 8 bytes / one physical CAN
 * frame each):
 *   byte0  bits 7-5: sequence counter (0-7) — lets a receiver tell two
 *                    messages using the same PGN/CAN id apart if a new one
 *                    starts before an old one finished. Constant for every
 *                    frame of one message.
 *          bits 4-0: frame counter — 0 for the first frame, incrementing by
 *                    one for each subsequent frame (max 31, so 32 frames
 *                    total per message).
 *   Frame 0: byte1 = total payload length (0-223); bytes 2-7 = first 6 data bytes.
 *   Frame N (N>=1): bytes 1-7 = next 7 data bytes.
 *
 *   6 + 31*7 = 223 bytes, the protocol's hard payload ceiling.
 *
 * Known limitations:
 *   - Real NMEA 2000 stacks only use Fast Packet framing for messages that
 *     don't fit in one frame — a short PGN is sent as a plain 8-byte frame
 *     with no Fast Packet header, and whether to expect Fast Packet framing
 *     for a given exchange is a property of the PGN, which this
 *     driver-agnostic library has no notion of. This implementation always
 *     uses Fast Packet framing, even for payloads that would fit in a
 *     single classic frame, to keep the wire format self-describing. This
 *     is spec-legal but not how real NMEA 2000 devices behave for short
 *     PGNs.
 *   - No flow control and no way to signal "stop, my buffer is too small"
 *     back to the sender — that is a property of Fast Packet itself. A
 *     receive() call that finds the announced length exceeds the buffer
 *     returns Status::BUFFER_OVERFLOW immediately rather than draining the
 *     frames that are still coming.
 *   - The sequence counter tags outgoing messages and keeps the
 *     continuation frames of one receive() call self-consistent; it does
 *     not demultiplex several interleaved messages arriving on the same
 *     rxId at once — receive() tracks exactly one message per call.
 */
class Nmea2000FastPacketProtocol final : public ITransportProtocol
{
    public:

        explicit Nmea2000FastPacketProtocol(const TpConfig& cfg = {}) : m_cfg(cfg) {}

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

        TpProtocol id() const override { return TpProtocol::NMEA2000_FAST_PACKET; }

    private:

        TpConfig m_cfg;

        /** @brief Cycles 0-7; a fresh value is used for every send() call. */
        mutable uint8_t m_nextSeqCounter = 0;
};

#endif // CAN_TP_NMEA2000_FAST_PACKET_PROTOCOL_HPP
