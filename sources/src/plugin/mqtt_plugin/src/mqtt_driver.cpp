#include "mqtt_driver.hpp"
#include "uLogger.hpp"
#include <cstring>
#include <algorithm>
#include <chrono>

#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LOG_HDR "MQTT_DRV    |"

// OpenSSL Includes
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>

// POSIX poll(), used only by the TLS path of m_recvRaw()/m_sendRaw() to
// bound SSL_read()/SSL_write() the same way TCPIP::timeout_read()/
// timeout_write() bound plain recv()/send() — see those two helpers below.
#include <poll.h>

MqttDriver::~MqttDriver()
{
    close();
}

ICommDriver::Status MqttDriver::open(const Config& config)
{
    if (m_pTcpip) {
        close(); // Clean up existing
    }

    m_config = config;
    m_connected = false;
    m_sessionOpen = false;
    m_nextPacketId = 1;
    m_sslCtx = nullptr;
    m_ssl = nullptr;
    m_lastActivity = std::chrono::steady_clock::now();

    // 1. Open TCP Socket
    m_pTcpip = std::make_shared<TCPIP>();
    ICommDriver::Status tcpStatus = m_pTcpip->open(
        m_config.host,
        m_config.port,
        m_config.connectTimeoutMs
    );

    if (tcpStatus != ICommDriver::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("TCPIP Open failed"));
        return ICommDriver::Status::PORT_ACCESS;
    }

    if (m_config.useTls) {
        if (ICommDriver::Status::SUCCESS != setupTls()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("TLS Setup failed"));
            return ICommDriver::Status::OPERATION_FAILED;
        }
    }

    m_connected = true;
    return ICommDriver::Status::SUCCESS;
}

void MqttDriver::close()
{
    if (m_ssl) {
        SSL_shutdown(m_ssl);
        SSL_free(m_ssl); // also detaches from the fd; TCPIP still owns the fd itself
        m_ssl = nullptr;
    }
    if (m_sslCtx) {
        SSL_CTX_free(m_sslCtx);
        m_sslCtx = nullptr;
    }

    if (m_pTcpip) {
        m_pTcpip->close();
        m_pTcpip.reset();
    }

    m_connected = false;
    m_sessionOpen = false;
}

ICommDriver::Status MqttDriver::connect()
{
    if (!m_connected) return ICommDriver::Status::PORT_ACCESS;

    if (!m_config.username.empty() && m_config.password.empty()) {
        // Not a hard MQTT violation (a username with an empty password is
        // legal), but a password without a username is not — call it out
        // rather than silently sending something the broker may reject.
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("connect: username set with empty password"));
    }
    if (m_config.username.empty() && !m_config.password.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("connect: password set without a username — ignoring password (MQTT 3.1.1 requires User Name Flag for Password Flag)"));
    }
    const bool hasUser = !m_config.username.empty();
    const bool hasPass = hasUser && !m_config.password.empty();
    const bool hasWill = !m_config.willTopic.empty();

    if (m_config.willQos > 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("connect: willQos must be 0-2"));
        return ICommDriver::Status::INVALID_PARAM;
    }

    // 1. Construct CONNECT Packet

    // Connect Flags — built up from what's actually configured, rather than
    // a hardcoded value: User Name / Password / Will (+ Will QoS/Retain) /
    // Clean Session, each only set when the corresponding Config field asks
    // for it. (A previous version of this code hardcoded 0xC2, which claimed
    // User Name + Password were present in the payload while never actually
    // writing those fields — a malformed-packet bug fixed by this rewrite.)
    uint8_t flags = 0;
    if (hasUser) flags |= 0x80;
    if (hasPass) flags |= 0x40;
    if (hasWill) {
        flags |= 0x04;
        flags |= static_cast<uint8_t>((m_config.willQos & 0x03) << 3);
        if (m_config.willRetain) flags |= 0x20;
    }
    if (m_config.cleanSession) flags |= 0x02;

    // Client ID (payload's first field, always present — used below for
    // both the client-id length used in Remaining Length and the payload
    // bytes themselves).
    const std::string clientId = m_config.clientId.empty() ? "mqtt_client_" : m_config.clientId;

    // Variable Header: Protocol Name + Version + Flags + Keep Alive
    static const std::string protocolName = "MQTT";
    std::vector<uint8_t> varHeader;
    varHeader.push_back(0);
    varHeader.push_back(static_cast<uint8_t>(protocolName.length()));
    varHeader.insert(varHeader.end(), protocolName.begin(), protocolName.end());
    varHeader.push_back(4); // MQTT Version 3.1.1
    varHeader.push_back(flags);
    varHeader.push_back(static_cast<uint8_t>((m_config.keepAlive >> 8) & 0xFF));
    varHeader.push_back(static_cast<uint8_t>(m_config.keepAlive & 0xFF));

    // Payload: Client ID, then (if Will Flag) Will Topic + Will Message,
    // then (if User Name Flag) User Name, then (if Password Flag) Password
    // — this exact order is mandated by the MQTT 3.1.1 spec.
    std::vector<uint8_t> payload;
    payload.push_back(static_cast<uint8_t>((clientId.length() >> 8) & 0xFF));
    payload.push_back(static_cast<uint8_t>(clientId.length() & 0xFF));
    payload.insert(payload.end(), clientId.begin(), clientId.end());

    if (hasWill) {
        payload.push_back(static_cast<uint8_t>((m_config.willTopic.length() >> 8) & 0xFF));
        payload.push_back(static_cast<uint8_t>(m_config.willTopic.length() & 0xFF));
        payload.insert(payload.end(), m_config.willTopic.begin(), m_config.willTopic.end());

        payload.push_back(static_cast<uint8_t>((m_config.willPayload.length() >> 8) & 0xFF));
        payload.push_back(static_cast<uint8_t>(m_config.willPayload.length() & 0xFF));
        payload.insert(payload.end(), m_config.willPayload.begin(), m_config.willPayload.end());
    }
    if (hasUser) {
        payload.push_back(static_cast<uint8_t>((m_config.username.length() >> 8) & 0xFF));
        payload.push_back(static_cast<uint8_t>(m_config.username.length() & 0xFF));
        payload.insert(payload.end(), m_config.username.begin(), m_config.username.end());
    }
    if (hasPass) {
        payload.push_back(static_cast<uint8_t>((m_config.password.length() >> 8) & 0xFF));
        payload.push_back(static_cast<uint8_t>(m_config.password.length() & 0xFF));
        payload.insert(payload.end(), m_config.password.begin(), m_config.password.end());
    }

    const size_t remainingLen = varHeader.size() + payload.size();
    std::vector<uint8_t> remainingLenBytes = encodeVarInt(remainingLen);

    std::vector<uint8_t> packet;
    packet.reserve(1 + remainingLenBytes.size() + remainingLen);
    packet.push_back(0x10); // Fixed Header: CONNECT
    packet.insert(packet.end(), remainingLenBytes.begin(), remainingLenBytes.end());
    packet.insert(packet.end(), varHeader.begin(), varHeader.end());
    packet.insert(packet.end(), payload.begin(), payload.end());

    // Send
    auto sendSt = m_sendRaw(5000, std::span<const uint8_t>(packet.data(), packet.size()));
    if (sendSt != ICommDriver::Status::SUCCESS) {
        return ICommDriver::Status::WRITE_ERROR;
    }

    // 2. Receive CONNACK
    std::vector<uint8_t> ackPacket;
    auto res = recvPacket(ackPacket, 5000);
    if (res != ICommDriver::Status::SUCCESS) return res;

    if (ackPacket.empty() || ackPacket[0] != 0x20) { // CONNACK
        return ICommDriver::Status::PROTOCOL_ERROR;
    }

    // Session-present + return-code sit right after CONNACK's own Remaining
    // Length field — decode that (rather than reusing remainingLenBytes.size(),
    // which is the *CONNECT* packet's own length prefix and has nothing to do
    // with how many bytes CONNACK's length prefix took).
    size_t offset = 1;
    decodeVarInt(ackPacket, offset); // advances offset past CONNACK's Remaining Length bytes
    if (offset + 1 >= ackPacket.size()) {
        return ICommDriver::Status::PROTOCOL_ERROR;
    }
    uint8_t sessionPresent = ackPacket[offset];
    uint8_t returnCode = ackPacket[offset + 1];

    if (returnCode != 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CONNACK failed, code:"); LOG_UINT32(returnCode));
        return ICommDriver::Status::PROTOCOL_ERROR;
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("CONNACK ok, sessionPresent="); LOG_UINT32(sessionPresent));

    m_sessionOpen = true;
    return ICommDriver::Status::SUCCESS;
}

ICommDriver::Status MqttDriver::disconnect()
{
    if (!m_sessionOpen || !m_pTcpip) {
        return ICommDriver::Status::PORT_ACCESS;
    }

    // DISCONNECT: fixed header 0xE0, Remaining Length 0 (no variable header, no payload).
    const std::vector<uint8_t> packet{0xE0, 0x00};
    auto sendSt = m_sendRaw(5000, std::span<const uint8_t>(packet.data(), packet.size()));

    // A clean DISCONNECT is "best effort" per the MQTT spec — the broker
    // doesn't acknowledge it, it just closes the connection — so the
    // session is considered over here regardless of whether the write
    // itself succeeded. Note this also means the broker will NOT publish
    // this client's Last Will (Will messages are only sent for an
    // *unclean* disconnect) — that's what makes DISCONNECT vs. just
    // dropping the TCP connection observably different when testing a Will.
    m_sessionOpen = false;

    return (sendSt == ICommDriver::Status::SUCCESS) ? ICommDriver::Status::SUCCESS
                                                      : ICommDriver::Status::WRITE_ERROR;
}

ICommDriver::Status MqttDriver::subscribe(const std::string& topic, uint8_t qos)
{
    if (!m_sessionOpen) {
        return ICommDriver::Status::PORT_ACCESS;
    }
    if (topic.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("subscribe: empty topic filter"));
        return ICommDriver::Status::INVALID_PARAM;
    }
    if (topic.length() > 0xFFFF) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("subscribe: topic filter longer than 65535 bytes"));
        return ICommDriver::Status::INVALID_PARAM;
    }

    const uint16_t packetId = m_nextPacketId++;
    if (m_nextPacketId == 0) {
        m_nextPacketId = 1; // wrap past 0: 0 is not a valid MQTT packet id
    }

    // SUBSCRIBE: fixed header 0x82 (the one other control packet, besides
    // PUBREL, with mandatory reserved flag bits — 0010 — set).
    std::vector<uint8_t> packet;
    packet.push_back(0x82);
    // Variable Header: Packet Identifier (SUBSCRIBE always carries one, regardless of QoS)
    // Payload: one Topic Filter (Length + string) + Requested QoS — placeholder
    // Remaining Length patched in below, same two-pass approach as m_publish().
    std::vector<uint8_t> varAndPayload;
    varAndPayload.push_back(static_cast<uint8_t>((packetId >> 8) & 0xFF));
    varAndPayload.push_back(static_cast<uint8_t>(packetId & 0xFF));
    varAndPayload.push_back(static_cast<uint8_t>((topic.length() >> 8) & 0xFF));
    varAndPayload.push_back(static_cast<uint8_t>(topic.length() & 0xFF));
    varAndPayload.insert(varAndPayload.end(), topic.begin(), topic.end());
    varAndPayload.push_back(qos & 0x03);

    std::vector<uint8_t> remLenBytes = encodeVarInt(varAndPayload.size());
    packet.insert(packet.end(), remLenBytes.begin(), remLenBytes.end());
    packet.insert(packet.end(), varAndPayload.begin(), varAndPayload.end());

    auto sendSt = m_sendRaw(5000, std::span<const uint8_t>(packet.data(), packet.size()));
    if (sendSt != ICommDriver::Status::SUCCESS) {
        return ICommDriver::Status::WRITE_ERROR;
    }

    std::vector<uint8_t> subAck;
    auto st = m_waitForPacketType(0x90 /*SUBACK*/, packetId, 5000, &subAck);
    if (st != ICommDriver::Status::SUCCESS) {
        return st;
    }

    // SUBACK's Return Code sits right after the Packet Identifier we just
    // matched on — re-derive that same offset (Remaining Length bytes + 2).
    size_t offset = 1;
    decodeVarInt(subAck, offset);
    offset += 2; // Packet Identifier
    if (offset >= subAck.size()) {
        return ICommDriver::Status::PROTOCOL_ERROR;
    }
    const uint8_t returnCode = subAck[offset];
    if (returnCode == 0x80) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SUBSCRIBE refused by broker for topic:"); LOG_STRING(topic));
        return ICommDriver::Status::PROTOCOL_ERROR;
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("SUBSCRIBE ["); LOG_STRING(topic);
              LOG_STRING("] granted qos="); LOG_UINT32(returnCode));

    return ICommDriver::Status::SUCCESS;
}

ICommDriver::Status MqttDriver::unsubscribe(const std::string& topic)
{
    if (!m_sessionOpen) {
        return ICommDriver::Status::PORT_ACCESS;
    }
    if (topic.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("unsubscribe: empty topic filter"));
        return ICommDriver::Status::INVALID_PARAM;
    }

    const uint16_t packetId = m_nextPacketId++;
    if (m_nextPacketId == 0) {
        m_nextPacketId = 1;
    }

    // UNSUBSCRIBE: fixed header 0xA2 (same mandatory reserved bits as SUBSCRIBE/PUBREL).
    std::vector<uint8_t> packet;
    packet.push_back(0xA2);
    std::vector<uint8_t> varAndPayload;
    varAndPayload.push_back(static_cast<uint8_t>((packetId >> 8) & 0xFF));
    varAndPayload.push_back(static_cast<uint8_t>(packetId & 0xFF));
    varAndPayload.push_back(static_cast<uint8_t>((topic.length() >> 8) & 0xFF));
    varAndPayload.push_back(static_cast<uint8_t>(topic.length() & 0xFF));
    varAndPayload.insert(varAndPayload.end(), topic.begin(), topic.end());

    std::vector<uint8_t> remLenBytes = encodeVarInt(varAndPayload.size());
    packet.insert(packet.end(), remLenBytes.begin(), remLenBytes.end());
    packet.insert(packet.end(), varAndPayload.begin(), varAndPayload.end());

    auto sendSt = m_sendRaw(5000, std::span<const uint8_t>(packet.data(), packet.size()));
    if (sendSt != ICommDriver::Status::SUCCESS) {
        return ICommDriver::Status::WRITE_ERROR;
    }

    auto st = m_waitForPacketType(0xB0 /*UNSUBACK*/, packetId, 5000);
    if (st != ICommDriver::Status::SUCCESS) {
        return st;
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("UNSUBSCRIBE ["); LOG_STRING(topic); LOG_STRING("] confirmed"));
    return ICommDriver::Status::SUCCESS;
}

ICommDriver::Status MqttDriver::ping() const
{
    if (!m_sessionOpen) {
        return ICommDriver::Status::PORT_ACCESS;
    }

    // PINGREQ: fixed header 0xC0, Remaining Length 0.
    const std::vector<uint8_t> packet{0xC0, 0x00};
    auto sendSt = m_sendRaw(5000, std::span<const uint8_t>(packet.data(), packet.size()));
    if (sendSt != ICommDriver::Status::SUCCESS) {
        return ICommDriver::Status::WRITE_ERROR;
    }

    // PINGRESP carries no packet id — m_waitForPacketType() assumes one
    // sits right after Remaining Length, which PINGRESP doesn't have, so
    // it can't be reused here; wait for it directly instead.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(5000);
    while (true) {
        const auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::milliseconds(0)) {
            return ICommDriver::Status::READ_TIMEOUT;
        }
        const uint32_t remainingMs = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count());

        std::vector<uint8_t> packetIn;
        auto res = recvPacket(packetIn, remainingMs);
        if (res != ICommDriver::Status::SUCCESS) {
            return res;
        }
        if (!packetIn.empty() && packetIn[0] == 0xD0 /*PINGRESP*/) {
            return ICommDriver::Status::SUCCESS;
        }
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Unexpected packet type while waiting for PINGRESP: 0x");
                  LOG_HEX8(packetIn.empty() ? 0 : packetIn[0]));
        // keep waiting for the actual PINGRESP
    }
}

ICommDriver::Status MqttDriver::m_ensureKeepAlive(uint32_t timeoutMs) const
{
    if (m_config.keepAlive == 0) {
        return ICommDriver::Status::SUCCESS; // keepalive disabled — nothing to do
    }

    const auto elapsed = std::chrono::steady_clock::now() - m_lastActivity;
    const auto threshold = std::chrono::milliseconds(
        static_cast<uint32_t>(m_config.keepAlive) * 800 /* 0.8 * 1000ms */);

    if (elapsed < threshold) {
        return ICommDriver::Status::SUCCESS; // not due yet
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Keepalive due — sending PINGREQ"));
    (void)timeoutMs; // ping() uses its own fixed 5s budget, matching every other ack wait in this driver
    return ping();
}

ICommDriver::Status MqttDriver::m_ackIncomingPublish(uint8_t qos, uint16_t packetId, uint32_t timeoutMs) const
{
    if (qos == 0) {
        return ICommDriver::Status::SUCCESS; // QoS 0 is never acknowledged
    }

    if (qos == 1) {
        // PUBACK: fixed header 0x40, Remaining Length 2, Packet Identifier.
        const std::vector<uint8_t> puback{0x40, 0x02,
                                          static_cast<uint8_t>((packetId >> 8) & 0xFF),
                                          static_cast<uint8_t>(packetId & 0xFF)};
        auto sendSt = m_sendRaw(5000, std::span<const uint8_t>(puback.data(), puback.size()));
        return (sendSt == ICommDriver::Status::SUCCESS) ? ICommDriver::Status::SUCCESS
                                                          : ICommDriver::Status::WRITE_ERROR;
    }

    // qos == 2: send PUBREC, then wait for the broker's PUBREL, then send
    // PUBCOMP — the mirror image of m_waitForAck()'s publisher-side QoS 2
    // handshake (there, we send PUBREL and wait for PUBCOMP).
    const std::vector<uint8_t> pubrec{0x50, 0x02,
                                      static_cast<uint8_t>((packetId >> 8) & 0xFF),
                                      static_cast<uint8_t>(packetId & 0xFF)};
    auto sendSt = m_sendRaw(5000, std::span<const uint8_t>(pubrec.data(), pubrec.size()));
    if (sendSt != ICommDriver::Status::SUCCESS) {
        return ICommDriver::Status::WRITE_ERROR;
    }

    auto st = m_waitForPacketType(0x62 /*PUBREL*/, packetId, timeoutMs);
    if (st != ICommDriver::Status::SUCCESS) {
        return st;
    }

    const std::vector<uint8_t> pubcomp{0x70, 0x02,
                                       static_cast<uint8_t>((packetId >> 8) & 0xFF),
                                       static_cast<uint8_t>(packetId & 0xFF)};
    sendSt = m_sendRaw(5000, std::span<const uint8_t>(pubcomp.data(), pubcomp.size()));
    return (sendSt == ICommDriver::Status::SUCCESS) ? ICommDriver::Status::SUCCESS
                                                      : ICommDriver::Status::WRITE_ERROR;
}

ICommDriver::Status MqttDriver::receiveMessage(uint32_t timeoutMs, std::string& outTopic, std::string& outPayload) const
{
    if (!m_sessionOpen) {
        return ICommDriver::Status::PORT_ACCESS;
    }

    // This is the one call expected to sit idle for a long time between
    // messages — top the keepalive clock up first so a broker-side idle
    // timeout doesn't cut the session out from under a long MQTT.RECEIVE
    // wait (or the background "name ?= MQTT.RECEIVE &" loop).
    auto pingSt = m_ensureKeepAlive(5000);
    if (pingSt != ICommDriver::Status::SUCCESS) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Keepalive PINGREQ failed:"); LOG_STRING(ICommDriver::to_string(pingSt)));
        return pingSt;
    }

    // Wait for the next incoming PUBLISH (fixed header high nibble 0x3 —
    // low nibble carries DUP/QoS/RETAIN flags, which vary per message, so
    // unlike m_waitForPacketType() (used for the fixed-flag acks PUBACK/
    // PUBREC/PUBCOMP/SUBACK) this can't reuse it: it needs a high-nibble
    // match, not an exact-byte match, and there is no packet id to filter
    // on up front either (QoS 0 PUBLISHes carry none at all).
    std::vector<uint8_t> packet;
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (true) {
            const auto remaining = deadline - std::chrono::steady_clock::now();
            if (remaining <= std::chrono::milliseconds(0)) {
                return ICommDriver::Status::READ_TIMEOUT;
            }
            const uint32_t remainingMs = static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count());

            auto res = recvPacket(packet, remainingMs);
            if (res != ICommDriver::Status::SUCCESS) {
                return res;
            }
            if (!packet.empty() && (packet[0] & 0xF0) == 0x30) {
                break;
            }
            if (!packet.empty() && packet[0] == 0xD0 /*PINGRESP*/) {
                // A stray PINGRESP to a keepalive PINGREQ we sent moments
                // ago (or one whose wait in ping() already timed out) —
                // not an error, just not what we're waiting for here.
                packet.clear();
                continue;
            }
            LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Unexpected packet type while waiting for PUBLISH: 0x");
                      LOG_HEX8(packet.empty() ? 0 : packet[0]));
            packet.clear();
            // keep waiting — see the doc comment on receiveMessage()'s declaration
        }
    }

    const uint8_t qos = (packet[0] >> 1) & 0x03;

    size_t offset = 1;
    const uint32_t remainingLen = decodeVarInt(packet, offset);
    (void)remainingLen; // packet.size() already reflects the actual bytes read; not needed beyond validation below

    if (offset + 2 > packet.size()) {
        return ICommDriver::Status::PROTOCOL_ERROR;
    }
    const uint16_t topicLen = static_cast<uint16_t>((packet[offset] << 8) | packet[offset + 1]);
    offset += 2;
    if (offset + topicLen > packet.size()) {
        return ICommDriver::Status::PROTOCOL_ERROR;
    }
    outTopic.assign(reinterpret_cast<const char*>(packet.data() + offset), topicLen);
    offset += topicLen;

    uint16_t packetId = 0;
    if (qos > 0) {
        if (offset + 2 > packet.size()) {
            return ICommDriver::Status::PROTOCOL_ERROR;
        }
        packetId = static_cast<uint16_t>((packet[offset] << 8) | packet[offset + 1]);
        offset += 2;
    }

    // Everything remaining is the payload (MQTT PUBLISH has no length
    // prefix of its own for it — it's simply "whatever is left").
    outPayload.assign(reinterpret_cast<const char*>(packet.data() + offset), packet.size() - offset);

    auto ackSt = m_ackIncomingPublish(qos, packetId, timeoutMs);
    if (ackSt != ICommDriver::Status::SUCCESS) {
        // The message itself was received intact; only its acknowledgement
        // failed. Surface the failure (the broker may redeliver), but the
        // caller already has outTopic/outPayload if it wants to use them
        // regardless.
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Failed to acknowledge incoming PUBLISH on topic:"); LOG_STRING(outTopic));
        return ackSt;
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("PUBLISH received ["); LOG_STRING(outTopic);
              LOG_STRING("] qos="); LOG_UINT32(qos); LOG_STRING("bytes="); LOG_SIZET(outPayload.size()));

    return ICommDriver::Status::SUCCESS;
}

ICommDriver::Status MqttDriver::m_publish(const std::string& topic, const std::string& payload, uint16_t* pPacketId) const
{
    if (!m_sessionOpen) {
        return ICommDriver::Status::PORT_ACCESS;
    }
    if (topic.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("publish: empty topic (xtra_params) — a topic is required"));
        return ICommDriver::Status::INVALID_PARAM;
    }
    if (topic.length() > 0xFFFF) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("publish: topic longer than 65535 bytes"));
        return ICommDriver::Status::INVALID_PARAM;
    }

    const uint8_t qos = m_config.qos & 0x03;

    std::vector<uint8_t> packet;
    packet.push_back(0x30); // placeholder Fixed Header, patched below once flags/Remaining Length are known

    // Variable Header: Topic Length (16-bit big-endian) + Topic
    packet.push_back(static_cast<uint8_t>((topic.length() >> 8) & 0xFF));
    packet.push_back(static_cast<uint8_t>(topic.length() & 0xFF));
    packet.insert(packet.end(), topic.begin(), topic.end());

    // Packet Identifier — QoS 1/2 only (QoS 0 has none, and is never acknowledged)
    uint16_t packetId = 0;
    if (qos > 0) {
        packetId = m_nextPacketId++;
        if (m_nextPacketId == 0) {
            m_nextPacketId = 1; // wrap past 0: 0 is not a valid MQTT packet id
        }
        packet.push_back(static_cast<uint8_t>((packetId >> 8) & 0xFF));
        packet.push_back(static_cast<uint8_t>(packetId & 0xFF));
    }

    // Payload
    packet.insert(packet.end(), payload.begin(), payload.end());

    // Remaining Length, then the real Fixed Header (DUP=0, QoS, RETAIN — the
    // previous implementation always sent a bare 0x30 here, meaning QoS/RETAIN
    // were silently never actually signalled to the broker no matter what
    // Config said; that bug is fixed as part of this rewrite).
    const size_t remainingLen = packet.size() - 1; // exclude the placeholder Fixed Header byte
    std::vector<uint8_t> remLenBytes = encodeVarInt(remainingLen);
    packet.erase(packet.begin());
    packet.insert(packet.begin(), remLenBytes.begin(), remLenBytes.end());
    packet.insert(packet.begin(), static_cast<uint8_t>(0x30 | (qos << 1) | (m_config.retain ? 0x01 : 0x00)));

    auto sendSt = m_sendRaw(5000, std::span<const uint8_t>(packet.data(), packet.size()));
    if (sendSt != ICommDriver::Status::SUCCESS) {
        return ICommDriver::Status::WRITE_ERROR;
    }

    if (pPacketId) {
        *pPacketId = packetId;
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("PUBLISH ["); LOG_STRING(topic);
              LOG_STRING("] qos="); LOG_UINT32(qos); LOG_STRING("bytes="); LOG_SIZET(payload.size()));

    return ICommDriver::Status::SUCCESS;
}

ICommDriver::Status MqttDriver::m_waitForPacketType(uint8_t expectedType, uint16_t expectedPacketId,
                                                    uint32_t timeoutMs, std::vector<uint8_t>* pOutPacket) const
{
    // Budget timeoutMs across as many recvPacket() calls as it takes to
    // find the matching packet (a stray/mismatched packet arriving in the
    // meantime shouldn't reset the clock) — each call gets whatever's left
    // of the deadline.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    while (true) {
        const auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::milliseconds(0)) {
            return ICommDriver::Status::READ_TIMEOUT;
        }
        const uint32_t remainingMs = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count());

        std::vector<uint8_t> packet;
        auto res = recvPacket(packet, remainingMs);
        if (res != ICommDriver::Status::SUCCESS) {
            return res;
        }
        if (packet.empty()) {
            return ICommDriver::Status::PROTOCOL_ERROR;
        }

        if (packet[0] != expectedType) {
            LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Unexpected packet type while waiting for ack: 0x"); LOG_HEX8(packet[0]));
            continue;
        }

        // Packet Identifier sits right after this packet's own Remaining Length field.
        size_t offset = 1;
        decodeVarInt(packet, offset);
        if (offset + 1 >= packet.size()) {
            return ICommDriver::Status::PROTOCOL_ERROR;
        }

        const uint16_t respPacketId = static_cast<uint16_t>((packet[offset] << 8) | packet[offset + 1]);
        if (respPacketId == expectedPacketId) {
            if (pOutPacket) {
                *pOutPacket = std::move(packet);
            }
            return ICommDriver::Status::SUCCESS;
        }

        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Ack packet id mismatch, expected"); LOG_UINT32(expectedPacketId);
                  LOG_STRING("got"); LOG_UINT32(respPacketId); LOG_STRING("— still waiting"));
        // Could be a stray/late ack for an older packet id; keep waiting for ours.
    }
}

ICommDriver::ReadResult MqttDriver::m_waitForAck(uint8_t qos, uint16_t packetId, uint32_t timeoutMs, std::span<uint8_t> buffer) const
{
    ICommDriver::ReadResult result;

    auto fillConfirmation = [&](const char* pszText) {
        const size_t szLen = std::min(buffer.size(), std::strlen(pszText));
        if (szLen > 0) {
            std::memcpy(buffer.data(), pszText, szLen);
        }
        result.status     = ICommDriver::Status::SUCCESS;
        result.bytes_read = szLen;
    };

    if (qos == 1) {
        auto st = m_waitForPacketType(0x40 /*PUBACK*/, packetId, timeoutMs);
        if (st != ICommDriver::Status::SUCCESS) {
            result.status = st;
            return result;
        }
        fillConfirmation("PUBACK");
        return result;
    }

    if (qos == 2) {
        auto st = m_waitForPacketType(0x50 /*PUBREC*/, packetId, timeoutMs);
        if (st != ICommDriver::Status::SUCCESS) {
            result.status = st;
            return result;
        }

        // PUBREL — the one MQTT control packet with mandatory reserved flag
        // bits (0010) set in its fixed header: 0110 0010 = 0x62.
        const std::vector<uint8_t> pubrel{0x62, 0x02,
                                          static_cast<uint8_t>((packetId >> 8) & 0xFF),
                                          static_cast<uint8_t>(packetId & 0xFF)};
        auto sendSt = m_sendRaw(5000, std::span<const uint8_t>(pubrel.data(), pubrel.size()));
        if (sendSt != ICommDriver::Status::SUCCESS) {
            result.status = ICommDriver::Status::WRITE_ERROR;
            return result;
        }

        st = m_waitForPacketType(0x70 /*PUBCOMP*/, packetId, timeoutMs);
        if (st != ICommDriver::Status::SUCCESS) {
            result.status = st;
            return result;
        }
        fillConfirmation("PUBCOMP");
        return result;
    }

    // qos == 0 (or anything else): nothing to acknowledge.
    result.status     = ICommDriver::Status::SUCCESS;
    result.bytes_read = 0;
    return result;
}

// -----------------------------------------------------------------------
// Private Helpers
// -----------------------------------------------------------------------

ICommDriver::Status MqttDriver::setupTls()
{
    static bool sslInited = false;
    if (!sslInited) {
        SSL_library_init();
        SSL_load_error_strings();
        sslInited = true;
    }

    m_sslCtx = SSL_CTX_new(TLS_client_method());
    if (!m_sslCtx) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SSL_CTX_new failed"));
        return ICommDriver::Status::OPERATION_FAILED;
    }
    SSL_CTX_set_min_proto_version(m_sslCtx, TLS1_2_VERSION);

    if (!m_config.caCertPath.empty()) {
        if (SSL_CTX_load_verify_locations(m_sslCtx, m_config.caCertPath.c_str(), nullptr) != 1) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CA Cert load failed:"); LOG_STRING(m_config.caCertPath));
            return ICommDriver::Status::OPERATION_FAILED;
        }
        SSL_CTX_set_verify(m_sslCtx, SSL_VERIFY_PEER, nullptr);
    } else {
        // No CA given: this is the common "point at Mosquitto's self-signed
        // test cert" case — verifying against nothing would just always
        // fail, so skip verification and say so loudly rather than
        // silently accepting anything.
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("TLS enabled with no CA cert configured — server certificate will NOT be verified"));
        SSL_CTX_set_verify(m_sslCtx, SSL_VERIFY_NONE, nullptr);
    }

    // Mutual TLS (Mosquitto's require_certificate true): both cert and key
    // must be supplied together.
    if (!m_config.clientCertPath.empty() || !m_config.clientKeyPath.empty()) {
        if (m_config.clientCertPath.empty() || m_config.clientKeyPath.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("TLS client cert/key: both must be set for mutual TLS, only one was"));
            return ICommDriver::Status::INVALID_PARAM;
        }
        if (SSL_CTX_use_certificate_file(m_sslCtx, m_config.clientCertPath.c_str(), SSL_FILETYPE_PEM) != 1) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Client cert load failed:"); LOG_STRING(m_config.clientCertPath));
            return ICommDriver::Status::OPERATION_FAILED;
        }
        if (SSL_CTX_use_PrivateKey_file(m_sslCtx, m_config.clientKeyPath.c_str(), SSL_FILETYPE_PEM) != 1) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Client key load failed:"); LOG_STRING(m_config.clientKeyPath));
            return ICommDriver::Status::OPERATION_FAILED;
        }
        if (SSL_CTX_check_private_key(m_sslCtx) != 1) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Client cert/key mismatch"));
            return ICommDriver::Status::OPERATION_FAILED;
        }
    }

    m_ssl = SSL_new(m_sslCtx);
    if (!m_ssl) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SSL_new failed"));
        return ICommDriver::Status::OPERATION_FAILED;
    }

    // SNI + (when verifying) hostname verification against the cert's
    // SAN/CN — both keyed off the configured broker hostname.
    SSL_set_tlsext_host_name(m_ssl, m_config.host.c_str());
    if (!m_config.caCertPath.empty()) {
        SSL_set1_host(m_ssl, m_config.host.c_str());
    }

    SSL_set_fd(m_ssl, m_pTcpip->nativeHandle());

    // Handshake, bounded by connectTimeoutMs the same way TCPIP::open()
    // bounds the TCP-level connect() — the underlying fd is left in
    // blocking mode (TCPIP restores it after its own connect()), so
    // SSL_connect() can return WANT_READ/WANT_WRITE only on the very first
    // call before any bytes have moved; poll() + retry handles that.
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(m_config.connectTimeoutMs ? m_config.connectTimeoutMs : 5000);
    while (true) {
        const int rc = SSL_connect(m_ssl);
        if (rc == 1) {
            break; // handshake complete
        }

        const int sslErr = SSL_get_error(m_ssl, rc);
        if (sslErr != SSL_ERROR_WANT_READ && sslErr != SSL_ERROR_WANT_WRITE) {
            char errBuf[256];
            ERR_error_string_n(ERR_get_error(), errBuf, sizeof(errBuf));
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("TLS handshake failed:"); LOG_STRING(errBuf));
            return ICommDriver::Status::OPERATION_FAILED;
        }

        const auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::milliseconds(0)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("TLS handshake timed out"));
            return ICommDriver::Status::OPERATION_FAILED;
        }

        struct pollfd pfd{};
        pfd.fd = m_pTcpip->nativeHandle();
        pfd.events = static_cast<short>(sslErr == SSL_ERROR_WANT_WRITE ? POLLOUT : POLLIN);
        ::poll(&pfd, 1, static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count()));
        // loop back around and retry SSL_connect() regardless of poll's
        // outcome — a spurious wakeup just costs one extra WANT_READ/WRITE
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("TLS handshake complete, cipher:"); LOG_STRING(SSL_get_cipher(m_ssl)));
    return ICommDriver::Status::SUCCESS;
}

ICommDriver::Status MqttDriver::m_sendRaw(uint32_t timeoutMs, std::span<const uint8_t> buffer) const
{
    if (!m_ssl) {
        auto res = m_pTcpip->tout_write(timeoutMs, buffer);
        if (res.status == ICommDriver::Status::SUCCESS) {
            m_lastActivity = std::chrono::steady_clock::now();
        }
        return res.status;
    }

    // TLS path: SSL_write() over the (blocking) fd. A short write can still
    // ask for WANT_READ (renegotiation) as well as WANT_WRITE, so both are
    // handled the same way — poll the appropriate direction and retry.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    size_t totalWritten = 0;
    while (totalWritten < buffer.size()) {
        const int rc = SSL_write(m_ssl, buffer.data() + totalWritten,
                                  static_cast<int>(buffer.size() - totalWritten));
        if (rc > 0) {
            totalWritten += static_cast<size_t>(rc);
            continue;
        }

        const int sslErr = SSL_get_error(m_ssl, rc);
        if (sslErr != SSL_ERROR_WANT_READ && sslErr != SSL_ERROR_WANT_WRITE) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SSL_write failed, SSL error:"); LOG_INT32(sslErr));
            return ICommDriver::Status::WRITE_ERROR;
        }

        const auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::milliseconds(0)) {
            return ICommDriver::Status::WRITE_TIMEOUT;
        }
        struct pollfd pfd{};
        pfd.fd = m_pTcpip->nativeHandle();
        pfd.events = static_cast<short>(sslErr == SSL_ERROR_WANT_WRITE ? POLLOUT : POLLIN);
        ::poll(&pfd, 1, static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count()));
    }

    m_lastActivity = std::chrono::steady_clock::now();
    return ICommDriver::Status::SUCCESS;
}

ICommDriver::Status MqttDriver::m_recvRaw(uint32_t timeoutMs, std::span<uint8_t> buffer, size_t& bytesRead) const
{
    bytesRead = 0;

    if (!m_ssl) {
        auto res = m_pTcpip->tout_read(timeoutMs, buffer,
            ICommDriver::ReadOptions{.mode = ICommDriver::ReadMode::Exact});
        bytesRead = res.bytes_read;
        return res.status;
    }

    // TLS path: mirror TCPIP::timeout_read()'s "one poll + one read, return
    // whatever came back" shape, just via SSL_read() instead of recv().
    struct pollfd pfd{};
    pfd.fd = m_pTcpip->nativeHandle();
    pfd.events = POLLIN;
    const int pollRc = ::poll(&pfd, 1, static_cast<int>(timeoutMs));
    if (pollRc <= 0) {
        return ICommDriver::Status::READ_TIMEOUT;
    }

    const int rc = SSL_read(m_ssl, buffer.data(), static_cast<int>(buffer.size()));
    if (rc > 0) {
        bytesRead = static_cast<size_t>(rc);
        return ICommDriver::Status::SUCCESS;
    }

    const int sslErr = SSL_get_error(m_ssl, rc);
    if (sslErr == SSL_ERROR_WANT_READ || sslErr == SSL_ERROR_WANT_WRITE) {
        // poll() said data was ready but SSL needed a protocol-level
        // round-trip first (e.g. a session ticket) — from the caller's
        // point of view that's indistinguishable from "nothing yet";
        // recvPacket()'s own retry loop will call back in.
        return ICommDriver::Status::READ_TIMEOUT;
    }
    if (sslErr == SSL_ERROR_ZERO_RETURN) {
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("TLS peer closed the connection"));
        return ICommDriver::Status::READ_ERROR;
    }

    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SSL_read failed, SSL error:"); LOG_INT32(sslErr));
    return ICommDriver::Status::READ_ERROR;
}

// Bounds every read once a packet has started arriving (Remaining Length
// bytes, then Payload) — generous for network jitter, but not meant to be
// tuned by callers: a message that starts but stalls mid-transmission is a
// broken-connection problem, not a "nothing to receive yet" one (that's
// what recvPacket()'s own timeoutMs parameter is for).
static constexpr uint32_t kPacketContinuationTimeoutMs = 5000;

ICommDriver::Status MqttDriver::recvPacket(std::vector<uint8_t>& packetOut, uint32_t timeoutMs) const
{
    // 1. Read the First Byte (Fixed Header) — this is the only part of a
    // packet that can legitimately take a while to arrive (nothing new to
    // receive yet), so it's the only read bounded by the caller's timeoutMs.
    uint8_t firstByte = 0;
    {
        std::vector<uint8_t> buf(1);
        size_t got = 0;
        auto st = m_recvRaw(timeoutMs, std::span<uint8_t>(buf), got);
        if (st != ICommDriver::Status::SUCCESS || got == 0) {
            return ICommDriver::Status::READ_TIMEOUT;
        }
        firstByte = buf[0];
    }
    packetOut.push_back(firstByte);

    // 2. Read Remaining Length Bytes
    std::vector<uint8_t> remLenBytes;
    uint32_t remLen = 0;
    int multiplier = 1;
    uint8_t digit;

    while (true) {
        if (remLenBytes.size() >= 4) {
            return ICommDriver::Status::PROTOCOL_ERROR;
        }

        std::vector<uint8_t> buf(1);
        size_t got = 0;
        auto st = m_recvRaw(kPacketContinuationTimeoutMs, std::span<uint8_t>(buf), got);
        if (st != ICommDriver::Status::SUCCESS || got == 0) {
            return ICommDriver::Status::READ_TIMEOUT;
        }

        digit = buf[0];
        remLenBytes.push_back(digit);
        remLen += (digit & 0x7F) * multiplier;
        multiplier *= 128;
        packetOut.push_back(digit);

        if ((digit & 0x80) == 0) {
            break;
        }
    }

    // 3. Read Payload (Remaining Length bytes)
    if (remLen > 0) {
        std::vector<uint8_t> payloadBuf(remLen);
        size_t totalRead = 0;
        while (totalRead < remLen) {
            size_t toRead = remLen - totalRead;
            size_t got = 0;
            auto st = m_recvRaw(kPacketContinuationTimeoutMs,
                std::span<uint8_t>(payloadBuf.data() + totalRead, toRead), got);

            if (st != ICommDriver::Status::SUCCESS || got == 0) {
                return ICommDriver::Status::READ_TIMEOUT;
            }
            totalRead += got;
        }
        packetOut.insert(packetOut.end(), payloadBuf.begin(), payloadBuf.end());
    }

    return ICommDriver::Status::SUCCESS;
}

uint32_t MqttDriver::decodeVarInt(const std::vector<uint8_t>& data, size_t& offset) const
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

std::vector<uint8_t> MqttDriver::encodeVarInt(uint32_t value) const
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

bool MqttDriver::isConnected() const
{
    return m_sessionOpen;
}

// -----------------------------------------------------------------------
// ICommDriver: PUBLISH (tout_write) and its acknowledgement (tout_read)
//
// See the class doc comment (mqtt_driver.hpp) for the full contract. Both
// are what CommScriptCommandInterpreter<MqttDriver> / CommScriptClient
// <MqttDriver> call for MQTT.CMD / MQTT.SCRIPT — this is the only publish
// path; there is no separate PUBLISH-style command.
// -----------------------------------------------------------------------

bool MqttDriver::is_open() const
{
    return m_connected && m_pTcpip && m_pTcpip->is_open();
}

ICommDriver::WriteResult MqttDriver::tout_write(uint32_t u32WriteTimeout,
                                                 std::span<const uint8_t> buffer,
                                                 std::string_view xtra_params) const
{
    (void)u32WriteTimeout; // PUBLISH is a single non-blocking TCP write; the
                            // timeout that matters is the ack wait, passed to
                            // tout_read() instead — see the class doc comment.
    ICommDriver::WriteResult result;

    if (!is_open() || !m_sessionOpen) {
        result.status = ICommDriver::Status::PORT_ACCESS;
        return result;
    }

    if (xtra_params.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("tout_write: a topic is required, e.g. '~ sensors/temp'"));
        result.status = ICommDriver::Status::INVALID_PARAM;
        return result;
    }

    const std::string topic(xtra_params);
    const std::string payload(reinterpret_cast<const char*>(buffer.data()), buffer.size());

    uint16_t packetId = 0;
    auto st = m_publish(topic, payload, &packetId);
    if (st != ICommDriver::Status::SUCCESS) {
        m_bAwaitingAck = false;
        result.status = st;
        return result;
    }

    // QoS 0 has no acknowledgement at all — leave nothing outstanding, so a
    // subsequent tout_read() (if the script calls one) returns immediately
    // instead of waiting for something that will never arrive.
    m_bAwaitingAck    = (m_config.qos > 0);
    m_pendingPacketId = packetId;
    m_pendingQos      = m_config.qos;

    result.status        = ICommDriver::Status::SUCCESS;
    result.bytes_written = buffer.size();
    return result;
}

ICommDriver::ReadResult MqttDriver::tout_read(uint32_t u32ReadTimeout,
                                               std::span<uint8_t> buffer,
                                               const ICommDriver::ReadOptions& options,
                                               std::string_view xtra_params) const
{
    (void)xtra_params; // nothing to address here — see class doc comment

    ICommDriver::ReadResult result;

    if (!is_open() || !m_sessionOpen) {
        result.status = ICommDriver::Status::PORT_ACCESS;
        return result;
    }

    if (options.mode != ICommDriver::ReadMode::Exact) {
        // A PUBACK/PUBCOMP confirmation is a single fixed value, not a
        // delimited/tokenised byte stream — LINE/TOKEN reads don't apply.
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("tout_read: only exact reads are supported"));
        result.status = ICommDriver::Status::INVALID_PARAM;
        return result;
    }

    if (!m_bAwaitingAck) {
        // Either the last tout_write() was QoS 0 (nothing to acknowledge)
        // or tout_read() was called without a preceding tout_write() at
        // all — either way, there's nothing to wait for.
        result.status     = ICommDriver::Status::SUCCESS;
        result.bytes_read = 0;
        return result;
    }

    const uint8_t  qos      = m_pendingQos;
    const uint16_t packetId = m_pendingPacketId;
    m_bAwaitingAck = false; // one tout_write() pairs with at most one tout_read()

    return m_waitForAck(qos, packetId, u32ReadTimeout, buffer);
}
