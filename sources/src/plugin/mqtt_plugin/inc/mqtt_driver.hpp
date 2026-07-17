#ifndef MQTT_DRIVER_HPP
#define MQTT_DRIVER_HPP

#include "uTcpip.hpp"
#include <memory>
#include <vector>
#include <string>

typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;


/**
 * @brief MQTT Client Driver wrapping the generic TCPIP driver.
 *
 * Implements the MQTT v3.1.1 protocol logic on top of the byte-stream TCPIP driver.
 * Handles CONNECT/CONNACK handshake and PUBLISH/PUBACK cycles.
 */
class MqttDriver
{
public:
    struct Config {
        std::string host;
        uint16_t port;
        uint32_t connectTimeoutMs; // Passed to TCPIP::open
        bool useTls;
        std::string caCertPath;
        std::string clientCertPath;
        std::string clientKeyPath;
        std::string clientId;
        uint8_t keepAlive; // Seconds
        uint8_t qos;       // Default QoS for publish
        bool retain;       // Default Retain flag
    };

    MqttDriver() = default;
    ~MqttDriver();

    // Connection Management
    ICommDriver::Status open(const Config& config);
    void close();
    bool isConnected() const;

    // MQTT Actions
    ICommDriver::Status connect();       // Send CONNECT, wait for CONNACK
    ICommDriver::Status disconnect();    // Send DISCONNECT

    // Publishing
    ICommDriver::Status publish(const std::string& topic, const std::string& payload, uint8_t qos = 0, bool retain = false);

    // Waiting for specific response types (e.g., PUBACK for QoS 1)
    ICommDriver::Status waitForResponse(uint16_t packetId, uint32_t timeoutMs, uint8_t expectedType);

private:
    // Internal State
    std::shared_ptr<TCPIP> m_pTcpip;
    bool m_connected; // TCP level
    bool m_sessionOpen; // MQTT session level
    uint16_t m_nextPacketId;

    // TLS Context
    SSL_CTX* m_sslCtx;
    SSL* m_ssl;

    Config m_config;

    // Helper: Send raw bytes
    ICommDriver::Status sendRaw(const std::vector<uint8_t>& data);

    // Helper: Read one full MQTT packet (Fixed Header + Variable + Payload)
    // This assumes the peer sends one packet at a time and waits for ACK.
    ICommDriver::Status recvPacket(std::vector<uint8_t>& packetOut);

    // Helper: Parse Variable Byte Integer (MQTT spec)
    uint32_t decodeVarInt(const std::vector<uint8_t>& data, size_t& offset) const;

    // Helper: Encode Variable Byte Integer
    std::vector<uint8_t> encodeVarInt(uint32_t value) const;

    // SSL Helpers
    ICommDriver::Status setupTls();
};

#endif // MQTT_DRIVER_HPP
