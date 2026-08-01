#include "mqtt_driver.hpp"
#include "uLogger.hpp"
#include <cstring>
#include <algorithm>

#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LOG_HDR "MQTT_DRV    |"

// OpenSSL Includes
#include <openssl/ssl.h>
#include <openssl/err.h>

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
        SSL_free(m_ssl);
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

    // 1. Construct CONNECT Packet
    std::vector<uint8_t> packet;

    // Variable Header
    std::string protocolName = "MQTT";
    packet.push_back(0); // Length MSB
    packet.push_back(4); // Length LSB
    packet.insert(packet.end(), protocolName.begin(), protocolName.end());

    packet.push_back(4); // MQTT Version 3.1.1

    // Connect Flags: Clean Session (1), No User/Pass (0), etc.
    uint8_t flags = 0xC2; // Clean Session = 1, Reserved = 0
    if (m_config.clientId.empty()) flags |= 0x00;
    packet.push_back(flags);

    // Keep Alive (2 bytes, big endian)
    packet.push_back((m_config.keepAlive >> 8) & 0xFF);
    packet.push_back(m_config.keepAlive & 0xFF);

    // Client ID
    std::string clientId = m_config.clientId.empty() ? "mqtt_client_" : m_config.clientId;
    packet.push_back(0); // Length MSB
    packet.push_back(clientId.length()); // Length LSB
    packet.insert(packet.end(), clientId.begin(), clientId.end());

    // Compute Remaining Length
    size_t varHeaderLen = 2 + protocolName.length() + 1 + 1 + 2; // Protocol + Version + Flags + KeepAlive
    size_t payloadLen = varHeaderLen + 2 + clientId.length(); // +2 for ClientID Length field

    std::vector<uint8_t> remainingLenBytes = encodeVarInt(payloadLen);

    // Prepend Remaining Length and Fixed Header
    packet.insert(packet.begin(), remainingLenBytes.begin(), remainingLenBytes.end());
    packet.insert(packet.begin(), 0x10); // Fixed Header: CONNECT

    // Send
    auto writeRes = m_pTcpip->tout_write(5000, std::span<uint8_t>(packet.data(), packet.size()));
    if (writeRes.status != ICommDriver::Status::SUCCESS) {
        return ICommDriver::Status::WRITE_ERROR;
    }

    // 2. Receive CONNACK
    std::vector<uint8_t> ackPacket;
    auto res = recvPacket(ackPacket);
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
    auto writeRes = m_pTcpip->tout_write(5000, std::span<const uint8_t>(packet.data(), packet.size()));

    // A clean DISCONNECT is "best effort" per the MQTT spec — the broker
    // doesn't acknowledge it, it just closes the connection — so the
    // session is considered over here regardless of whether the write
    // itself succeeded.
    m_sessionOpen = false;

    return (writeRes.status == ICommDriver::Status::SUCCESS) ? ICommDriver::Status::SUCCESS
                                                               : ICommDriver::Status::WRITE_ERROR;
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

    auto writeRes = m_pTcpip->tout_write(5000, std::span<const uint8_t>(packet.data(), packet.size()));
    if (writeRes.status != ICommDriver::Status::SUCCESS) {
        return ICommDriver::Status::WRITE_ERROR;
    }

    if (pPacketId) {
        *pPacketId = packetId;
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("PUBLISH ["); LOG_STRING(topic);
              LOG_STRING("] qos="); LOG_UINT32(qos); LOG_STRING("bytes="); LOG_SIZET(payload.size()));

    return ICommDriver::Status::SUCCESS;
}

ICommDriver::Status MqttDriver::m_waitForPacketType(uint8_t expectedType, uint16_t expectedPacketId, uint32_t timeoutMs) const
{
    // NOTE: recvPacket() applies its own fixed per-read timeouts to each
    // underlying TCP read rather than a single deadline shared across the
    // whole wait; if several non-matching packets arrive back-to-back this
    // loop can in principle run longer than timeoutMs. Same characteristic
    // the previous waitForResponse() had — flagging it here rather than
    // silently carrying it forward unremarked.
    (void)timeoutMs;

    while (true) {
        std::vector<uint8_t> packet;
        auto res = recvPacket(packet);
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
        auto writeRes = m_pTcpip->tout_write(5000, std::span<const uint8_t>(pubrel.data(), pubrel.size()));
        if (writeRes.status != ICommDriver::Status::SUCCESS) {
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
    SSL_library_init();
    m_sslCtx = SSL_CTX_new(TLS_client_method());
    if (!m_sslCtx) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SSL_CTX_new failed"));
        return ICommDriver::Status::OPERATION_FAILED;
    }

    if (!m_config.caCertPath.empty()) {
        if (SSL_CTX_load_verify_locations(m_sslCtx, m_config.caCertPath.c_str(), nullptr) != 1) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CA Cert load failed"));
            return ICommDriver::Status::OPERATION_FAILED;
        }
    }
    return ICommDriver::Status::SUCCESS;
}

ICommDriver::Status MqttDriver::recvPacket(std::vector<uint8_t>& packetOut) const
{
    // 1. Read the First Byte (Fixed Header)
    uint8_t firstByte = 0;
    {
        std::vector<uint8_t> buf(1);
        auto res = m_pTcpip->tout_read(1000, std::span<uint8_t>(buf),
            ICommDriver::ReadOptions{.mode = ICommDriver::ReadMode::Exact});
        if (res.status != ICommDriver::Status::SUCCESS || res.bytes_read == 0) {
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
        auto res = m_pTcpip->tout_read(1000, std::span<uint8_t>(buf),
            ICommDriver::ReadOptions{.mode = ICommDriver::ReadMode::Exact});
        if (res.status != ICommDriver::Status::SUCCESS || res.bytes_read == 0) {
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
            auto res = m_pTcpip->tout_read(5000,
                std::span<uint8_t>(payloadBuf.data() + totalRead, toRead),
                ICommDriver::ReadOptions{.mode = ICommDriver::ReadMode::Exact});

            if (res.status != ICommDriver::Status::SUCCESS || res.bytes_read == 0) {
                return ICommDriver::Status::READ_TIMEOUT;
            }
            totalRead += res.bytes_read;
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
