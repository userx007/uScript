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
 * Implements the MQTT v3.1.1 protocol logic on top of the byte-stream TCPIP
 * driver: CONNECT/CONNACK (see connect()), and PUBLISH plus its QoS
 * acknowledgement chain through the ICommDriver surface itself:
 *
 *   - tout_write(buffer, xtra_params) sends buffer as the PUBLISH payload to
 *     the topic named by xtra_params (required — there is no per-connection
 *     default topic, since a single MQTT session routinely publishes to many
 *     topics). QoS and retain come from Config (see open()), not from the
 *     call. tout_write() does NOT wait for any acknowledgement itself — for
 *     QoS 1/2 it only records which ack is now outstanding; call tout_read()
 *     to actually wait for it. This split mirrors the "> payload | expected"
 *     send-then-expect shape every other CMD/SCRIPT-driven plugin
 *     (TCPIP/UART/KVCAN/...) already uses, and lets a single MQTT.CMD line
 *     do both, e.g. `MQTT.CMD > H"48656C6C6F" ~ sensors/temp | PUBACK`.
 *
 *   - tout_read() waits for whatever tout_write() just made outstanding:
 *     nothing (QoS 0 — returns immediately, SUCCESS, 0 bytes: there is
 *     genuinely no acknowledgement to wait for), PUBACK (QoS 1), or the
 *     full PUBREC -> PUBREL -> PUBCOMP handshake (QoS 2, driven internally —
 *     the PUBREL is sent for you). On success the buffer receives a short
 *     ASCII confirmation ("PUBACK"/"PUBCOMP"), so a script can assert it
 *     with the same `| expected_text` syntax used everywhere else.
 *     xtra_params is unused here (nothing to address — the pending ack is
 *     tied to the packet id tout_write() just assigned, not a topic).
 *
 * This lets MQTT.CMD / MQTT.SCRIPT reuse the same generic
 * CommScriptCommandInterpreter<T> / CommScriptClient<T> machinery as
 * TCPIP.CMD / TCPIP.SCRIPT, and is the only way to publish — there is no
 * separate PUBLISH-style command.
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

    // ICommDriver interface — see class doc comment above for the exact
    // publish/acknowledgement contract. Available once open()/connect()
    // have succeeded.
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
     * Always reflects the MQTT session identity: "MQTT <label|clientId>
     * host:port" — xtra_params is the publish topic (tout_write()) or
     * unused (tout_read()), neither of which changes the connection's own
     * identity, so it's intentionally not part of this label.
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

    // Assigns packet ids for QoS 1/2 PUBLISH packets. mutable: bumped by
    // m_publish(), called from tout_write(), which is const (ICommDriver's
    // contract) — same reasoning as the ack-tracking members below.
    mutable uint16_t m_nextPacketId;

    // Set by tout_write() for QoS 1/2, consumed by tout_read(); see the
    // class doc comment. mutable because tout_write()/tout_read() are
    // const (ICommDriver's contract), same reasoning as every other
    // driver's connection-state members in this codebase.
    mutable bool     m_bAwaitingAck    = false;
    mutable uint16_t m_pendingPacketId = 0;
    mutable uint8_t  m_pendingQos      = 0;

    // TLS Context
    SSL_CTX* m_sslCtx;
    SSL* m_ssl;

    Config m_config;
    std::string m_strIdentityLabel;  ///< GUI comm-dump display label, see describeConnection()

    // Helper: build and send one PUBLISH packet for (topic, payload) using
    // Config's qos/retain. On success, *pPacketId receives the packet id
    // assigned for QoS 1/2 (undefined/unused for QoS 0). Does not wait for
    // any acknowledgement — that is tout_read()'s job (see class doc comment).
    // const: called from tout_write(), which ICommDriver requires be const;
    // all state it touches (m_nextPacketId, m_bAwaitingAck/m_pendingPacketId/
    // m_pendingQos) is mutable for exactly this reason.
    ICommDriver::Status m_publish(const std::string& topic, const std::string& payload, uint16_t* pPacketId) const;

    // Helper: wait for the acknowledgement chain appropriate to qos/packetId
    // (PUBACK for QoS 1; PUBREC, then send PUBREL, then wait PUBCOMP for
    // QoS 2), writing a short confirmation string into buffer on success.
    ICommDriver::ReadResult m_waitForAck(uint8_t qos, uint16_t packetId, uint32_t timeoutMs,
                                         std::span<uint8_t> buffer) const;

    // Helper: read MQTT packets (via recvPacket()) until one of type
    // expectedType carrying packet id expectedPacketId turns up, or
    // timeoutMs is exceeded — anything else read in the meantime is
    // logged and discarded. Used by m_waitForAck() to pull PUBACK/PUBREC/
    // PUBCOMP off the wire, and by connect() for CONNACK (expectedPacketId
    // is meaningless for CONNACK, which carries none — pass 0, unused for
    // that packet type; see m_waitForPacketType()'s definition).
    ICommDriver::Status m_waitForPacketType(uint8_t expectedType, uint16_t expectedPacketId,
                                            uint32_t timeoutMs) const;

    // Helper: read one full MQTT packet (Fixed Header + Variable + Payload)
    // This assumes the peer sends one packet at a time and waits for ACK.
    ICommDriver::Status recvPacket(std::vector<uint8_t>& packetOut) const;

    // Helper: Parse Variable Byte Integer (MQTT spec)
    uint32_t decodeVarInt(const std::vector<uint8_t>& data, size_t& offset) const;

    // Helper: Encode Variable Byte Integer
    std::vector<uint8_t> encodeVarInt(uint32_t value) const;

    // SSL Helpers
    ICommDriver::Status setupTls();
};

#endif // MQTT_DRIVER_HPP
