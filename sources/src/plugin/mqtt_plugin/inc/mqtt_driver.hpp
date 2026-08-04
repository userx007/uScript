#ifndef MQTT_DRIVER_HPP
#define MQTT_DRIVER_HPP

#include "uTcpip.hpp"
#include <memory>
#include <vector>
#include <string>
#include <span>
#include <string_view>
#include <cstdio>
#include <chrono>

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
 *
 * subscribe()/receiveMessage() are the MQTT.SUBSCRIBE/MQTT.RECEIVE half of
 * the driver, deliberately kept separate from the connect()/tout_write()/
 * tout_read() trio above: a subscription is inherently long-lived (it must
 * survive across many RECEIVE calls, including ones made from a background
 * thread via the core script engine's "name ?= MQTT.RECEIVE &" — see
 * src/script/core/README.md's "Threaded variable macros" section), whereas
 * MQTT.CMD/SCRIPT each use a fresh, short-lived MqttDriver instance per
 * invocation (see MqttPlugin::m_OpenDriver()). MqttPlugin therefore keeps a
 * second, persistent MqttDriver instance specifically for subscribe()/
 * receiveMessage(), opened by the first MQTT.SUBSCRIBE call and kept alive
 * across subsequent MQTT.SUBSCRIBE/MQTT.RECEIVE calls (see
 * MqttPlugin::m_OpenSubscriberDriver()) — publishing (tout_write/tout_read)
 * and subscribing never share a connection.
 *
 * -------------------------------------------------------------------------
 * Session-level features (CONNECT payload)
 * -------------------------------------------------------------------------
 * Config additionally covers everything else a CONNECT packet can carry:
 * username/password (Mosquitto's password_file auth), a Last Will and
 * Testament (delivered by the broker to willTopic if this client
 * disconnects uncleanly — the standard way to test failure/offline
 * detection against Mosquitto), and Clean Session (false requests a
 * persistent broker-side session so QoS 1/2 subscriptions and queued
 * messages survive a reconnect — see subscribe()'s doc comment for the
 * interaction with QoS).
 *
 * -------------------------------------------------------------------------
 * Keepalive
 * -------------------------------------------------------------------------
 * MQTT requires a client to send *something* at least once every
 * Config::keepAlive seconds or the broker is entitled to close the
 * connection. Every write through this driver (publish, subscribe,
 * ack, ...) resets that clock; when nothing else is due, m_ensureKeepAlive()
 * — called at the top of receiveMessage()'s wait, the one call this driver
 * expects to block for a long time between activity — sends a PINGREQ and
 * waits for PINGRESP itself so a long-idle MQTT.SUBSCRIBE/MQTT.RECEIVE loop
 * does not silently get dropped by the broker.
 */
class MqttDriver : public ICommDriver
{
public:
    struct Config {
        std::string host;
        uint16_t port;
        uint32_t connectTimeoutMs; // Passed to TCPIP::open
        bool useTls;
        std::string caCertPath;     // empty: certificate chain is NOT verified (self-signed test brokers) — see setupTls()
        std::string clientCertPath; // both cert+key set: mutual TLS (Mosquitto's require_certificate)
        std::string clientKeyPath;
        std::string clientId;
        uint8_t keepAlive; // Seconds
        uint8_t qos;       // Default QoS for publish
        bool retain;       // Default Retain flag

        // Authentication (Mosquitto password_file / plugin auth). username
        // empty => no credentials sent at all (User Name Flag/Password Flag
        // both cleared). A password without a username is not valid MQTT
        // 3.1.1 and is ignored (logged) — see connect().
        std::string username;
        std::string password;

        // Last Will and Testament: willTopic empty => no Will Flag set, and
        // willPayload/willQos/willRetain are then unused. Non-empty
        // willTopic => the broker publishes willPayload to willTopic
        // (honouring willQos/willRetain) if this client's TCP connection
        // drops without a clean DISCONNECT — the standard way to make an
        // MQTT client's liveness observable to other subscribers.
        std::string willTopic;
        std::string willPayload;
        uint8_t willQos = 0;
        bool willRetain = false;

        // true (default, matches previous hardcoded behaviour): broker
        // discards any prior session state for this clientId and starts
        // fresh. false: requests a persistent session — subscriptions and
        // undelivered QoS 1/2 messages survive this client disconnecting
        // and reconnecting with the same clientId. A stable (non-empty,
        // reused) clientId is required for false to have any effect.
        bool cleanSession = true;
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

    /**
     * @brief Send SUBSCRIBE for one topic filter and wait for SUBACK.
     * @param topic Topic filter (may contain MQTT wildcards '+'/'#').
     * @param qos   Requested QoS (0-2) — the broker may grant a lower QoS
     *              than requested; the granted value is logged but not
     *              otherwise surfaced (receiveMessage() always honours
     *              whatever QoS the broker actually used for each message,
     *              read from that message's own fixed header).
     * @return SUCCESS once SUBACK confirms the subscription (any granted
     *         QoS 0-2), or a failure Status — including PROTOCOL_ERROR if
     *         the broker's SUBACK return code is 0x80 (subscription refused).
     *
     * May be called more than once on the same driver instance to
     * subscribe to additional topics — each call sends its own SUBSCRIBE/
     * waits for its own SUBACK; MQTT itself has no notion of "batch
     * subscribe" beyond what one SUBSCRIBE packet's topic list carries, and
     * this driver only ever puts one topic filter in each packet it sends.
     */
    ICommDriver::Status subscribe(const std::string& topic, uint8_t qos);

    /**
     * @brief Send UNSUBSCRIBE for one topic filter and wait for UNSUBACK.
     * @param topic Topic filter exactly as passed to a prior subscribe()
     *              call (MQTT matches UNSUBSCRIBE filters literally against
     *              what was subscribed, not against incoming topic names).
     * @return SUCCESS once UNSUBACK confirms removal, or a failure Status.
     *         MQTT 3.1.1's UNSUBACK carries no per-topic result code (unlike
     *         SUBACK) — receiving it at all means the broker processed the
     *         request, regardless of whether that topic was actually
     *         subscribed.
     */
    ICommDriver::Status unsubscribe(const std::string& topic);

    /**
     * @brief Send PINGREQ and wait for PINGRESP.
     *
     * Normally invoked automatically (see m_ensureKeepAlive()) rather than
     * called directly, but exposed as a driver-level primitive — and as the
     * MQTT.PING plugin command — for scripts that want to explicitly probe
     * "is this session still alive" without publishing or subscribing
     * anything, or that manage their own keepalive timing.
     */
    ICommDriver::Status ping() const;

    /**
     * @brief Wait up to timeoutMs for one incoming PUBLISH from the broker
     *        on any subscribed topic, acknowledging it (PUBACK / PUBREC-
     *        wait PUBREL-PUBCOMP) as its QoS requires before returning.
     * @param[out] outTopic   the message's topic (not necessarily the exact
     *                        filter subscribe() was called with, if that
     *                        filter used a wildcard)
     * @param[out] outPayload the message's payload, as received (no
     *                        decoding/validation of its contents)
     * @return SUCCESS with outTopic/outPayload filled in, READ_TIMEOUT if
     *         nothing arrived within timeoutMs, or another Status on a
     *         protocol/connection error. Non-PUBLISH packets received while
     *         waiting (there shouldn't be any, on a connection used only
     *         for subscriptions, but a misbehaving broker is not this
     *         driver's problem to enforce) are logged and skipped rather
     *         than treated as an error.
     *
     * Also responsible for this connection's keepalive: since this is the
     * one call expected to sit idle for a long time between messages (a
     * MQTT.SUBSCRIBE session waiting on MQTT.RECEIVE), it sends a PINGREQ
     * itself first whenever the keepalive interval is close to elapsing —
     * see m_ensureKeepAlive().
     */
    ICommDriver::Status receiveMessage(uint32_t timeoutMs, std::string& outTopic, std::string& outPayload) const;

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

    // TLS Context. When m_config.useTls, all wire I/O (connect()'s CONNECT/
    // CONNACK, publish/subscribe/receive, ...) goes through m_ssl via
    // m_sendRaw()/m_recvRaw() instead of straight to m_pTcpip — see
    // setupTls().
    SSL_CTX* m_sslCtx;
    SSL* m_ssl;

    Config m_config;
    std::string m_strIdentityLabel;  ///< GUI comm-dump display label, see describeConnection()

    // Keepalive: timestamp of the last byte successfully written to the
    // wire (by m_sendRaw(), so every packet type updates it uniformly).
    // mutable for the same reason as the ack-tracking members above:
    // touched from const call paths (tout_write() -> m_publish(), and
    // receiveMessage()'s m_ensureKeepAlive() check).
    mutable std::chrono::steady_clock::time_point m_lastActivity;

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
    // that packet type; see m_waitForPacketType()'s definition). pOutPacket,
    // if non-null, receives the matched packet's raw bytes — subscribe()
    // uses this to inspect SUBACK's return code, which sits right after the
    // packet id this function already validates.
    ICommDriver::Status m_waitForPacketType(uint8_t expectedType, uint16_t expectedPacketId,
                                            uint32_t timeoutMs, std::vector<uint8_t>* pOutPacket = nullptr) const;

    // Helper: read one full MQTT packet (Fixed Header + Variable + Payload).
    // timeoutMs bounds only the wait for the packet's FIRST byte — once a
    // packet has started arriving, the rest (Remaining Length bytes, then
    // Payload) is read with its own short, fixed per-read timeout
    // (kPacketContinuationTimeoutMs, mqtt_driver.cpp), since a message that
    // starts arriving but then stalls mid-transmission is a different
    // failure (a broken connection) than "nothing new to receive yet" —
    // the caller's timeoutMs is about the latter.
    ICommDriver::Status recvPacket(std::vector<uint8_t>& packetOut, uint32_t timeoutMs) const;

    // Helper: Parse Variable Byte Integer (MQTT spec)
    uint32_t decodeVarInt(const std::vector<uint8_t>& data, size_t& offset) const;

    // Helper: Encode Variable Byte Integer
    std::vector<uint8_t> encodeVarInt(uint32_t value) const;

    // Helper: send the subscriber-side acknowledgement an incoming PUBLISH
    // of the given qos/packetId requires: nothing for QoS 0, PUBACK for
    // QoS 1, or PUBREC-then-wait-for-the-broker's-PUBREL-then-PUBCOMP for
    // QoS 2 — the mirror image of m_waitForAck()'s publisher-side QoS 2
    // handshake (there, we send PUBREL and wait for PUBCOMP; here, the
    // broker sends PUBREL and we send PUBCOMP).
    ICommDriver::Status m_ackIncomingPublish(uint8_t qos, uint16_t packetId, uint32_t timeoutMs) const;

    // Helper: if at least (Config::keepAlive * 0.8) seconds have passed
    // since the last byte we wrote to the wire, send a PINGREQ and wait for
    // PINGRESP (bounded by timeoutMs) before the caller proceeds to its own
    // (possibly long) wait. keepAlive == 0 disables this (a keepAlive of 0
    // is itself valid MQTT for "no keepalive"). Called from
    // receiveMessage() — see its doc comment.
    ICommDriver::Status m_ensureKeepAlive(uint32_t timeoutMs) const;

    // Low-level I/O: every packet send/receive in this driver goes through
    // these two rather than m_pTcpip directly, so that useTls transparently
    // routes the exact same protocol logic over SSL_write()/SSL_read()
    // instead of TCPIP::tout_write()/tout_read() — see setupTls(). Both
    // also keep m_lastActivity current for m_ensureKeepAlive().
    ICommDriver::Status m_sendRaw(uint32_t timeoutMs, std::span<const uint8_t> buffer) const;
    ICommDriver::Status m_recvRaw(uint32_t timeoutMs, std::span<uint8_t> buffer, size_t& bytesRead) const;

    // SSL Helpers
    ICommDriver::Status setupTls();
};

#endif // MQTT_DRIVER_HPP
