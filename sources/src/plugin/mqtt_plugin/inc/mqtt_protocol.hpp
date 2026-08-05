#ifndef MQTT_PROTOCOL_HPP
#define MQTT_PROTOCOL_HPP

#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief Pure MQTT v3.1.1 protocol codec.
 *
 * This is the "protocol side" of the plugin's three-way split (see
 * mqtt_plugin.hpp's class doc comment for the other two): it only ever
 * builds and parses MQTT control packets as plain byte buffers. It never
 * touches a socket, a TLS context, a timeout, or anything else transport-
 * related — MqttTransport owns all of that. The only state this class
 * carries is the packet-id sequence a session needs (assigned by build*()
 * for QoS 1/2 PUBLISH/SUBSCRIBE/UNSUBSCRIBE), which is protocol-level
 * bookkeeping, not transport state.
 *
 * MqttPlugin is what wires the two together: it asks this class to build a
 * packet, hands the resulting bytes to MqttTransport::send(), later reads
 * raw bytes back via MqttTransport::recv() into a complete packet buffer
 * (MqttPlugin::m_readPacket() owns that read loop — see its doc comment for
 * why that loop itself isn't part of this class), and asks this class to
 * decode that buffer.
 *
 * Every build*() returns a complete, ready-to-send packet (fixed header +
 * remaining length + variable header + payload). Every decode*() assumes
 * it's given exactly one complete, already-received packet — none of these
 * functions read a stream or ask "is there more coming"; that framing
 * question belongs to whoever is doing the actual reading (MqttPlugin).
 */
class MqttProtocol
{
public:
    // Fixed-header first byte for each control packet type. PUBLISH's low
    // nibble additionally carries DUP/QoS/RETAIN and so varies per message —
    // isPublish() below masks that off before comparing.
    static constexpr uint8_t kConnect     = 0x10;
    static constexpr uint8_t kConnAck     = 0x20;
    static constexpr uint8_t kPublish     = 0x30;
    static constexpr uint8_t kPubAck      = 0x40;
    static constexpr uint8_t kPubRec      = 0x50;
    static constexpr uint8_t kPubRel      = 0x62; // one of two packet types with mandatory reserved flag bits (0010) set
    static constexpr uint8_t kPubComp     = 0x70;
    static constexpr uint8_t kSubscribe   = 0x82; // the other one
    static constexpr uint8_t kSubAck      = 0x90;
    static constexpr uint8_t kUnsubscribe = 0xA2; // same mandatory reserved bits as SUBSCRIBE/PUBREL
    static constexpr uint8_t kUnsubAck    = 0xB0;
    static constexpr uint8_t kPingReq     = 0xC0;
    static constexpr uint8_t kPingResp    = 0xD0;
    static constexpr uint8_t kDisconnect  = 0xE0;

    struct ConnectParams {
        std::string clientId;
        std::string username;   // empty => CONNECT carries no credentials at all
        std::string password;   // ignored if username is empty (not a valid MQTT 3.1.1 combination)
        std::string willTopic;  // empty => no Will Flag set; willPayload/willQos/willRetain then unused
        std::string willPayload;
        uint8_t willQos = 0;
        bool willRetain = false;
        bool cleanSession = true;
        uint16_t keepAlive = 60; // seconds
    };

    struct ConnAckResult {
        bool sessionPresent = false;
        uint8_t returnCode = 0xFF; // 0 = accepted; see MQTT 3.1.1 §3.2.2.3 for the other values
        bool ok() const { return returnCode == 0; }
    };

    struct SubAckResult {
        uint16_t packetId = 0;
        uint8_t returnCode = 0x80; // granted QoS (0-2), or 0x80 = subscription refused
        bool ok() const { return returnCode != 0x80; }
    };

    struct PublishMessage {
        std::string topic;
        std::string payload;
        uint8_t qos = 0;
        bool retain = false;
        bool dup = false;
        uint16_t packetId = 0; // only meaningful (and only present on the wire) for qos > 0
    };

    MqttProtocol() = default;

    // ---- Builders: pure encode, no I/O, no validation beyond what's noted ----
    // (Callers — MqttPlugin — are expected to have already validated topic/
    // argument shape before calling; these assume well-formed input.)

    std::vector<uint8_t> buildConnect(const ConnectParams& params) const;
    std::vector<uint8_t> buildDisconnect() const;
    std::vector<uint8_t> buildPingReq() const;

    // Assigns a fresh packet id for qos > 0 (written to *pOutPacketId; left
    // at 0, matching "no packet id" for qos == 0, when pOutPacketId is
    // non-null but qos == 0).
    std::vector<uint8_t> buildPublish(const std::string& topic, const std::string& payload,
                                       uint8_t qos, bool retain, uint16_t* pOutPacketId);

    // Subscriber-side acknowledgements MqttPlugin sends back for an
    // incoming PUBLISH it just received (mirror image of the wait-for-ack
    // side below): PUBACK for QoS 1, PUBREC/PUBCOMP bracketing the
    // broker's own PUBREL for QoS 2.
    std::vector<uint8_t> buildPubAck(uint16_t packetId) const;
    std::vector<uint8_t> buildPubRec(uint16_t packetId) const;
    std::vector<uint8_t> buildPubRel(uint16_t packetId) const;
    std::vector<uint8_t> buildPubComp(uint16_t packetId) const;

    std::vector<uint8_t> buildSubscribe(const std::string& topic, uint8_t qos, uint16_t* pOutPacketId);
    std::vector<uint8_t> buildUnsubscribe(const std::string& topic, uint16_t* pOutPacketId);

    // ---- Decoders: pure decode of one already-complete raw packet ----

    static uint8_t packetType(const std::vector<uint8_t>& packet) { return packet.empty() ? 0 : packet[0]; }
    static bool isPublish(const std::vector<uint8_t>& packet) { return (packetType(packet) & 0xF0) == kPublish; }

    ConnAckResult decodeConnAck(const std::vector<uint8_t>& packet) const;
    SubAckResult  decodeSubAck(const std::vector<uint8_t>& packet) const;

    // PUBACK / PUBREC / PUBREL / PUBCOMP / UNSUBACK all share one shape —
    // fixed header + Remaining Length(2) + Packet Identifier, nothing else
    // — so one decoder covers all five. Returns false if the packet is too
    // short to contain a Packet Identifier.
    static bool decodeSimpleAck(const std::vector<uint8_t>& packet, uint16_t* pOutPacketId);

    PublishMessage decodePublish(const std::vector<uint8_t>& packet) const;

    // ---- Variable Byte Integer helpers ----
    // Used both internally (Remaining Length on every packet this class
    // builds/decodes) and by MqttPlugin's read loop, which needs
    // encodeVarInt() for nothing itself but decodeVarInt()'s exact
    // continuation-bit semantics mirrored byte-by-byte while it reads a
    // packet's Remaining Length field off the wire — see
    // MqttPlugin::m_readPacket().
    static std::vector<uint8_t> encodeVarInt(uint32_t value);
    // Assumes 'data' already contains a complete, well-formed Variable Byte
    // Integer starting at 'offset' (true for anything this class is asked
    // to decode, since MqttPlugin's read loop only ever hands over packets
    // it has already fully received) — advances 'offset' past it and
    // returns the value.
    static uint32_t decodeVarInt(const std::vector<uint8_t>& data, size_t& offset);

    void resetPacketIdSequence() { m_nextPacketId = 1; }

private:
    // Packet ids: shared across PUBLISH (QoS>0)/SUBSCRIBE/UNSUBSCRIBE, per
    // the MQTT spec (they all draw from the same per-session id space).
    // 0 is never a valid MQTT packet id, so the sequence wraps past it.
    uint16_t m_nextPacketId = 1;
    uint16_t m_allocatePacketId();
};

#endif // MQTT_PROTOCOL_HPP
