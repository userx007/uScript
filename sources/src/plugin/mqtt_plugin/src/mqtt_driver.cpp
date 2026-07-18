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

    // Validate ACK Type
    if (ackPacket[0] != 0x20) { // CONNACK
        return ICommDriver::Status::PROTOCOL_ERROR;
    }

    // Check Return Code (byte at index 3: FixedHeader(1) + RemLen(1-4) + SessionPresent(1) + ReturnCode(1))
    // Simplified: Skip variable header length
    size_t offset = 1 + remainingLenBytes.size();
    uint8_t sessionPresent = ackPacket[offset];
    uint8_t returnCode = ackPacket[offset + 1];

    if (returnCode != 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CONNACK failed, code:"); LOG_UINT32(returnCode));
        return ICommDriver::Status::PROTOCOL_ERROR;
    }

    m_sessionOpen = true;
    return ICommDriver::Status::SUCCESS;
}

ICommDriver::Status MqttDriver::publish(const std::string& topic, const std::string& payload, uint8_t qos, bool retain)
{
    if (!m_sessionOpen) return ICommDriver::Status::PORT_ACCESS;

    std::vector<uint8_t> packet;

    // Fixed Header
    packet.push_back(0x30); // PUBLISH (0x30) | Dup:0, QoS:qos, Retain:retain

    // Variable Header: Topic Length + Topic
    packet.push_back(0); // Topic Length MSB
    packet.push_back(topic.length()); // Topic Length LSB
    packet.insert(packet.end(), topic.begin(), topic.end());

    // If QoS > 0, add Packet Identifier
    uint16_t packetId = 0;
    if (qos > 0) {
        packetId = m_nextPacketId++;
        packet.push_back((packetId >> 8) & 0xFF);
        packet.push_back(packetId & 0xFF);
    }

    // Payload
    packet.insert(packet.end(), payload.begin(), payload.end());

    // Calculate Remaining Length
    size_t remainingLen = packet.size() - 1; // Exclude Fixed Header
    std::vector<uint8_t> remLenBytes = encodeVarInt(remainingLen);

    // Rebuild Fixed Header with correct Remaining Length
    packet.erase(packet.begin()); // Remove old 0x30
    packet.insert(packet.begin(), remLenBytes.begin(), remLenBytes.end());
    packet.insert(packet.begin(), 0x30);

    // Send
    auto writeRes = m_pTcpip->tout_write(5000, std::span<uint8_t>(packet.data(), packet.size()));
    if (writeRes.status != ICommDriver::Status::SUCCESS) {
        return ICommDriver::Status::WRITE_ERROR;
    }

    // If QoS 1, wait for PUBACK
    if (qos == 1) {
        return waitForResponse(packetId, 5000, 0x40); // 0x40 = PUBACK
    }

    return ICommDriver::Status::SUCCESS;
}

ICommDriver::Status MqttDriver::waitForResponse(uint16_t packetId, uint32_t timeoutMs, uint8_t expectedType)
{
    while (true) {
        std::vector<uint8_t> packet;
        auto res = recvPacket(packet);
        if (res != ICommDriver::Status::SUCCESS) {
            if (res == ICommDriver::Status::READ_TIMEOUT) {
                return ICommDriver::Status::READ_TIMEOUT;
            }
            return res;
        }

        if (packet[0] != expectedType) {
            LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Unexpected packet type: 0x") LOG_HEX8(packet[0]));
            continue;
        }

        // Extract Packet ID from response
        size_t offset = 1;
        uint8_t firstByte = packet[offset];
        offset += 1;

        // Skip remaining length bytes (variable byte integer)
        uint32_t remLen = 0;
        int multiplier = 1;
        uint8_t digit;
        do {
            if (offset >= packet.size()) break;
            digit = packet[offset];
            remLen += (digit & 0x7F) * multiplier;
            multiplier *= 128;
            offset++;
        } while ((digit & 0x80) != 0);

        // After remaining length, we expect Packet ID
        if (offset + 1 < packet.size()) {
            uint16_t respPacketId = (packet[offset] << 8) | packet[offset+1];
            if (respPacketId == packetId) {
                return ICommDriver::Status::SUCCESS;
            }
        }
    }
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

ICommDriver::Status MqttDriver::recvPacket(std::vector<uint8_t>& packetOut)
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

ICommDriver::Status MqttDriver::sendRaw(const std::vector<uint8_t>& data)
{
    if (data.empty()) {

        return ICommDriver::Status::SUCCESS;
    }

    std::span<const uint8_t> spanData(data);
    ICommDriver::WriteResult res = m_pTcpip->tout_write(5000, spanData);

    return res.status;
}

bool MqttDriver::isConnected() const
{
    return m_sessionOpen;
}

// -----------------------------------------------------------------------
// ICommDriver pass-through
//
// MQTT.CMD / MQTT.SCRIPT reuse the same CommScriptCommandInterpreter<T> /
// CommScriptClient<T> machinery as TCPIP.CMD / TCPIP.SCRIPT. Those templates
// talk to the driver purely through the ICommDriver surface, so here that
// surface is simply forwarded to the already-connected TCPIP socket - this
// lets a script send/expect raw bytes on the live MQTT session without
// re-implementing framing, timeouts, etc. a second time.
// -----------------------------------------------------------------------

bool MqttDriver::is_open() const
{
    return m_connected && m_pTcpip && m_pTcpip->is_open();
}

ICommDriver::ReadResult MqttDriver::tout_read(uint32_t u32ReadTimeout,
                                               std::span<uint8_t> buffer,
                                               const ICommDriver::ReadOptions& options,
                                               std::string_view xtra_params) const
{
    if (!m_pTcpip) {
        return ICommDriver::ReadResult{ICommDriver::Status::PORT_ACCESS, 0, false};
    }
    return m_pTcpip->tout_read(u32ReadTimeout, buffer, options, xtra_params);
}

ICommDriver::WriteResult MqttDriver::tout_write(uint32_t u32WriteTimeout,
                                                 std::span<const uint8_t> buffer,
                                                 std::string_view xtra_params) const
{
    if (!m_pTcpip) {
        return ICommDriver::WriteResult{ICommDriver::Status::PORT_ACCESS, 0};
    }
    return m_pTcpip->tout_write(u32WriteTimeout, buffer, xtra_params);
}
