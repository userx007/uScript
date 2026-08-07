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

#include "uTcpip.hpp"
#include "mqtt_protocol.hpp"

typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;

#define MQTT_PLUGIN_VERSION   "4.0.0.0"
#define MQTT_PLUGIN_NAME      "MQTT"

#define MQTT_PLUGIN_COMMANDS_CONFIG_TABLE \
    MQTT_PLUGIN_CMD_RECORD(INFO)          \
    MQTT_PLUGIN_CMD_RECORD(CONFIG)        \
    MQTT_PLUGIN_CMD_RECORD(CMD)           \
    MQTT_PLUGIN_CMD_RECORD(SCRIPT)

/**
 * @brief MQTT plugin — follows the same CMD/SCRIPT pattern as KVCANPlugin:
 * m_MQTT_CMD()/m_MQTT_SCRIPT() are thin wrappers around
 * ucmdexec::generic_cmd()/generic_script(), whose DriverT is the real,
 * already-existing `TCPIP` driver (src/lib/drivers/tcpip) — unmodified,
 * never reimplemented or wrapped in a decorator that stands in for it.
 *
 * The difference from a plain UART/TCPIP-style plugin, exactly mirroring
 * why KVCANPlugin does the same thing for CAN-TP: MQTT.CMD's grammar
 * ("> <SUBSCRIBE|UNSUBSCRIBE|PING|PUBLISH> ... [| expected]", or "<" for
 * receive) has nothing to do with the generic "> data ~ xtra | expected"
 * shape CommScriptCommandInterpreter<TCPIP> would otherwise assume, and one
 * logical MQTT operation maps to a small protocol exchange (a CONNECT/
 * CONNACK handshake if the session isn't up yet, one packet out, its
 * acknowledgement back — occasionally with an extra PUBREL/PUBCOMP leg for
 * QoS 2) rather than a single raw byte buffer. So m_MQTT_CMD()/
 * m_MQTT_SCRIPT() supply `m_Send()`/`m_Receive()` as
 * `CommScriptCommandInterpreter`/`CommScriptClient`'s `pfsend`/`pfrecv`
 * override (see uCommScriptCommandInterpreter.hpp) instead of letting the
 * interpreter call `driver->tout_write()`/`tout_read()` directly:
 *
 *   - **Protocol side**: `MqttProtocol` (mqtt_protocol.hpp) — pure MQTT
 *     v3.1.1 packet encode/decode, no I/O, driver-agnostic. Owned here as
 *     m_protocol (persists for the life of the session, since packet ids
 *     are assigned from one running sequence).
 *   - **Driver side**: `TCPIP` — the real Ethernet driver. m_Send()/
 *     m_Receive() are the only things that ever call its tout_write()/
 *     tout_read(); every physical send/receive is reported to the GUI
 *     comm-dump panel by hand (`gui_notify_comm_dump()`) right after it
 *     happens, once per complete MQTT packet — see m_SendPacket()/
 *     m_ReadPacket(). This is what makes the comm-dump panel show the real
 *     driver's actual traffic (the literal bytes exchanged with the
 *     broker) instead of a decorator's higher-level view of it: supplying
 *     pfsend/pfrecv suppresses the interpreter's own automatic dump (which
 *     would otherwise show the pre-parse MQTT.CMD argument text /
 *     confirmation string, not real wire bytes), and this plugin dumps the
 *     accurate replacement itself.
 *   - **Plugin side**: this class — CONFIG storage, the intermediary
 *     command-text parsing (m_Send()), TLS (layered directly onto TCPIP's
 *     socket via nativeHandle(), since TCPIP has no TLS awareness of its
 *     own), and the CONNECT/CONNACK session handshake (performed once, in
 *     m_OpenDriver(), before the driver is ever handed to the interpreter).
 *
 * -------------------------------------------------------------------------
 * Session lifetime
 * -------------------------------------------------------------------------
 * Unlike a typical UART/TCPIP CMD (fresh connection per call), every
 * MQTT.CMD/MQTT.SCRIPT call shares one persistent TCPIP connection + MQTT
 * session per plugin instance — opened and CONNECTed lazily by
 * m_OpenDriver() the first time it's needed, and kept alive for as long as
 * the plugin is loaded (closed by doCleanup()). This is what makes
 * `MQTT.CMD <` meaningful: it waits on whatever `MQTT.CMD > SUBSCRIBE ...`
 * calls happened earlier on that same session, including from a background
 * thread (`MQTT.CMD < &`).
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
        , m_u32ReadBufferSize(4096)
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

        m_mapMqttCmds.insert({"SUBSCRIBE",   &MqttPlugin::m_HandleSubscribe});
        m_mapMqttCmds.insert({"UNSUBSCRIBE", &MqttPlugin::m_HandleUnsubscribe});
        m_mapMqttCmds.insert({"PING",        &MqttPlugin::m_HandlePing});
        m_mapMqttCmds.insert({"PUBLISH",     &MqttPlugin::m_HandlePublish});
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
    uint32_t getReadBufferSize(void) const { return m_u32ReadBufferSize; }
    bool setReadBufferSize(const std::string& bufSizeStr) const;

    bool getReceiveIncludeTopic(void) const { return m_bReceiveIncludeTopic; }
    void setReceiveIncludeTopic(bool val) const { m_bReceiveIncludeTopic = val; }

    const std::string& getUsername(void) const { return m_strUsername; }
    void setUsername(const std::string& val) const { m_strUsername = val; }
    const std::string& getPassword(void) const { return m_strPassword; }
    void setPassword(const std::string& val) const { m_strPassword = val; }

    const std::string& getWillTopic(void) const { return m_strWillTopic; }
    void setWillTopic(const std::string& val) const { m_strWillTopic = val; }
    const std::string& getWillPayload(void) const { return m_strWillPayload; }
    void setWillPayload(const std::string& val) const { m_strWillPayload = val; }
    uint8_t getWillQos(void) const { return m_u8WillQos; }
    bool setWillQos(const std::string& qosStr) const;
    bool getWillRetain(void) const { return m_bWillRetain; }
    void setWillRetain(bool val) const { m_bWillRetain = val; }

    bool getCleanSession(void) const { return m_bCleanSession; }
    void setCleanSession(bool val) const { m_bCleanSession = val; }

    const std::string& getClientId(void) const { return m_strClientId; }
    void setClientId(const std::string& val) const { m_strClientId = val; }

private:

    // Factory used by both m_MQTT_CMD() and m_MQTT_SCRIPT() (passed as
    // ucmdexec::generic_cmd/generic_script's openFn): returns the one
    // persistent TCPIP connection for this plugin instance, opening it —
    // and completing the TLS handshake (if configured) and the MQTT
    // CONNECT/CONNACK handshake — on first use. Reused as-is on every later
    // call as long as it's still open; see class doc comment's "Session
    // lifetime". Returns nullptr (logging why) on any failure.
    std::shared_ptr<TCPIP> m_OpenDriver(void) const;

    // pfsend/pfrecv overrides — see class doc comment. Both are `const`
    // (CommScriptCommandInterpreter's contract); all state either one
    // touches (m_protocol, m_lastActivity, the ack-pending markers) is
    // `mutable` for exactly that reason, or (for the ack-pending markers)
    // `thread_local` — see m_Receive()'s definition for why.
    ICommDriver::WriteResult m_Send(uint32_t u32WriteTimeout, std::span<const uint8_t> dataSpan,
                                     std::shared_ptr<const TCPIP> shpDriver, std::string_view xtra_params) const;
    ICommDriver::ReadResult m_Receive(uint32_t u32ReadTimeout, std::span<uint8_t> dataSpan,
                                       const ICommDriver::ReadOptions& options,
                                       std::shared_ptr<const TCPIP> shpDriver, std::string_view xtra_params) const;

    // TLS — layered directly onto shpDriver's socket via nativeHandle()
    // (TCPIP itself has no TLS awareness). Set up once by m_OpenDriver();
    // every physical send/receive after that transparently routes through
    // SSL_write()/SSL_read() instead of shpDriver->tout_write()/tout_read()
    // — see m_PhysicalSend()/m_PhysicalRecv().
    bool m_SetupTls(const std::shared_ptr<TCPIP>& pDriver) const;
    mutable SSL_CTX* m_sslCtx = nullptr;
    mutable SSL* m_ssl = nullptr;

    // Physical I/O: routes through SSL if m_ssl is set, otherwise straight
    // to shpDriver->tout_write()/tout_read() — the real driver call every
    // packet send/receive in this class ultimately goes through.
    ICommDriver::Status m_PhysicalSend(const std::shared_ptr<const TCPIP>& shpDriver,
                                        std::span<const uint8_t> data, uint32_t timeoutMs) const;
    ICommDriver::Status m_PhysicalRecv(const std::shared_ptr<const TCPIP>& shpDriver,
                                        std::span<uint8_t> buffer, uint32_t timeoutMs, size_t& outBytesRead) const;

    // Sends one complete MQTT packet (built by MqttProtocol) via
    // m_PhysicalSend(), reports it to the GUI comm-dump panel on success
    // (see class doc comment), and refreshes m_lastActivity (see
    // m_EnsureKeepAlive()).
    ICommDriver::Status m_SendPacket(const std::shared_ptr<const TCPIP>& shpDriver,
                                      const std::vector<uint8_t>& packet, std::string_view xtra_params) const;

    // Reads one complete MQTT packet (fixed header, Remaining Length,
    // payload) via m_PhysicalRecv() and reports it to the GUI comm-dump
    // panel as a single row on success — a raw byte-by-byte dump of MQTT's
    // variable-length framing would be far noisier than useful, so this
    // dumps the fully-reassembled packet once, the same granularity
    // KVCANPlugin's un-segmented ("TpProtocol::NONE") path uses for a
    // single physical frame. timeoutMs bounds only the wait for the
    // packet's first byte; once a packet has started arriving, the rest is
    // read with its own short fixed timeout (a stall mid-packet is a
    // broken-connection problem, not a "nothing to receive yet" one).
    ICommDriver::Status m_ReadPacket(const std::shared_ptr<const TCPIP>& shpDriver,
                                      std::vector<uint8_t>& packetOut, uint32_t timeoutMs,
                                      std::string_view xtra_params) const;

    // Reads packets (via m_ReadPacket()) until one of type expectedType
    // carrying packet id expectedPacketId turns up, or timeoutMs elapses —
    // anything else read meanwhile is logged and discarded.
    bool m_WaitForAckPacket(const std::shared_ptr<const TCPIP>& shpDriver,
                             uint8_t expectedType, uint16_t expectedPacketId,
                             uint32_t timeoutMs, std::vector<uint8_t>& outPacket, std::string_view xtra_params) const;

    // If at least (kKeepAliveSeconds * 0.8) seconds have passed since the
    // last byte this plugin wrote to the wire, sends a PINGREQ and waits
    // for PINGRESP — called from the standalone-receive path in
    // m_Receive(), the one call expected to sit idle for a long time.
    bool m_EnsureKeepAlive(const std::shared_ptr<const TCPIP>& shpDriver, std::string_view xtra_params) const;
    mutable std::chrono::steady_clock::time_point m_lastActivity;

    // ---- Intermediary layer: MQTT.CMD argument decomposition ----
    // Tokenizes on whitespace only (the shared CommScriptCommandValidator
    // grammar requires an unquoted field to contain no '"' at all, so this
    // layer can't support its own embedded quoting on top of that — see
    // this class's .cpp for the full explanation, including why a trailing
    // NUL byte must be stripped first).
    static void m_TokenizeArgs(const std::string& text, std::vector<std::string>& outTokens);

    // MQTT sub-command handlers (the "specific callback associated to that
    // command"). Each builds and sends its packet via m_protocol/
    // m_SendPacket(), and — for a command whose success is confirmed by an
    // acknowledgement — records what m_Receive() should wait for next (see
    // m_Receive()'s doc comment). Returns false on bad arguments or a send
    // failure.
    bool m_HandleSubscribe(const std::shared_ptr<const TCPIP>& shpDriver, const std::vector<std::string>& args, std::string_view xtra_params) const;
    bool m_HandleUnsubscribe(const std::shared_ptr<const TCPIP>& shpDriver, const std::vector<std::string>& args, std::string_view xtra_params) const;
    bool m_HandlePing(const std::shared_ptr<const TCPIP>& shpDriver, const std::vector<std::string>& args, std::string_view xtra_params) const;
    bool m_HandlePublish(const std::shared_ptr<const TCPIP>& shpDriver, const std::vector<std::string>& args, std::string_view xtra_params) const;

    using MqttSubCmdHandler = bool (MqttPlugin::*)(const std::shared_ptr<const TCPIP>&, const std::vector<std::string>&, std::string_view) const;
    std::unordered_map<std::string, MqttSubCmdHandler> m_mapMqttCmds;

    // The "<" side: waits for the next incoming PUBLISH, acknowledges it
    // per its QoS, and writes "topic:payload" or "payload" (per
    // m_bReceiveIncludeTopic) into buffer. Called from m_Receive() when no
    // ack is pending — see its doc comment.
    ICommDriver::ReadResult m_DoStandaloneReceive(const std::shared_ptr<const TCPIP>& shpDriver,
                                                   uint32_t timeoutMs, std::span<uint8_t> buffer,
                                                   std::string_view xtra_params) const;

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

    mutable std::string m_strHost;
    mutable uint16_t m_u16Port;
    mutable bool m_bUseTls;
    mutable uint8_t m_u16Qos;
    mutable bool m_bRetain;

    mutable uint32_t m_u32ReadTimeout;
    mutable uint32_t m_u32ReadBufferSize;

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

    // The persistent real driver + MQTT protocol/session state — see class
    // doc comment's "Session lifetime" and m_OpenDriver().
    mutable std::shared_ptr<TCPIP> m_pTcpip;
    mutable MqttProtocol m_protocol;
    mutable bool m_sessionEstablished = false;

    /**
      * \brief functions associated to the plugin commands
    */
    #define MQTT_PLUGIN_CMD_RECORD(a)  bool m_MQTT_##a ( const std::string& args, std::stop_token st ) const;
    MQTT_PLUGIN_COMMANDS_CONFIG_TABLE
    #undef  MQTT_PLUGIN_CMD_RECORD
};

#endif // MQTT_PLUGIN_HPP
