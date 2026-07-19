#ifndef MQTT_DRIVER_HPP
#define MQTT_DRIVER_HPP

#include "uTcpip.hpp"
#include <memory>
#include <vector>
#include <string>
#include <span>
#include <string_view>
#include <cstdio>

typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;


/**
 * @brief MQTT Client Driver wrapping the generic TCPIP driver.
 *
 * Implements the MQTT v3.1.1 protocol logic on top of the byte-stream TCPIP driver.
 * Handles CONNECT/CONNACK handshake and PUBLISH/PUBACK cycles.
 *
 * Also implements ICommDriver directly (tout_read()/tout_write()/is_open()),
 * passing straight through to the underlying TCPIP socket once a session is
 * open. This mirrors how TCPIP itself implements ICommDriver, and is what
 * lets MqttDriver plug into the same generic CommScriptCommandInterpreter<T>
 * / CommScriptClient<T> machinery used by the TCPIP/UART/etc. plugins for
 * their CMD and SCRIPT commands: MQTT.CMD / MQTT.SCRIPT can send/expect raw
 * bytes on the already-connected MQTT session (e.g. to poke at PUBLISH /
 * SUBSCRIBE frames by hand), the same grammar as TCPIP.CMD / TCPIP.SCRIPT.
 */
class MqttDriver : public ICommDriver
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

    /**
     * @brief Construct with a display label for the GUI comm-dump panel
     * (see describeConnection()). Connection itself is still established via
     * open(config) — this label is supplied separately, matching every other
     * driver in this codebase (e.g. W5500Net, which has the same
     * default-constructor-then-open() shape).
     */
    explicit MqttDriver(std::string strIdentityLabel)
        : m_strIdentityLabel(std::move(strIdentityLabel))
    {
    }

    ~MqttDriver();

    // Connection Management
    ICommDriver::Status open(const Config& config);
    void close();
    bool isConnected() const; // MQTT session-level (CONNACK received)

    // MQTT Actions
    ICommDriver::Status connect();       // Send CONNECT, wait for CONNACK
    ICommDriver::Status disconnect();    // Send DISCONNECT

    // Publishing
    ICommDriver::Status publish(const std::string& topic, const std::string& payload, uint8_t qos = 0, bool retain = false);

    // Waiting for specific response types (e.g., PUBACK for QoS 1)
    ICommDriver::Status waitForResponse(uint16_t packetId, uint32_t timeoutMs, uint8_t expectedType);

    // ICommDriver interface: raw pass-through to the underlying TCPIP
    // socket, for use by CommScriptCommandInterpreter<MqttDriver> /
    // CommScriptClient<MqttDriver> (MQTT.CMD / MQTT.SCRIPT). Available once
    // open()/connect() have succeeded.
    bool is_open() const override; // TCP-level (socket connected)

    ICommDriver::ReadResult tout_read(uint32_t u32ReadTimeout,
                                       std::span<uint8_t> buffer,
                                       const ICommDriver::ReadOptions& options,
                                       std::string_view xtra_params = {}) const override;

    ICommDriver::WriteResult tout_write(uint32_t u32WriteTimeout,
                                         std::span<const uint8_t> buffer,
                                         std::string_view xtra_params = {}) const override;

    /**
     * @brief Describe this connection for the GUI comm-dump panel.
     *
     * xtra_params is forwarded to the underlying TCPIP the same way
     * tout_read()/tout_write() themselves forward it (see class docs) — but
     * TCPIP itself ignores xtra_params (single-peer client), so this always
     * reflects the MQTT session identity: "MQTT <label|clientId> host:port".
     */
    CommDetails describeConnection(std::string_view /*xtra_params*/ = {}) const override
    {
        char label[k_labelSize];
        std::snprintf(label, sizeof(label), "MQTT %s %s:%u",
                      !m_strIdentityLabel.empty() ? m_strIdentityLabel.c_str()
                                                   : m_config.clientId.c_str(),
                      m_config.host.c_str(), m_config.port);
        return commdump_details(CommFamily::NET, label);
    }

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
    std::string m_strIdentityLabel;  ///< GUI comm-dump display label, see describeConnection()

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
