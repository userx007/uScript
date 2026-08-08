#ifndef MQTT_DRIVER_HPP
#define MQTT_DRIVER_HPP

#include "uTcpip.hpp"
#include "ICommDriver.hpp"
#include "mqtt_protocol.hpp"

#include <memory>
#include <string>
#include <vector>
#include <span>
#include <unordered_map>
#include <chrono>
#include <cstdio>

typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;

/**
 * @brief The "driver side" — everything MQTT-implementation-specific lives
 * here, so MqttPlugin (mqtt_plugin.hpp) can stay a thin, high-level shell:
 * CONFIG storage, an INFO summary, and wiring this driver into
 * ucmdexec::generic_cmd()/generic_script(), the same shared mechanism
 * every other comm-driver plugin (UART, TCPIP, KVCAN, ...) uses.
 *
 * Three-way split, per plugin architecture guideline #12:
 *   - **Protocol side**: `MqttProtocol` (mqtt_protocol.hpp) — pure MQTT
 *     v3.1.1 packet encode/decode, no I/O, driver-agnostic. Owned here as
 *     m_protocol (persists for the life of the session, since packet ids
 *     are assigned from one running sequence).
 *   - **Driver side**: this class. Implements `ICommDriver` (so it can be
 *     `CommScriptCommandInterpreter`/`CommScriptClient`'s `DriverT`) and
 *     depends on the real, already-existing `TCPIP` driver
 *     (src/lib/drivers/tcpip) — unmodified, wrapped, never reimplemented.
 *     Owns the TLS layer (since TCPIP has no TLS awareness of its own —
 *     see m_SetupTls(), which attaches OpenSSL directly to TCPIP's socket
 *     via nativeHandle()), the CONNECT/CONNACK session handshake (open()),
 *     MQTT packet framing over the wire (m_SendPacket()/m_ReadPacket()),
 *     and — crucially — the GUI comm-dump reporting for every physical
 *     packet exchanged (see send()/receive()'s doc comments for why that
 *     lives here rather than being left to the interpreter's own
 *     automatic dump).
 *   - **Plugin side**: `MqttPlugin` — stores CONFIG, builds this class's
 *     Config from it, and supplies send()/receive() to
 *     `ucmdexec::generic_cmd()`/`generic_script()` as the `pfsend`/`pfrecv`
 *     override. That's the entire plugin; every MQTT-specific behaviour is
 *     implemented here instead.
 *
 * send()/receive() are written to match `CommScriptCommandInterpreter<
 * MqttDriver>::SendFunc`/`RecvFunc` exactly, so the plugin's lambdas are
 * one-liners (`[](..., shpDriver, ...) { return shpDriver->send(...); }`)
 * with no plugin state captured at all — see mqtt_plugin.cpp.
 */
class MqttDriver : public ICommDriver
{
public:
    struct Config {
        // Transport
        std::string host;
        uint16_t port = 1883;
        uint32_t connectTimeoutMs = 5000;
        bool useTls = false;
        std::string caCertPath;     // empty: server certificate chain is NOT verified — see m_SetupTls()
        std::string clientCertPath; // both cert+key set: mutual TLS (Mosquitto's require_certificate)
        std::string clientKeyPath;

        // CONNECT (session) parameters
        std::string clientId;
        std::string username;   // empty => CONNECT carries no credentials — see MqttProtocol::buildConnect()
        std::string password;
        std::string willTopic;  // empty => no Will Flag set
        std::string willPayload;
        uint8_t willQos = 0;
        bool willRetain = false;
        bool cleanSession = true;
        uint16_t keepAlive = 60; // seconds

        // Default PUBLISH parameters (topic/payload come from the command line itself)
        uint8_t qos = 0;
        bool retain = false;

        // Whether the standalone "<" receive stores "topic:payload" or just "payload"
        bool receiveIncludeTopic = false;
    };

    explicit MqttDriver(Config config);
    ~MqttDriver();

    /**
     * @brief Opens the real TCPIP connection, completes the TLS handshake
     * (if configured), then sends CONNECT and waits for CONNACK. Called
     * once by MqttPlugin's factory lambda before this driver is ever
     * handed to the interpreter — every other method assumes this already
     * succeeded.
     */
    bool open();
    void close();

    // ---- ICommDriver ----
    // Real physical I/O never actually flows through these when this
    // driver is used via send()/receive() below (which is how
    // MqttPlugin always uses it) — they exist because
    // CommScriptCommandInterpreter<MqttDriver> requires DriverT to
    // implement ICommDriver, and may itself call is_open()/
    // describeConnection() for its own bookkeeping regardless of pfsend/
    // pfrecv being set. tout_write()/tout_read() are thin passthroughs to
    // the real TCPIP driver for exactly that reason — completeness, not
    // an alternate code path this class relies on.
    bool is_open() const override;
    CommDetails describeConnection(std::string_view xtra_params = {}) const override;
    ICommDriver::WriteResult tout_write(uint32_t u32WriteTimeout, std::span<const uint8_t> buffer,
                                         std::string_view xtra_params = {}) const override;
    ICommDriver::ReadResult tout_read(uint32_t u32ReadTimeout, std::span<uint8_t> buffer,
                                       const ICommDriver::ReadOptions& options,
                                       std::string_view xtra_params = {}) const override;

    /**
     * @brief The "intermediary layer": parses the MQTT.CMD argument text in
     * dataSpan (e.g. "SUBSCRIBE sensors/temp 1"), determines the MQTT
     * command, builds and sends the corresponding packet, and reports it to
     * the GUI comm-dump panel — see m_SendPacket(). Matches
     * `CommScriptCommandInterpreter<MqttDriver>::SendFunc`'s exact
     * signature, so MqttPlugin passes this straight through as `pfsend`
     * (see mqtt_plugin.cpp): supplying pfsend/pfrecv suppresses the
     * interpreter's own automatic dump (which would otherwise show the
     * pre-parse argument text, not real wire bytes), so this class must —
     * and does — dump the accurate replacement itself.
     */
    ICommDriver::WriteResult send(uint32_t u32WriteTimeout, std::span<const uint8_t> dataSpan,
                                   std::string_view xtra_params) const;

    /**
     * @brief The other half: waits for whatever acknowledgement the
     * preceding send() call on this same command line made outstanding
     * (PUBACK/PUBCOMP/SUBACK/UNSUBACK/PONG), or — for a standalone
     * "MQTT.CMD <" — waits for the next incoming PUBLISH instead. See
     * m_TlAwaitingAck's doc comment (mqtt_driver.cpp) for how the two
     * cases are told apart. Matches `RecvFunc`'s exact signature.
     */
    ICommDriver::ReadResult receive(uint32_t u32ReadTimeout, std::span<uint8_t> dataSpan,
                                     const ICommDriver::ReadOptions& options, std::string_view xtra_params) const;

private:
    Config m_config;
    std::shared_ptr<TCPIP> m_pTcpip;
    mutable MqttProtocol m_protocol;
    bool m_sessionEstablished = false;

    // TLS — layered directly onto m_pTcpip's socket via nativeHandle().
    SSL_CTX* m_sslCtx = nullptr;
    SSL* m_ssl = nullptr;
    bool m_SetupTls();

    // Physical I/O: routes through SSL if set up, otherwise straight to
    // m_pTcpip->tout_write()/tout_read().
    ICommDriver::Status m_PhysicalSend(std::span<const uint8_t> data, uint32_t timeoutMs) const;
    ICommDriver::Status m_PhysicalRecv(std::span<uint8_t> buffer, uint32_t timeoutMs, size_t& outBytesRead) const;

    // Sends one complete MQTT packet (built by MqttProtocol) via
    // m_PhysicalSend(), reports it to the GUI comm-dump panel on success,
    // and refreshes m_lastActivity (see m_EnsureKeepAlive()).
    ICommDriver::Status m_SendPacket(const std::vector<uint8_t>& packet, std::string_view xtra_params) const;

    // Reads one complete MQTT packet (fixed header, Remaining Length,
    // payload) via m_PhysicalRecv() and reports it to the GUI comm-dump
    // panel as a single row on success — a raw byte-by-byte dump of MQTT's
    // variable-length framing would be far noisier than useful, so this
    // dumps the fully-reassembled packet once. timeoutMs bounds only the
    // wait for the packet's first byte; once a packet has started
    // arriving, the rest is read with its own short fixed timeout (a stall
    // mid-packet is a broken-connection problem, not a "nothing to receive
    // yet" one).
    ICommDriver::Status m_ReadPacket(std::vector<uint8_t>& packetOut, uint32_t timeoutMs, std::string_view xtra_params) const;

    // Reads packets (via m_ReadPacket()) until one of type expectedType
    // carrying packet id expectedPacketId turns up, or timeoutMs elapses —
    // anything else read meanwhile is logged and discarded.
    bool m_WaitForAckPacket(uint8_t expectedType, uint16_t expectedPacketId,
                             uint32_t timeoutMs, std::vector<uint8_t>& outPacket, std::string_view xtra_params) const;

    // If at least (Config::keepAlive * 0.8) seconds have passed since the
    // last byte this driver wrote to the wire, sends a PINGREQ and waits
    // for PINGRESP — called from the standalone-receive path in receive(),
    // the one call expected to sit idle for a long time.
    bool m_EnsureKeepAlive(std::string_view xtra_params) const;
    mutable std::chrono::steady_clock::time_point m_lastActivity;

    // ---- Intermediary layer: MQTT.CMD argument decomposition ----
    // Tokenizes on whitespace only (the shared CommScriptCommandValidator
    // grammar requires an unquoted field to contain no '"' at all, so this
    // layer can't support its own embedded quoting on top of that). Also
    // strips a trailing NUL byte first: the interpreter's STRING_RAW
    // conversion (ustring::stringToVector()) appends one by default, and an
    // MQTT topic/payload string must not contain an embedded NUL (MQTT
    // 3.1.1 §1.5.3) — Mosquitto (and any spec-compliant broker) rejects a
    // packet containing one as malformed and drops the connection.
    static void m_TokenizeArgs(std::span<const uint8_t> dataSpan, std::vector<std::string>& outTokens);

    // MQTT sub-command handlers (the "specific callback associated to that
    // command"). Each builds and sends its packet via m_protocol/
    // m_SendPacket(), and — for a command whose success is confirmed by an
    // acknowledgement — records what receive() should wait for next (see
    // receive()'s doc comment). Returns false on bad arguments or a send
    // failure.
    bool m_HandleSubscribe(const std::vector<std::string>& args, std::string_view xtra_params) const;
    bool m_HandleUnsubscribe(const std::vector<std::string>& args, std::string_view xtra_params) const;
    bool m_HandlePing(const std::vector<std::string>& args, std::string_view xtra_params) const;
    bool m_HandlePublish(const std::vector<std::string>& args, std::string_view xtra_params) const;

    using MqttSubCmdHandler = bool (MqttDriver::*)(const std::vector<std::string>&, std::string_view) const;
    std::unordered_map<std::string, MqttSubCmdHandler> m_mapMqttCmds;

    // The "<" side: waits for the next incoming PUBLISH, acknowledges it
    // per its QoS, and writes "topic:payload" or "payload" (per
    // Config::receiveIncludeTopic) into buffer. Called from receive() when
    // no ack is pending — see its doc comment.
    ICommDriver::ReadResult m_DoStandaloneReceive(uint32_t timeoutMs, std::span<uint8_t> buffer, std::string_view xtra_params) const;
};

#endif // MQTT_DRIVER_HPP
