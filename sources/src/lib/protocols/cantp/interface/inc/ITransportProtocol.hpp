#ifndef CAN_TP_ITRANSPORT_PROTOCOL_HPP
#define CAN_TP_ITRANSPORT_PROTOCOL_HPP

#include "ICommDriver.hpp"

#include <span>
#include <string>
#include <cstdint>
#include <cctype>
#include <string_view>
#include <algorithm>


/**
 * @brief Selects which transport protocol a plugin should use for payloads
 *        that do not fit into a single CAN / CAN-FD frame.
 *
 * NONE preserves today's behaviour exactly: every tout_write()/tout_read()
 * call maps 1:1 to a single physical frame, and payloads above the frame's
 * max DLC are simply rejected/truncated by the driver as they are now.
 */
enum class TpProtocol : uint8_t
{
    NONE                 = 0,  /**< Raw single-frame framing (current KVCAN/PCAN/SLCAN behaviour). */
    ISO_TP               = 1,  /**< ISO 15765-2 — automotive diagnostics / general purpose.         */
    J1939_TP             = 2,  /**< SAE J1939-21 — BAM (broadcast) and RTS/CTS (peer-to-peer).      */
    CANOPEN_SDO          = 3,  /**< CANopen SDO — expedited/segmented/block transfer (CiA 301).     */
    NMEA2000_FAST_PACKET = 4, /**< NMEA 2000 Fast Packet — single-source broadcast, no handshake.  */
};


/**
 * @file ITransportProtocol.hpp
 * @brief Driver-agnostic multi-frame transport-protocol interface.
 *
 * Every concrete protocol (IsoTpProtocol, J1939TpProtocol, ...) is written
 * exclusively against ICommDriver — the same interface KVCAN / PCAN / SLCAN
 * drivers already implement. That is the whole point of this library: it
 * never touches SocketCAN, a PCAN SDK, or a serial SLCAN adapter directly,
 * so one implementation works unmodified with every current and future
 * ICommDriver-based CAN backend.
 *
 * How a protocol uses ICommDriver:
 *   - Each driver::tout_write() call === "transmit exactly one physical
 *     frame" (this is already true today: KVCAN::tout_write() packs
 *     buffer into a single can_frame/canfd_frame).
 *   - Each driver::tout_read() call with ReadMode::Exact === "receive
 *     exactly one physical frame".
 *   - xtra_params is the per-call CAN-ID hint every driver already
 *     supports; protocols use it to address the frames they build
 *     (SF/FF/CF on txId, Flow-Control / TP.CM / TP.DT on the matching id).
 *
 * A protocol therefore needs no knowledge of sockets, handles or vendor
 * SDKs at all — it only ever calls driver.tout_write()/tout_read().
 */
class ITransportProtocol
{
    public:

        virtual ~ITransportProtocol() = default;

        /**
         * @brief Segment (if needed) and transmit @p data over @p driver.
         *
         * For payloads that fit in a single frame this degrades to exactly
         * one tout_write() call — functionally equivalent to the current
         * TpProtocol::NONE path, just wrapped in protocol framing (e.g. the
         * ISO-TP Single-Frame PCI byte).
         *
         * @param driver           Concrete ICommDriver to send frames over.
         * @param u32WriteTimeout  Overall operation deadline in milliseconds
         *                         (applies to the whole segmented transfer,
         *                         not to each individual frame).
         * @param data             Full message to transmit (may exceed one frame).
         * @param txId             CAN ID to stamp on frames we transmit
         *                         (SF / FF / CF, or Flow-Control replies when
         *                         acting as the receiving side of a handshake).
         * @param rxId             CAN ID to filter handshake frames coming back
         *                         from the peer (e.g. ISO-TP Flow Control,
         *                         J1939 CTS/EOM). May be empty for protocols /
         *                         payload sizes that need no peer handshake.
         * @return WriteResult; bytes_written == data.size() on full success.
         */
        virtual ICommDriver::WriteResult send(
            const ICommDriver& driver,
            uint32_t u32WriteTimeout,
            std::span<const uint8_t> data,
            std::string_view txId,
            std::string_view rxId = {}) const = 0;

        /**
         * @brief Receive and reassemble a (possibly multi-frame) message into @p buffer.
         *
         * @param driver          Concrete ICommDriver to receive frames from.
         * @param u32ReadTimeout  Overall operation deadline in milliseconds.
         * @param buffer          Destination for the reassembled message.
         *                        Status::BUFFER_OVERFLOW if the announced
         *                        message length exceeds buffer.size().
         * @param rxId            CAN ID identifying frames belonging to this
         *                        message (SF / FF / CF from the peer).
         * @param txId            CAN ID to stamp on any handshake frames we
         *                        send back to the peer (Flow Control, CTS...).
         *                        May be empty for protocols that never talk back.
         * @return ReadResult; bytes_read == reassembled message length on success.
         */
        virtual ICommDriver::ReadResult receive(
            const ICommDriver& driver,
            uint32_t u32ReadTimeout,
            std::span<uint8_t> buffer,
            std::string_view rxId,
            std::string_view txId = {}) const = 0;

        /** @brief Identifies which protocol this instance implements. */
        virtual TpProtocol id() const = 0;
};


/**
 * @brief Parse an INI/CONFIG string into a TpProtocol value.
 * Accepted (case-insensitive): "NONE", "ISOTP" / "ISO-TP" / "ISO_TP",
 * "J1939" / "J1939TP", "CANOPEN" / "CANOPENSDO", "NMEA2000" / "NMEA2000FP" / "FASTPACKET".
 * @return true on success; @p out is left untouched on failure.
 */
inline bool tp_protocol_from_string(std::string_view sv, TpProtocol& out)
{
    std::string s(sv);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    s.erase(std::remove(s.begin(), s.end(), '-'), s.end());
    s.erase(std::remove(s.begin(), s.end(), '_'), s.end());

    if (s.empty() || s == "NONE")       { out = TpProtocol::NONE;        return true; }
    if (s == "ISOTP")                   { out = TpProtocol::ISO_TP;      return true; }
    if (s == "J1939" || s == "J1939TP") { out = TpProtocol::J1939_TP;    return true; }
    if (s == "CANOPEN" || s == "CANOPENSDO") { out = TpProtocol::CANOPEN_SDO; return true; }
    if (s == "NMEA2000" || s == "NMEA2000FP" || s == "FASTPACKET") { out = TpProtocol::NMEA2000_FAST_PACKET; return true; }
    return false;
}

/**
 * @brief Render a TpProtocol back to its canonical string form (for logs / describeConnection()).
 */
inline std::string_view tp_protocol_to_string(TpProtocol proto)
{
    switch (proto)
    {
        case TpProtocol::NONE:                 return "NONE";
        case TpProtocol::ISO_TP:               return "ISOTP";
        case TpProtocol::J1939_TP:             return "J1939";
        case TpProtocol::CANOPEN_SDO:          return "CANOPEN";
        case TpProtocol::NMEA2000_FAST_PACKET: return "NMEA2000FP";
        default:                               return "UNKNOWN";
    }
}

#endif // CAN_TP_ITRANSPORT_PROTOCOL_HPP
