#ifndef MQTT_PLUGIN_HPP
#define MQTT_PLUGIN_HPP

#include "uSharedConfig.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "uLogger.hpp"
#include "uNumeric.hpp"
#include "uString.hpp"
#include "uFile.hpp"

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <chrono>

#include "mqtt_protocol.hpp"
#include "mqtt_transport.hpp"

#define MQTT_PLUGIN_VERSION   "2.0.0.0"
#define MQTT_PLUGIN_NAME      "MQTT"

#define MQTT_PLUGIN_COMMANDS_CONFIG_TABLE \
    MQTT_PLUGIN_CMD_RECORD(INFO)          \
    MQTT_PLUGIN_CMD_RECORD(CONFIG)        \
    MQTT_PLUGIN_CMD_RECORD(CMD)           \
    MQTT_PLUGIN_CMD_RECORD(SCRIPT)

/**
 * @brief MQTT plugin — the piece that connects the protocol side to the
 * driver side.
 *
 * Three-way split, per plugin architecture guideline #12:
 *
 *   - **Protocol side** (`MqttProtocol`, mqtt_protocol.hpp): pure MQTT
 *     v3.1.1 packet encode/decode. Builds byte buffers, parses byte
 *     buffers. Never touches a socket and has no notion of "driver" at all
 *     — it would work identically wrapped around any transport.
 *
 *   - **Driver side** (`MqttTransport`, mqtt_transport.hpp): pure byte
 *     transport (TCP + optional TLS). Sends bytes, receives bytes. Has no
 *     notion of "MQTT" at all — it doesn't know a Remaining Length field
 *     exists, let alone how to read one.
 *
 *   - **Plugin side** (this class): owns one `MqttProtocol` + one
 *     `MqttTransport` per loaded plugin instance and wires them together.
 *     For every MQTT.CMD, it decides which command was requested, asks
 *     `MqttProtocol` to build the corresponding packet into a send buffer,
 *     and calls `MqttTransport::send()` to put that buffer on the wire —
 *     then, if that command has an acknowledgement to wait for, calls
 *     `MqttTransport::recv()` (via m_readPacket(), which owns the "keep
 *     recv()-ing until one full packet has arrived" loop — that loop is
 *     transport *orchestration*, not a transport primitive itself, and not
 *     something `MqttProtocol` could do without knowing about drivers) and
 *     asks `MqttProtocol` to decode what came back.
 *
 * -------------------------------------------------------------------------
 * Command surface: everything lives under `MQTT.CMD`
 * -------------------------------------------------------------------------
 * `MQTT.CMD > <mqtt-command> [args...] [| expected]` — publish/subscribe/
 * unsubscribe/ping. `MQTT.CMD <` (or, for a background thread, `MQTT.CMD <
 * &` — the trailing `&` is stripped and handled by the script engine
 * itself before this plugin ever sees it, exactly like every other
 * threaded command; see src/script/core/README.md's "Threaded variable
 * macros" section) — receive one incoming PUBLISH.
 *
 * Unlike TCPIP.CMD/UART.CMD, MQTT.CMD does **not** use the generic
 * CommScriptCommandInterpreter grammar (`> data ~ xtra_params | expected`)
 * — MQTT.CMD doesn't address anything via `~`, and everything after `>` is
 * one plain string this plugin decomposes itself. m_ExecuteCmdString() is
 * that "intermediary layer": it splits the string on the first *unquoted*
 * `|` into a send side and an optional receive side, tokenizes the send
 * side (plain tokens, `"quoted strings"`, `H"hex bytes"`), takes the first
 * token as the MQTT command keyword, validates it and its remaining
 * arguments, and dispatches to that command's handler — SUBSCRIBE/
 * UNSUBSCRIBE/PING/PUBLISH each have one, looked up via m_mapMqttCmds
 * (mirroring how the top-level INFO/CONFIG/CMD/SCRIPT commands are
 * dispatched via m_mapCmds). Avoid an unquoted `|` inside a plain token
 * (a topic or payload that happens to contain one) — wrap it in `"..."`
 * instead, e.g. `PUBLISH "a|b" some/topic`.
 *
 * PUBLISH is the direct replacement for what used to be MQTT.CMD's whole
 * job (topic came from `~`, payload from `>`) — it is now just one more
 * entry in the same command table as SUBSCRIBE/UNSUBSCRIBE/PING, spelled
 * out explicitly: `MQTT.CMD > PUBLISH <payload> <topic>`.
 *
 * Every command handler, on success, fills a short confirmation string
 * ("PUBACK", "PUBCOMP", "SUBACK", "UNSUBACK", "PONG" — empty for a QoS 0
 * PUBLISH, which has no acknowledgement at all) that m_ExecuteCmdString()
 * compares against the optional receive side, e.g.
 * `MQTT.CMD > PUBLISH OPEN actuators/valve3/cmd | PUBACK` both performs the
 * publish (at whatever QoS/retain CONFIG currently has) and asserts it was
 * actually acknowledged at QoS 1 — a QoS 2 publish would need `| PUBCOMP`
 * instead, and asserting anything after a QoS 0 publish always fails,
 * since there is genuinely nothing to acknowledge.
 *
 * -------------------------------------------------------------------------
 * Session lifetime
 * -------------------------------------------------------------------------
 * Unlike the previous fresh-connection-per-CMD design, every MQTT.CMD/
 * MQTT.SCRIPT call now shares one persistent CONNECT/CONNACK session per
 * plugin instance — opened lazily by the first call that needs it
 * (m_EnsureSession()) and kept alive for as long as the plugin is loaded
 * (closed by doCleanup()). This matches a real MQTT client's usage
 * pattern (one client, one session, publish/subscribe/receive freely on
 * it) and is what makes `MQTT.CMD <` meaningful at all: it waits on
 * whatever `MQTT.CMD > SUBSCRIBE ...` calls happened earlier on that same
 * session, including from a background thread.
 */
class MqttPlugin : public PluginInterface
{
public:
    MqttPlugin()
        : m_strVersion(MQTT_PLUGIN_VERSION)
        , m_bIsInitialized(false)
        , m_bIsEnabled(false)
        , m_bIsFaultTolerant(false)
        , m_bIsPrivileged(false)
        , m_strResultData()
        , m_strHost("localhost")
        , m_u16Port(1883)
        , m_bUseTls(false)
        , m_u16Qos(0)
        , m_bRetain(false)
        , m_u32ReadTimeout(5000)
        , m_strClientId()
        , m_bReceiveIncludeTopic(false)
        , m_u8WillQos(0)
        , m_bWillRetain(false)
        , m_bCleanSession(true)
    {
        #define MQTT_PLUGIN_CMD_RECORD(a) m_mapCmds.insert( std::make_pair( #a, \
            PluginCommandEntry<MqttPlugin>{&MqttPlugin::m_MQTT_##a, false} ));
        MQTT_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  MQTT_PLUGIN_CMD_RECORD

        #define MQTT_SUBCMD_RECORD(a) m_mapMqttCmds.insert( std::make_pair( #a, &MqttPlugin::m_MQTTCB_##a ) );
        MQTT_SUBCMD_RECORD(SUBSCRIBE)
        MQTT_SUBCMD_RECORD(UNSUBSCRIBE)
        MQTT_SUBCMD_RECORD(PING)
        MQTT_SUBCMD_RECORD(PUBLISH)
        #undef MQTT_SUBCMD_RECORD
    }

    ~MqttPlugin() = default;

    bool isInitialized(void) const { return m_bIsInitialized; }
    bool isEnabled(void) const { return m_bIsEnabled; }

    bool setParams(const PluginDataSet *psSetParams);
    void getParams(PluginDataGet *psGetParams) const;
    bool doDispatch(const std::string& strCmd, const std::string& strParams, std::stop_token st = {}) const;
    const PluginCommandsMap<MqttPlugin>* getMap(void) const { return &m_mapCmds; }
    const std::string& getVersion(void) const { return m_strVersion; }
    const std::string& getData(void) const { return m_strResultData; }
    void resetData(void) const { m_strResultData.clear(); }
    bool doInit(void *pvUserData);
    bool doEnable(void) { m_bIsEnabled = true; return true; }
    void doCleanup(void);
    bool isFaultTolerant(void) const { return m_bIsFaultTolerant; }
    bool isPrivileged(void) const { return m_bIsPrivileged; }

    // Getters/Setters
    const std::string& getHost(void) const { return m_strHost; }
    void setHost(const std::string& host) const { m_strHost = host; }
    uint16_t getPort(void) const { return m_u16Port; }
    bool setPort(const std::string& portStr) const;
    bool isTlsEnabled(void) const { return m_bUseTls; }
    void setTlsEnabled(bool val) const { m_bUseTls = val; }
    uint8_t getQos(void) const { return m_u16Qos; }
    bool setQos(const std::string& qosStr) const;
    bool getRetain(void) const { return m_bRetain; }
    void setRetain(bool val) const { m_bRetain = val; }
    const std::string& getTlsCertPath(void) const { return m_strTlsCertPath; }
    void setTlsCertPath(const std::string& path) const { m_strTlsCertPath = path; }
    const std::string& getTlsKeyPath(void) const { return m_strTlsKeyPath; }
    void setTlsKeyPath(const std::string& path) const { m_strTlsKeyPath = path; }
    const std::string& getTlsCaPath(void) const { return m_strTlsCaPath; }
    void setTlsCaPath(const std::string& path) const { m_strTlsCaPath = path; }
    uint32_t getReadTimeout(void) const { return m_u32ReadTimeout; }
    bool setReadTimeout(const std::string& timeoutStr) const;

    // Whether `MQTT.CMD <` stores "topic:payload" or just "payload" into
    // its destination macro.
    bool getReceiveIncludeTopic(void) const { return m_bReceiveIncludeTopic; }
    void setReceiveIncludeTopic(bool val) const { m_bReceiveIncludeTopic = val; }

    // Authentication (Mosquitto password_file / auth plugins). Empty
    // username => CONNECT carries no credentials — see MqttProtocol::buildConnect().
    const std::string& getUsername(void) const { return m_strUsername; }
    void setUsername(const std::string& val) const { m_strUsername = val; }
    const std::string& getPassword(void) const { return m_strPassword; }
    void setPassword(const std::string& val) const { m_strPassword = val; }

    // Last Will and Testament.
    const std::string& getWillTopic(void) const { return m_strWillTopic; }
    void setWillTopic(const std::string& val) const { m_strWillTopic = val; }
    const std::string& getWillPayload(void) const { return m_strWillPayload; }
    void setWillPayload(const std::string& val) const { m_strWillPayload = val; }
    uint8_t getWillQos(void) const { return m_u8WillQos; }
    bool setWillQos(const std::string& qosStr) const;
    bool getWillRetain(void) const { return m_bWillRetain; }
    void setWillRetain(bool val) const { m_bWillRetain = val; }

    // Clean Session — false requests a persistent broker session for
    // m_strClientId (needs a stable, explicitly-set client id to be useful).
    bool getCleanSession(void) const { return m_bCleanSession; }
    void setCleanSession(bool val) const { m_bCleanSession = val; }

    const std::string& getClientId(void) const { return m_strClientId; }
    void setClientId(const std::string& val) const { m_strClientId = val; }

private:

    // ---- MQTT sub-command handlers (the "specific callback associated to
    // that command", per the CMD > <command> dispatch table) ----
    // Each: validates args (always); if m_bIsEnabled, also performs the
    // real exchange over m_pTransport (opening/CONNECTing it first via
    // m_EnsureSession() if needed) and fills outConfirmation with this
    // command's short success token. Returns false on bad args, a failed
    // send/receive, or a protocol-level refusal (e.g. SUBACK 0x80).
    bool m_MQTTCB_SUBSCRIBE  (const std::vector<std::string>& args, std::string& outConfirmation) const;
    bool m_MQTTCB_UNSUBSCRIBE(const std::vector<std::string>& args, std::string& outConfirmation) const;
    bool m_MQTTCB_PING       (const std::vector<std::string>& args, std::string& outConfirmation) const;
    bool m_MQTTCB_PUBLISH    (const std::vector<std::string>& args, std::string& outConfirmation) const;

    using MqttSubCmdHandler = bool (MqttPlugin::*)(const std::vector<std::string>&, std::string&) const;
    std::unordered_map<std::string, MqttSubCmdHandler> m_mapMqttCmds;

    // ---- Intermediary layer: MQTT.CMD argument decomposition ----
    // Parses one "> ..." / "<" argument string (see class doc comment),
    // dispatches to m_mapMqttCmds, and checks the optional receive-side
    // assertion. Shared by m_MQTT_CMD() and m_MQTT_SCRIPT() (one call per
    // script line), which is why it's split out rather than inlined into
    // either.
    bool m_ExecuteCmdString(const std::string& rawArgs) const;
    bool m_DoReceive() const; // the "<" side — see class doc comment

    // Splits 'text' on the first '|' that is not inside a "..." quoted
    // span, trims both halves. If no such '|' exists, sendPart is all of
    // 'text' (trimmed) and receivePart is left empty.
    static void m_SplitSendReceive(const std::string& text, std::string& sendPart, std::string& receivePart);

    // Tokenizes on whitespace, treating a "..." span as one token (quotes
    // stripped, spaces preserved inside) and a H"..." span as one token
    // whose value is the decoded raw bytes. Returns false (logging why) if
    // a H"..." span contains invalid hex — better to fail the whole command
    // than silently run it with a token that isn't what the user wrote.
    static bool m_TokenizeArgs(const std::string& text, std::vector<std::string>& outTokens);

    // ---- Session management ----
    // Opens m_pTransport (TCP + TLS per CONFIG) and performs CONNECT/
    // CONNACK if not already open+connected; a no-op otherwise. All four
    // MQTT sub-command handlers call this before doing anything else.
    bool m_EnsureSession() const;

    // Sends 'packet' and, on success, refreshes m_lastActivity (see
    // m_EnsureKeepAlive()). Every packet send in this plugin goes through
    // this rather than m_pTransport->send() directly, for that reason.
    ICommDriver::Status m_SendPacket(const std::vector<uint8_t>& packet) const;

    // Reads one complete MQTT packet (fixed header, Remaining Length,
    // payload) from m_pTransport. timeoutMs bounds only the wait for the
    // packet's first byte; once a packet has started arriving, the rest is
    // read with its own short fixed timeout (a stall mid-packet is a
    // broken-connection problem, not a "nothing to receive yet" one).
    ICommDriver::Status m_readPacket(std::vector<uint8_t>& packetOut, uint32_t timeoutMs) const;

    // Reads packets (via m_readPacket()) until one of type expectedType
    // carrying packet id expectedPacketId turns up, or timeoutMs elapses —
    // anything else read meanwhile is logged and discarded. Used to pull
    // SUBACK/UNSUBACK/PUBACK/PUBREC/PUBREL/PUBCOMP off the wire.
    bool m_WaitForAckPacket(uint8_t expectedType, uint16_t expectedPacketId,
                             uint32_t timeoutMs, std::vector<uint8_t>& outPacket) const;

    // If at least (60 * 0.8) seconds have passed since the last byte this
    // plugin wrote to the wire, sends a PINGREQ and waits for PINGRESP
    // before `MQTT.CMD <` proceeds to its own (possibly long) wait — see
    // m_DoReceive(). 60s matches the fixed keepalive this plugin declares
    // in its CONNECT packet (see m_EnsureSession()).
    bool m_EnsureKeepAlive() const;

    bool m_LocalSetParams(const PluginDataSet *psSetParams);

    // Members
    PluginCommandsMap<MqttPlugin> m_mapCmds;
    std::string m_strVersion;
    mutable std::string m_strResultData;
    bool m_bIsInitialized;
    bool m_bIsEnabled;
    bool m_bIsFaultTolerant;
    bool m_bIsPrivileged;

    std::string m_strArtefactsPath;

    // MQTT Specific Config
    mutable std::string m_strHost;
    mutable uint16_t m_u16Port;
    mutable bool m_bUseTls;
    mutable uint8_t m_u16Qos; // default publish QoS (0-2)
    mutable bool m_bRetain;   // default publish retain flag

    mutable uint32_t m_u32ReadTimeout; // ack / MQTT.CMD < wait timeout, ms

    mutable std::string m_strTlsCaPath;
    mutable std::string m_strTlsCertPath;
    mutable std::string m_strTlsKeyPath;

    mutable std::string m_strClientId;
    mutable bool m_bReceiveIncludeTopic;

    mutable std::string m_strUsername;
    mutable std::string m_strPassword;

    mutable std::string m_strWillTopic;
    mutable std::string m_strWillPayload;
    mutable uint8_t m_u8WillQos;
    mutable bool m_bWillRetain;

    mutable bool m_bCleanSession;

    // The one persistent MQTT session this plugin instance keeps — see the
    // class doc comment's "Session lifetime" section.
    mutable std::shared_ptr<MqttTransport> m_pTransport;
    mutable MqttProtocol m_protocol;
    mutable std::chrono::steady_clock::time_point m_lastActivity;

    /**
      * \brief functions associated to the plugin commands
    */
    #define MQTT_PLUGIN_CMD_RECORD(a)  bool m_MQTT_##a ( const std::string& args, std::stop_token st ) const;
    MQTT_PLUGIN_COMMANDS_CONFIG_TABLE
    #undef  MQTT_PLUGIN_CMD_RECORD
};

#endif // MQTT_PLUGIN_HPP
