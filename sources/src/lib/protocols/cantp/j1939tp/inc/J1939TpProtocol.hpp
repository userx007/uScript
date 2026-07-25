#ifndef CAN_TP_J1939_TP_PROTOCOL_HPP
#define CAN_TP_J1939_TP_PROTOCOL_HPP

#include "ITransportProtocol.hpp"
#include "TpConfig.hpp"

/**
 * @file J1939TpProtocol.hpp
 * @brief SAE J1939-21 Transport Protocol (TP.CM / TP.DT) over 8-byte CAN frames.
 *
 * Implements both variants selected by TpConfig::j1939UseBam:
 *   - BAM  (Broadcast Announce Message): one-shot fire, no Flow Control,
 *     paced only by a fixed inter-packet gap. rxId is not used by the
 *     sender's handshake (there is none); the receiver reassembles
 *     purely from the DT packets that follow the BAM.
 *   - RTS/CTS (peer-to-peer): request/grant handshake, mirrors ISO-TP's
 *     block-size pacing but with J1939's distinct control-message layout.
 *
 * Addressing model:
 *   J1939 frame IDs normally encode priority + PGN + source/destination
 *   address. This library treats txId/rxId as already fully-formed 29-bit
 *   arbitration IDs to use for TP.CM (control) and TP.DT (data) frames
 *   respectively; composing those IDs from priority/PGN/SA/DA is left to
 *   the caller. This keeps the protocol implementation free of any J1939
 *   address-claiming logic, which is a separate concern.
 *
 * Control message byte layouts (SAE J1939-21):
 *   RTS: [0x10][TotalSize lo][TotalSize hi][TotalPackets][MaxPackets][PGN 3 bytes]
 *   CTS: [0x11][PacketsToSend][NextPacket][0xFF][0xFF][PGN 3 bytes]
 *   EndOfMsgAck: [0x13][TotalSize lo][TotalSize hi][TotalPackets][0xFF][PGN 3 bytes]
 *   Abort: [0xFF][Reason][0xFF][0xFF][0xFF][PGN 3 bytes]
 *   BAM: [0x20][TotalSize lo][TotalSize hi][TotalPackets][0xFF][PGN 3 bytes]
 *   TP.DT: [SequenceNumber 1..255][up to 7 data bytes]
 */
class J1939TpProtocol final : public ITransportProtocol
{
    public:

        explicit J1939TpProtocol(const TpConfig& cfg = {}) : m_cfg(cfg) {}

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

        TpProtocol id() const override { return TpProtocol::J1939_TP; }

    private:

        TpConfig m_cfg;

        ICommDriver::WriteResult send_bam(
            const ICommDriver& driver, uint32_t timeout,
            std::span<const uint8_t> data, std::string_view txId) const;

        ICommDriver::WriteResult send_rts_cts(
            const ICommDriver& driver, uint32_t timeout,
            std::span<const uint8_t> data, std::string_view txId, std::string_view rxId) const;

        ICommDriver::ReadResult receive_bam(
            const ICommDriver& driver, uint32_t timeout,
            std::span<uint8_t> buffer, std::string_view rxId, const uint8_t firstFrame[8]) const;

        ICommDriver::ReadResult receive_rts_cts(
            const ICommDriver& driver, uint32_t timeout,
            std::span<uint8_t> buffer, std::string_view rxId, std::string_view txId,
            const uint8_t firstFrame[8]) const;
};

#endif // CAN_TP_J1939_TP_PROTOCOL_HPP
