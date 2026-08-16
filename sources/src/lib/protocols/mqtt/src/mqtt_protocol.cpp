#include "mqtt_protocol.hpp"

uint16_t MqttProtocol::m_allocatePacketId()
{
    const uint16_t id = m_nextPacketId++;
    if (m_nextPacketId == 0) {
        m_nextPacketId = 1; // 0 is not a valid MQTT packet id
    }
    return id;
}

std::vector<uint8_t> MqttProtocol::encodeVarInt(uint32_t value)
{
    std::vector<uint8_t> bytes;
    do {
        uint8_t encoded = value % 128;
        value /= 128;
        if (value > 0) {
            encoded |= 0x80;
        }
        bytes.push_back(encoded);
    } while (value > 0);
    return bytes;
}

uint32_t MqttProtocol::decodeVarInt(const std::vector<uint8_t>& data, size_t& offset)
{
    uint32_t value = 0;
    int multiplier = 1;
    uint8_t digit;

    while (offset < data.size()) {
        digit = data[offset];
        value += (digit & 0x7F) * multiplier;
        multiplier *= 128;
        offset++;
        if ((digit & 0x80) == 0) {
            break;
        }
    }
    return value;
}

// -----------------------------------------------------------------------
// Builders
// -----------------------------------------------------------------------

std::vector<uint8_t> MqttProtocol::buildConnect(const ConnectParams& params) const
{
    const bool hasUser = !params.username.empty();
    const bool hasPass = hasUser && !params.password.empty();
    const bool hasWill = !params.willTopic.empty();

    uint8_t flags = 0;
    if (hasUser) flags |= 0x80;
    if (hasPass) flags |= 0x40;
    if (hasWill) {
        flags |= 0x04;
        flags |= static_cast<uint8_t>((params.willQos & 0x03) << 3);
        if (params.willRetain) flags |= 0x20;
    }
    if (params.cleanSession) flags |= 0x02;

    const std::string clientId = params.clientId.empty() ? "mqtt_client_" : params.clientId;

    static const std::string protocolName = "MQTT";
    std::vector<uint8_t> varHeader;
    varHeader.push_back(0);
    varHeader.push_back(static_cast<uint8_t>(protocolName.length()));
    varHeader.insert(varHeader.end(), protocolName.begin(), protocolName.end());
    varHeader.push_back(4); // MQTT Version 3.1.1
    varHeader.push_back(flags);
    varHeader.push_back(static_cast<uint8_t>((params.keepAlive >> 8) & 0xFF));
    varHeader.push_back(static_cast<uint8_t>(params.keepAlive & 0xFF));

    // Payload order is mandated by the spec: Client ID, Will Topic + Will
    // Message (if Will Flag), User Name (if flag), Password (if flag).
    std::vector<uint8_t> payload;
    payload.push_back(static_cast<uint8_t>((clientId.length() >> 8) & 0xFF));
    payload.push_back(static_cast<uint8_t>(clientId.length() & 0xFF));
    payload.insert(payload.end(), clientId.begin(), clientId.end());

    if (hasWill) {
        payload.push_back(static_cast<uint8_t>((params.willTopic.length() >> 8) & 0xFF));
        payload.push_back(static_cast<uint8_t>(params.willTopic.length() & 0xFF));
        payload.insert(payload.end(), params.willTopic.begin(), params.willTopic.end());

        payload.push_back(static_cast<uint8_t>((params.willPayload.length() >> 8) & 0xFF));
        payload.push_back(static_cast<uint8_t>(params.willPayload.length() & 0xFF));
        payload.insert(payload.end(), params.willPayload.begin(), params.willPayload.end());
    }
    if (hasUser) {
        payload.push_back(static_cast<uint8_t>((params.username.length() >> 8) & 0xFF));
        payload.push_back(static_cast<uint8_t>(params.username.length() & 0xFF));
        payload.insert(payload.end(), params.username.begin(), params.username.end());
    }
    if (hasPass) {
        payload.push_back(static_cast<uint8_t>((params.password.length() >> 8) & 0xFF));
        payload.push_back(static_cast<uint8_t>(params.password.length() & 0xFF));
        payload.insert(payload.end(), params.password.begin(), params.password.end());
    }

    const size_t remainingLen = varHeader.size() + payload.size();
    std::vector<uint8_t> remLenBytes = encodeVarInt(remainingLen);

    std::vector<uint8_t> packet;
    packet.reserve(1 + remLenBytes.size() + remainingLen);
    packet.push_back(kConnect);
    packet.insert(packet.end(), remLenBytes.begin(), remLenBytes.end());
    packet.insert(packet.end(), varHeader.begin(), varHeader.end());
    packet.insert(packet.end(), payload.begin(), payload.end());
    return packet;
}

std::vector<uint8_t> MqttProtocol::buildDisconnect() const
{
    return { kDisconnect, 0x00 };
}

std::vector<uint8_t> MqttProtocol::buildPingReq() const
{
    return { kPingReq, 0x00 };
}

std::vector<uint8_t> MqttProtocol::buildPublish(const std::string& topic, const std::string& payload,
                                                 uint8_t qos, bool retain, uint16_t* pOutPacketId)
{
    qos &= 0x03;

    std::vector<uint8_t> varAndPayload;
    varAndPayload.push_back(static_cast<uint8_t>((topic.length() >> 8) & 0xFF));
    varAndPayload.push_back(static_cast<uint8_t>(topic.length() & 0xFF));
    varAndPayload.insert(varAndPayload.end(), topic.begin(), topic.end());

    uint16_t packetId = 0;
    if (qos > 0) {
        packetId = m_allocatePacketId();
        varAndPayload.push_back(static_cast<uint8_t>((packetId >> 8) & 0xFF));
        varAndPayload.push_back(static_cast<uint8_t>(packetId & 0xFF));
    }

    varAndPayload.insert(varAndPayload.end(), payload.begin(), payload.end());

    std::vector<uint8_t> remLenBytes = encodeVarInt(varAndPayload.size());
    std::vector<uint8_t> packet;
    packet.reserve(1 + remLenBytes.size() + varAndPayload.size());
    packet.push_back(static_cast<uint8_t>(kPublish | (qos << 1) | (retain ? 0x01 : 0x00)));
    packet.insert(packet.end(), remLenBytes.begin(), remLenBytes.end());
    packet.insert(packet.end(), varAndPayload.begin(), varAndPayload.end());

    if (pOutPacketId) {
        *pOutPacketId = packetId;
    }
    return packet;
}

std::vector<uint8_t> MqttProtocol::buildPubAck(uint16_t packetId) const
{
    return { kPubAck, 0x02, static_cast<uint8_t>((packetId >> 8) & 0xFF), static_cast<uint8_t>(packetId & 0xFF) };
}

std::vector<uint8_t> MqttProtocol::buildPubRec(uint16_t packetId) const
{
    return { kPubRec, 0x02, static_cast<uint8_t>((packetId >> 8) & 0xFF), static_cast<uint8_t>(packetId & 0xFF) };
}

std::vector<uint8_t> MqttProtocol::buildPubRel(uint16_t packetId) const
{
    return { kPubRel, 0x02, static_cast<uint8_t>((packetId >> 8) & 0xFF), static_cast<uint8_t>(packetId & 0xFF) };
}

std::vector<uint8_t> MqttProtocol::buildPubComp(uint16_t packetId) const
{
    return { kPubComp, 0x02, static_cast<uint8_t>((packetId >> 8) & 0xFF), static_cast<uint8_t>(packetId & 0xFF) };
}

std::vector<uint8_t> MqttProtocol::buildSubscribe(const std::string& topic, uint8_t qos, uint16_t* pOutPacketId)
{
    const uint16_t packetId = m_allocatePacketId();

    std::vector<uint8_t> varAndPayload;
    varAndPayload.push_back(static_cast<uint8_t>((packetId >> 8) & 0xFF));
    varAndPayload.push_back(static_cast<uint8_t>(packetId & 0xFF));
    varAndPayload.push_back(static_cast<uint8_t>((topic.length() >> 8) & 0xFF));
    varAndPayload.push_back(static_cast<uint8_t>(topic.length() & 0xFF));
    varAndPayload.insert(varAndPayload.end(), topic.begin(), topic.end());
    varAndPayload.push_back(qos & 0x03);

    std::vector<uint8_t> remLenBytes = encodeVarInt(varAndPayload.size());
    std::vector<uint8_t> packet;
    packet.push_back(kSubscribe);
    packet.insert(packet.end(), remLenBytes.begin(), remLenBytes.end());
    packet.insert(packet.end(), varAndPayload.begin(), varAndPayload.end());

    if (pOutPacketId) {
        *pOutPacketId = packetId;
    }
    return packet;
}

std::vector<uint8_t> MqttProtocol::buildUnsubscribe(const std::string& topic, uint16_t* pOutPacketId)
{
    const uint16_t packetId = m_allocatePacketId();

    std::vector<uint8_t> varAndPayload;
    varAndPayload.push_back(static_cast<uint8_t>((packetId >> 8) & 0xFF));
    varAndPayload.push_back(static_cast<uint8_t>(packetId & 0xFF));
    varAndPayload.push_back(static_cast<uint8_t>((topic.length() >> 8) & 0xFF));
    varAndPayload.push_back(static_cast<uint8_t>(topic.length() & 0xFF));
    varAndPayload.insert(varAndPayload.end(), topic.begin(), topic.end());

    std::vector<uint8_t> remLenBytes = encodeVarInt(varAndPayload.size());
    std::vector<uint8_t> packet;
    packet.push_back(kUnsubscribe);
    packet.insert(packet.end(), remLenBytes.begin(), remLenBytes.end());
    packet.insert(packet.end(), varAndPayload.begin(), varAndPayload.end());

    if (pOutPacketId) {
        *pOutPacketId = packetId;
    }
    return packet;
}

// -----------------------------------------------------------------------
// Decoders
// -----------------------------------------------------------------------

MqttProtocol::ConnAckResult MqttProtocol::decodeConnAck(const std::vector<uint8_t>& packet) const
{
    ConnAckResult result;
    if (packetType(packet) != kConnAck) {
        return result; // returnCode stays 0xFF ("not actually a CONNACK")
    }

    size_t offset = 1;
    decodeVarInt(packet, offset); // skip Remaining Length
    if (offset + 1 >= packet.size()) {
        return result;
    }
    result.sessionPresent = (packet[offset] != 0);
    result.returnCode     = packet[offset + 1];
    return result;
}

MqttProtocol::SubAckResult MqttProtocol::decodeSubAck(const std::vector<uint8_t>& packet) const
{
    SubAckResult result;
    if (packetType(packet) != kSubAck) {
        return result;
    }

    size_t offset = 1;
    decodeVarInt(packet, offset); // skip Remaining Length
    if (offset + 2 >= packet.size()) {
        return result;
    }
    result.packetId   = static_cast<uint16_t>((packet[offset] << 8) | packet[offset + 1]);
    result.returnCode = packet[offset + 2];
    return result;
}

bool MqttProtocol::decodeSimpleAck(const std::vector<uint8_t>& packet, uint16_t* pOutPacketId)
{
    size_t offset = 1;
    decodeVarInt(packet, offset); // skip Remaining Length
    if (offset + 1 >= packet.size()) {
        return false;
    }
    if (pOutPacketId) {
        *pOutPacketId = static_cast<uint16_t>((packet[offset] << 8) | packet[offset + 1]);
    }
    return true;
}

MqttProtocol::PublishMessage MqttProtocol::decodePublish(const std::vector<uint8_t>& packet) const
{
    PublishMessage msg;
    if (!isPublish(packet)) {
        return msg;
    }

    const uint8_t header = packet[0];
    msg.dup    = (header & 0x08) != 0;
    msg.qos    = (header >> 1) & 0x03;
    msg.retain = (header & 0x01) != 0;

    size_t offset = 1;
    decodeVarInt(packet, offset); // skip Remaining Length

    if (offset + 2 > packet.size()) {
        return msg; // malformed — caller sees an empty topic and can treat that as an error
    }
    const uint16_t topicLen = static_cast<uint16_t>((packet[offset] << 8) | packet[offset + 1]);
    offset += 2;
    if (offset + topicLen > packet.size()) {
        return msg;
    }
    msg.topic.assign(reinterpret_cast<const char*>(packet.data() + offset), topicLen);
    offset += topicLen;

    if (msg.qos > 0) {
        if (offset + 2 > packet.size()) {
            return msg;
        }
        msg.packetId = static_cast<uint16_t>((packet[offset] << 8) | packet[offset + 1]);
        offset += 2;
    }

    // Everything left is the payload — PUBLISH has no length prefix of its
    // own for it; it's simply "whatever remains".
    msg.payload.assign(reinterpret_cast<const char*>(packet.data() + offset), packet.size() - offset);
    return msg;
}
