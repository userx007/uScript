#include "mqtt_protocol.hpp"

uint16_t MqttProtocol::m_allocatePacketId()
{
    const uint16_t id = m_nextPacketId++;
    if (m_nextPacketId == 0) {
        m_nextPacketId = 1; // 0 is not a valid MQTT packet id
    }
    return id;
}

std::vector<uint8_t> MqttProtocol::encodeVarInt(uint32_t u32Value)
{
    std::vector<uint8_t> bytes;
    do {
        uint8_t encoded = u32Value % 128;
        u32Value /= 128;
        if (u32Value > 0) {
            encoded |= 0x80;
        }
        bytes.push_back(encoded);
    } while (u32Value > 0);
    return bytes;
}

uint32_t MqttProtocol::decodeVarInt(const std::vector<uint8_t> &vData, size_t &offset)
{
    uint32_t value = 0;
    int multiplier = 1;
    uint8_t digit;

    while (offset < vData.size()) {
        digit = vData[offset];
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

std::vector<uint8_t> MqttProtocol::buildConnect(const ConnectParams &sParams) const
{
    const bool hasUser = !sParams.username.empty();
    const bool hasPass = hasUser && !sParams.password.empty();
    const bool hasWill = !sParams.willTopic.empty();

    uint8_t flags      = 0;
    if (hasUser) {
        flags |= 0x80;
    }
    if (hasPass) {
        flags |= 0x40;
    }
    if (hasWill) {
        flags |= 0x04;
        flags |= static_cast<uint8_t>((sParams.willQos & 0x03) << 3);
        if (sParams.willRetain) {
            flags |= 0x20;
        }
    }
    if (sParams.cleanSession) {
        flags |= 0x02;
    }

    const std::string clientId            = sParams.clientId.empty() ? "mqtt_client_" : sParams.clientId;

    static const std::string protocolName = "MQTT";
    std::vector<uint8_t> varHeader;
    varHeader.push_back(0);
    varHeader.push_back(static_cast<uint8_t>(protocolName.length()));
    varHeader.insert(varHeader.end(), protocolName.begin(), protocolName.end());
    varHeader.push_back(4); // MQTT Version 3.1.1
    varHeader.push_back(flags);
    varHeader.push_back(static_cast<uint8_t>((sParams.keepAlive >> 8) & 0xFF));
    varHeader.push_back(static_cast<uint8_t>(sParams.keepAlive & 0xFF));

    // Payload order is mandated by the spec: Client ID, Will Topic + Will
    // Message (if Will Flag), User Name (if flag), Password (if flag).
    std::vector<uint8_t> payload;
    payload.push_back(static_cast<uint8_t>((clientId.length() >> 8) & 0xFF));
    payload.push_back(static_cast<uint8_t>(clientId.length() & 0xFF));
    payload.insert(payload.end(), clientId.begin(), clientId.end());

    if (hasWill) {
        payload.push_back(static_cast<uint8_t>((sParams.willTopic.length() >> 8) & 0xFF));
        payload.push_back(static_cast<uint8_t>(sParams.willTopic.length() & 0xFF));
        payload.insert(payload.end(), sParams.willTopic.begin(), sParams.willTopic.end());

        payload.push_back(static_cast<uint8_t>((sParams.willPayload.length() >> 8) & 0xFF));
        payload.push_back(static_cast<uint8_t>(sParams.willPayload.length() & 0xFF));
        payload.insert(payload.end(), sParams.willPayload.begin(), sParams.willPayload.end());
    }
    if (hasUser) {
        payload.push_back(static_cast<uint8_t>((sParams.username.length() >> 8) & 0xFF));
        payload.push_back(static_cast<uint8_t>(sParams.username.length() & 0xFF));
        payload.insert(payload.end(), sParams.username.begin(), sParams.username.end());
    }
    if (hasPass) {
        payload.push_back(static_cast<uint8_t>((sParams.password.length() >> 8) & 0xFF));
        payload.push_back(static_cast<uint8_t>(sParams.password.length() & 0xFF));
        payload.insert(payload.end(), sParams.password.begin(), sParams.password.end());
    }

    const size_t remainingLen        = varHeader.size() + payload.size();
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
    return {kDisconnect, 0x00};
}

std::vector<uint8_t> MqttProtocol::buildPingReq() const
{
    return {kPingReq, 0x00};
}

std::vector<uint8_t> MqttProtocol::buildPublish(const std::string &strTopic, const std::string &strPayload,
                                                uint8_t u8Qos, bool bRetain, uint16_t *pu16OutPacketId)
{
    u8Qos &= 0x03;

    std::vector<uint8_t> varAndPayload;
    varAndPayload.push_back(static_cast<uint8_t>((strTopic.length() >> 8) & 0xFF));
    varAndPayload.push_back(static_cast<uint8_t>(strTopic.length() & 0xFF));
    varAndPayload.insert(varAndPayload.end(), strTopic.begin(), strTopic.end());

    uint16_t packetId = 0;
    if (u8Qos > 0) {
        packetId = m_allocatePacketId();
        varAndPayload.push_back(static_cast<uint8_t>((packetId >> 8) & 0xFF));
        varAndPayload.push_back(static_cast<uint8_t>(packetId & 0xFF));
    }

    varAndPayload.insert(varAndPayload.end(), strPayload.begin(), strPayload.end());

    std::vector<uint8_t> remLenBytes = encodeVarInt(varAndPayload.size());
    std::vector<uint8_t> packet;
    packet.reserve(1 + remLenBytes.size() + varAndPayload.size());
    packet.push_back(static_cast<uint8_t>(kPublish | (u8Qos << 1) | (bRetain ? 0x01 : 0x00)));
    packet.insert(packet.end(), remLenBytes.begin(), remLenBytes.end());
    packet.insert(packet.end(), varAndPayload.begin(), varAndPayload.end());

    if (pu16OutPacketId) {
        *pu16OutPacketId = packetId;
    }
    return packet;
}

std::vector<uint8_t> MqttProtocol::buildPubAck(uint16_t u16PacketId) const
{
    return {kPubAck, 0x02, static_cast<uint8_t>((u16PacketId >> 8) & 0xFF), static_cast<uint8_t>(u16PacketId & 0xFF)};
}

std::vector<uint8_t> MqttProtocol::buildPubRec(uint16_t u16PacketId) const
{
    return {kPubRec, 0x02, static_cast<uint8_t>((u16PacketId >> 8) & 0xFF), static_cast<uint8_t>(u16PacketId & 0xFF)};
}

std::vector<uint8_t> MqttProtocol::buildPubRel(uint16_t u16PacketId) const
{
    return {kPubRel, 0x02, static_cast<uint8_t>((u16PacketId >> 8) & 0xFF), static_cast<uint8_t>(u16PacketId & 0xFF)};
}

std::vector<uint8_t> MqttProtocol::buildPubComp(uint16_t u16PacketId) const
{
    return {kPubComp, 0x02, static_cast<uint8_t>((u16PacketId >> 8) & 0xFF), static_cast<uint8_t>(u16PacketId & 0xFF)};
}

std::vector<uint8_t> MqttProtocol::buildSubscribe(const std::string &strTopic, uint8_t u8Qos, uint16_t *pu16OutPacketId)
{
    const uint16_t packetId = m_allocatePacketId();

    std::vector<uint8_t> varAndPayload;
    varAndPayload.push_back(static_cast<uint8_t>((packetId >> 8) & 0xFF));
    varAndPayload.push_back(static_cast<uint8_t>(packetId & 0xFF));
    varAndPayload.push_back(static_cast<uint8_t>((strTopic.length() >> 8) & 0xFF));
    varAndPayload.push_back(static_cast<uint8_t>(strTopic.length() & 0xFF));
    varAndPayload.insert(varAndPayload.end(), strTopic.begin(), strTopic.end());
    varAndPayload.push_back(u8Qos & 0x03);

    std::vector<uint8_t> remLenBytes = encodeVarInt(varAndPayload.size());
    std::vector<uint8_t> packet;
    packet.push_back(kSubscribe);
    packet.insert(packet.end(), remLenBytes.begin(), remLenBytes.end());
    packet.insert(packet.end(), varAndPayload.begin(), varAndPayload.end());

    if (pu16OutPacketId) {
        *pu16OutPacketId = packetId;
    }
    return packet;
}

std::vector<uint8_t> MqttProtocol::buildUnsubscribe(const std::string &strTopic, uint16_t *pu16OutPacketId)
{
    const uint16_t packetId = m_allocatePacketId();

    std::vector<uint8_t> varAndPayload;
    varAndPayload.push_back(static_cast<uint8_t>((packetId >> 8) & 0xFF));
    varAndPayload.push_back(static_cast<uint8_t>(packetId & 0xFF));
    varAndPayload.push_back(static_cast<uint8_t>((strTopic.length() >> 8) & 0xFF));
    varAndPayload.push_back(static_cast<uint8_t>(strTopic.length() & 0xFF));
    varAndPayload.insert(varAndPayload.end(), strTopic.begin(), strTopic.end());

    std::vector<uint8_t> remLenBytes = encodeVarInt(varAndPayload.size());
    std::vector<uint8_t> packet;
    packet.push_back(kUnsubscribe);
    packet.insert(packet.end(), remLenBytes.begin(), remLenBytes.end());
    packet.insert(packet.end(), varAndPayload.begin(), varAndPayload.end());

    if (pu16OutPacketId) {
        *pu16OutPacketId = packetId;
    }
    return packet;
}

// -----------------------------------------------------------------------
// Decoders
// -----------------------------------------------------------------------

MqttProtocol::ConnAckResult MqttProtocol::decodeConnAck(const std::vector<uint8_t> &vPacket) const
{
    ConnAckResult result;
    if (packetType(vPacket) != kConnAck) {
        return result; // returnCode stays 0xFF ("not actually a CONNACK")
    }

    size_t offset = 1;
    decodeVarInt(vPacket, offset); // skip Remaining Length
    if (offset + 1 >= vPacket.size()) {
        return result;
    }
    result.sessionPresent = (vPacket[offset] != 0);
    result.returnCode     = vPacket[offset + 1];
    return result;
}

MqttProtocol::SubAckResult MqttProtocol::decodeSubAck(const std::vector<uint8_t> &vPacket) const
{
    SubAckResult result;
    if (packetType(vPacket) != kSubAck) {
        return result;
    }

    size_t offset = 1;
    decodeVarInt(vPacket, offset); // skip Remaining Length
    if (offset + 2 >= vPacket.size()) {
        return result;
    }
    result.packetId   = static_cast<uint16_t>((vPacket[offset] << 8) | vPacket[offset + 1]);
    result.returnCode = vPacket[offset + 2];
    return result;
}

bool MqttProtocol::decodeSimpleAck(const std::vector<uint8_t> &vPacket, uint16_t *pu16OutPacketId)
{
    size_t offset = 1;
    decodeVarInt(vPacket, offset); // skip Remaining Length
    if (offset + 1 >= vPacket.size()) {
        return false;
    }
    if (pu16OutPacketId) {
        *pu16OutPacketId = static_cast<uint16_t>((vPacket[offset] << 8) | vPacket[offset + 1]);
    }
    return true;
}

MqttProtocol::PublishMessage MqttProtocol::decodePublish(const std::vector<uint8_t> &vPacket) const
{
    PublishMessage msg;
    if (!isPublish(vPacket)) {
        return msg;
    }

    const uint8_t header = vPacket[0];
    msg.dup              = (header & 0x08) != 0;
    msg.qos              = (header >> 1) & 0x03;
    msg.retain           = (header & 0x01) != 0;

    size_t offset        = 1;
    decodeVarInt(vPacket, offset); // skip Remaining Length

    if (offset + 2 > vPacket.size()) {
        return msg; // malformed — caller sees an empty topic and can treat that as an error
    }
    const uint16_t topicLen = static_cast<uint16_t>((vPacket[offset] << 8) | vPacket[offset + 1]);
    offset += 2;
    if (offset + topicLen > vPacket.size()) {
        return msg;
    }
    msg.topic.assign(reinterpret_cast<const char *>(vPacket.data() + offset), topicLen);
    offset += topicLen;

    if (msg.qos > 0) {
        if (offset + 2 > vPacket.size()) {
            return msg;
        }
        msg.packetId = static_cast<uint16_t>((vPacket[offset] << 8) | vPacket[offset + 1]);
        offset += 2;
    }

    // Everything left is the payload — PUBLISH has no length prefix of its
    // own for it; it's simply "whatever remains".
    msg.payload.assign(reinterpret_cast<const char *>(vPacket.data() + offset), vPacket.size() - offset);
    return msg;
}
