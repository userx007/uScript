#ifndef MQTT_COMMAND_DRIVER_HPP
#define MQTT_COMMAND_DRIVER_HPP

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
 * @brief The "decorator driver" that connects the protocol side to the
 * driver side — the only new driver this plugin adds.
 *
 * Three-way split, per plugin architecture guideline #12:
 *   - **Protocol side**: `MqttProtocol` (mqtt_protocol.hpp) — pure MQTT
 *     v3.1.1 packet encode/decode, no I/O, driver-agnostic. Owned by this
 *     class (m_protocol).
 *   - **Driver side**: `TCPIP` (src/lib/drivers/tcpip) — the real,
 *     already-existing Ethernet driver. Unmodified, wrapped, never
 *     reimplemented. This class holds one as m_pInner and is the only
 *     thing that ever calls its tout_write()/tout_read().
 *   - **This class**: implements the exact same `ICommDriver` interface as
 *     m_pInner, so MQTT.CMD/MQTT.SCRIPT can keep using the standard
 *     `CommScriptCommandInterpreter<MqttCommandDriver>` /
 *     `CommScriptClient<MqttCommandDriver>` machinery (via
 *     `ucmdexec::generic_cmd`/`generic_script`, exactly like every other
 *     comm-driver plugin) — including its GUI comm-dump integration — while
 *     transparently inserting MQTT protocol handling in between. From the
 *     interpreter's point of view this is just another driver; the MQTT
 *     awareness is entirely internal.
 *
 * tout_write() is where "based on the command, put the corresponding data
 * into the send buffer and call the driver to send it" happens: the buffer
 * handed in is the raw MQTT.CMD argument TEXT (e.g. "SUBSCRIBE sensors/temp
 * 1" or "PUBLISH OPEN actuators/valve3/cmd" — see m_TokenizeArgs()), which
 * this class parses to find the MQTT command keyword, dispatches to that
 * command's handler (m_mapMqttCmds), which asks m_protocol to build the
 * actual packet bytes and calls m_pInner->tout_write() (via m_sendPacket(),
 * possibly through TLS — see "TLS" below) to physically send them.
 *
 * tout_read() is the other half: for `MQTT.CMD > ... | expected` it waits
 * for whatever acknowledgement the preceding tout_write() call on this same
 * command line made outstanding (PUBACK/PUBCOMP/SUBACK/UNSUBACK/PONG); for
 * a standalone `MQTT.CMD <` it waits for the next incoming PUBLISH instead
 * — see the "Command dispatch" section below for how these two cases are
 * told apart.
 *
 * -------------------------------------------------------------------------
 * TLS
 * -------------------------------------------------------------------------
 * TCPIP has no TLS awareness (plain sockets only), so when Config::useTls
 * is set this class layers TLS directly onto m_pInner's connected socket —
 * SSL_set_fd(m_pInner->nativeHandle()) — and every physical send/receive
 * (m_physicalSend()/m_physicalRecv()) transparently routes through
 * SSL_write()/SSL_read() instead of m_pInner->tout_write()/tout_read()
 * once the handshake (m_setupTls(), run from ensureSession()) has
 * completed. TCPIP itself is completely unaware this is happening.
 *
 * -------------------------------------------------------------------------
 * Command dispatch and the ack-pending handoff between tout_write()/tout_read()
 * -------------------------------------------------------------------------
 * ICommDriver's contract requires tout_write()/tout_read() to be const, so
 * "what ack (if any) is this tout_read() call meant to wait for" is carried
 * via `thread_local` state (see mqtt_command_driver.cpp) rather than a
 * plain mutable member: MQTT.CMD < & runs on its own background thread
 * (concurrently with other MQTT.CMD activity on this same, persistent,
 * driver instance — see mqtt_plugin.hpp's "Session lifetime"), and
 * thread_local storage is what keeps that background receive loop from
 * racing a foreground `> ... | ...` pair's own pending-ack bookkeeping.
 *
 * Because tout_write() has no visibility into whether a paired tout_read()
 * call will actually follow (that depends on whether the script line has a
 * trailing `| ...`, which only the interpreter's grammar sees), a QoS 1/2
 * PUBLISH whose line omits the expected-ack pipe leaves that PUBACK/PUBCOMP
 * unread on the wire; the next `MQTT.CMD <` on this driver will then
 * consume it (returning that confirmation text once instead of waiting for
 * a live message) rather than anything worse — a self-limiting one-time
 * mismatch, not persistent corruption. Always pair a QoS>0 `PUBLISH` with
 * its `| PUBACK` / `| PUBCOMP` to avoid it.
 */
class MqttCommandDriver : public ICommDriver
{
public:
    struct Config {
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

        // TLS — layered onto the inner TCPIP driver's socket, see class doc comment
        bool useTls = false;
        std::string host; // needed for SNI / hostname verification, independent of m_pInner's own identity
        std::string caCertPath;
        std::string clientCertPath;
        std::string clientKeyPath;
    };

    MqttCommandDriver(std::shared_ptr<TCPIP> pInner, Config config, std::string strIdentityLabel = {});
    ~MqttCommandDriver();

    /**
     * @brief Sets up TLS (if configured) over m_pInner's socket, then sends
     * CONNECT and waits for CONNACK. Idempotent — a no-op if the session is
     * already established. Must succeed before tout_write()/tout_read() are
     * used for anything MQTT-specific.
     */
    bool ensureSession();

    /**
     * @brief Send a clean DISCONNECT (best-effort — not acknowledged by the
     * broker) and mark the session no longer established. Does not close
     * the inner TCPIP driver; the caller owns that. A clean DISCONNECT is
     * what prevents the broker from publishing this session's Last Will —
     * see MqttProtocol::buildConnect()'s willTopic doc comment.
     */
    void disconnect();


    bool is_open() const override; // inner driver open AND MQTT session established

    /// See class doc comment's "tout_write()" paragraph.
    ICommDriver::WriteResult tout_write(uint32_t u32WriteTimeout,
                                         std::span<const uint8_t> buffer,
                                         std::string_view xtra_params = {}) const override;

    /// See class doc comment's "tout_read()" paragraph.
    ICommDriver::ReadResult tout_read(uint32_t u32ReadTimeout,
                                       std::span<uint8_t> buffer,
                                       const ICommDriver::ReadOptions& options,
                                       std::string_view xtra_params = {}) const override;

    CommDetails describeConnection(std::string_view xtra_params = {}) const override
    {
        return m_pInner->describeConnection(xtra_params);
    }

private:
    std::shared_ptr<TCPIP> m_pInner; // the real driver — see class doc comment
    Config m_config;
    std::string m_strIdentityLabel;

    mutable MqttProtocol m_protocol; // protocol side, owned here — see class doc comment
    bool m_sessionEstablished = false;

    SSL_CTX* m_sslCtx = nullptr;
    SSL* m_ssl = nullptr;
    bool m_setupTls();

    // Physical I/O: routes through SSL if TLS is set up, otherwise straight
    // to m_pInner->tout_write()/tout_read() — this is the actual "call the
    // real comm driver and send it" step every packet send/receive in this
    // class goes through.
    ICommDriver::Status m_physicalSend(std::span<const uint8_t> data, uint32_t timeoutMs) const;
    ICommDriver::Status m_physicalRecv(std::span<uint8_t> buffer, uint32_t timeoutMs, size_t& outBytesRead) const;

    // Sends one complete MQTT packet (built by MqttProtocol) via m_physicalSend(),
    // and refreshes m_lastActivity on success (see m_EnsureKeepAlive()).
    ICommDriver::Status m_sendPacket(const std::vector<uint8_t>& packet) const;

    // Reads one complete MQTT packet (fixed header, Remaining Length,
    // payload) via m_physicalRecv(). timeoutMs bounds only the wait for the
    // packet's first byte; once a packet has started arriving, the rest is
    // read with its own short fixed timeout (a stall mid-packet is a
    // broken-connection problem, not a "nothing to receive yet" one).
    ICommDriver::Status m_readPacket(std::vector<uint8_t>& packetOut, uint32_t timeoutMs) const;

    // Reads packets (via m_readPacket()) until one of type expectedType
    // carrying packet id expectedPacketId turns up, or timeoutMs elapses —
    // anything else read meanwhile is logged and discarded.
    bool m_WaitForAckPacket(uint8_t expectedType, uint16_t expectedPacketId,
                             uint32_t timeoutMs, std::vector<uint8_t>& outPacket) const;

    // If at least (Config::keepAlive * 0.8) seconds have passed since the
    // last byte this driver wrote to the wire, sends a PINGREQ and waits
    // for PINGRESP — called from the standalone-receive path in
    // tout_read(), the one call expected to sit idle for a long time.
    bool m_EnsureKeepAlive() const;
    mutable std::chrono::steady_clock::time_point m_lastActivity;

    // ---- Intermediary layer: MQTT.CMD argument decomposition ----
    // Tokenizes on whitespace only (the shared CommScriptCommandValidator
    // grammar requires an unquoted field to contain no '"' at all — see
    // this class's .cpp for why embedded quoting isn't available here).
    static void m_TokenizeArgs(const std::string& text, std::vector<std::string>& outTokens);

    // MQTT sub-command handlers (the "specific callback associated to that
    // command"). Each builds and sends its packet via m_protocol/
    // m_sendPacket(), and — for a command whose success is confirmed by an
    // acknowledgement — records what tout_read() should wait for next (see
    // class doc comment's "Command dispatch" section). Returns false on bad
    // arguments or a send failure.
    bool m_HandleSubscribe(const std::vector<std::string>& args) const;
    bool m_HandleUnsubscribe(const std::vector<std::string>& args) const;
    bool m_HandlePing(const std::vector<std::string>& args) const;
    bool m_HandlePublish(const std::vector<std::string>& args) const;

    using MqttSubCmdHandler = bool (MqttCommandDriver::*)(const std::vector<std::string>&) const;
    std::unordered_map<std::string, MqttSubCmdHandler> m_mapMqttCmds;

    // The "<" side: waits for the next incoming PUBLISH, acknowledges it
    // per its QoS, and writes "topic:payload" or "payload" (per
    // includeTopicOnReceive) into buffer. Called from tout_read() when no
    // ack is pending (see class doc comment).
    ICommDriver::ReadResult m_DoStandaloneReceive(uint32_t timeoutMs, std::span<uint8_t> buffer) const;

public:
    // Whether m_DoStandaloneReceive() stores "topic:payload" or just
    // "payload" — set by the plugin from its "it=" CONFIG key.
    bool includeTopicOnReceive = false;
};

#endif // MQTT_COMMAND_DRIVER_HPP
