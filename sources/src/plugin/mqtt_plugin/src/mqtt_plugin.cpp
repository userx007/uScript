#include "mqtt_plugin.hpp"
#include "uBoolEvaluator.hpp"
#include "uHexlify.hpp"
#include "uPluginSettings.hpp"
#include "uTimer.hpp"

#include <sstream>
#include <fstream>
#include <algorithm>
#include <cctype>

#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LOG_HDR "MQTT PLUGIN |"

// INI Keys
#define K_HOST           "HOST"
#define K_PORT           "PORT"
#define K_TLS_ENABLED    "TLS_ENABLED"
#define K_QOS            "QOS"
#define K_RETAIN         "RETAIN"
#define K_TLS_CA         "TLS_CA_CERT"
#define K_TLS_CLIENT_CERT "TLS_CLIENT_CERT"
#define K_TLS_CLIENT_KEY  "TLS_CLIENT_KEY"
#define K_ARTEFACTS      "ARTEFACTS_PATH"
#define K_READ_TIMEOUT   "READ_TIMEOUT"
#define K_RECEIVE_TOPIC  "RECEIVE_TOPIC"
#define K_CLIENT_ID      "CLIENT_ID"
#define K_USERNAME       "USERNAME"
#define K_PASSWORD       "PASSWORD"
#define K_WILL_TOPIC     "WILL_TOPIC"
#define K_WILL_PAYLOAD   "WILL_PAYLOAD"
#define K_WILL_QOS       "WILL_QOS"
#define K_WILL_RETAIN    "WILL_RETAIN"
#define K_CLEAN_SESSION  "CLEAN_SESSION"

// Config Command Short Keys
#define SK_HOST "h"
#define SK_PORT "p"
#define SK_TLS  "t"
#define SK_QOS  "q"
#define SK_RET  "r"
#define SK_CA   "ca"
#define SK_CRT  "crt"
#define SK_KEY  "key"
#define SK_RTOUT "rt"
#define SK_RTOPIC "it"
#define SK_CID   "id"
#define SK_USER  "u"
#define SK_PASS  "pw"
#define SK_WTOPIC "wt"
#define SK_WPAY   "wp"
#define SK_WQOS   "wq"
#define SK_WRET   "wr"
#define SK_CLEAN  "cs"

// Wait budget for a single ack (SUBACK/UNSUBACK/PUBACK/PUBREC/PUBREL/PUBCOMP/PINGRESP/CONNACK).
static constexpr uint32_t kAckTimeoutMs = 5000;
// Per-read timeout once a packet has started arriving (see m_readPacket()'s doc comment).
static constexpr uint32_t kPacketContinuationTimeoutMs = 5000;
// Fixed CONNECT keepalive this plugin declares — see mqtt_plugin.hpp's m_EnsureKeepAlive() doc comment.
static constexpr uint16_t kKeepAliveSeconds = 60;

extern "C"
{
    EXPORTED MqttPlugin* pluginEntry() { return new MqttPlugin(); }
    EXPORTED void pluginExit(MqttPlugin *ptrPlugin) { delete ptrPlugin; }
}

bool MqttPlugin::doInit(void *pvUserData)
{
    (void)pvUserData;
    m_bIsInitialized = true;
    return true;
}

void MqttPlugin::doCleanup(void)
{
    m_bIsInitialized = false;
    m_bIsEnabled = false;
    m_strResultData.clear();
    if (m_pTransport && m_pTransport->isOpen()) {
        auto pkt = m_protocol.buildDisconnect();
        m_SendPacket(pkt); // best-effort; a clean DISCONNECT is not acknowledged by the broker
        m_pTransport->close();
    }
    m_pTransport.reset();
    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Cleanup done"));
}

bool MqttPlugin::setParams(const PluginDataSet *psSetParams)
{
    bool bRetVal = false;
    if (generic_setparams<MqttPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
        if (m_LocalSetParams(psSetParams)) {
            bRetVal = true;
        }
    }
    return bRetVal;
}

void MqttPlugin::getParams(PluginDataGet *psGetParams) const
{
    generic_getparams<MqttPlugin>(this, psGetParams);
}

bool MqttPlugin::doDispatch(const std::string& strCmd, const std::string& strParams, std::stop_token st) const
{
    return generic_dispatch<MqttPlugin>(this, strCmd, strParams, st);
}

// --- Setters requiring validation ---

bool MqttPlugin::setPort(const std::string& portStr) const
{
    uint32_t port = 0;
    if (!numeric::str2uint32(portStr, port)) return false;
    if (port > 65535) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid port:"); LOG_UINT32(port));
        return false;
    }
    m_u16Port = static_cast<uint16_t>(port);
    return true;
}

bool MqttPlugin::setQos(const std::string& qosStr) const
{
    uint8_t qos = 0;
    if (!numeric::str2uint8(qosStr, qos)) return false;
    if (qos > 2) return false;
    m_u16Qos = qos;
    return true;
}

bool MqttPlugin::setReadTimeout(const std::string& timeoutStr) const
{
    return numeric::str2uint32(timeoutStr, m_u32ReadTimeout);
}

bool MqttPlugin::setWillQos(const std::string& qosStr) const
{
    uint8_t qos = 0;
    if (!numeric::str2uint8(qosStr, qos)) return false;
    if (qos > 2) return false;
    m_u8WillQos = qos;
    return true;
}

// --- Local Params ---

bool MqttPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    if (psSetParams->mapSettings.empty()) return true;

    PluginSettingsBinder sSettings;
    sSettings.Bind(K_ARTEFACTS, m_strArtefactsPath);
    sSettings.Bind(K_HOST,      m_strHost);
    sSettings.Bind(K_PORT,      [this](const std::string& v) { return setPort(v); });
    sSettings.Bind(K_QOS,       [this](const std::string& v) { return setQos(v); });
    sSettings.Bind(K_RETAIN, [this](const std::string& v) {
        BoolExprEvaluator beEvaluator;
        bool bVal = false;
        if (false == beEvaluator.evaluate(v, bVal)) return false;
        setRetain(bVal);
        return true;
    });
    sSettings.Bind(K_TLS_ENABLED, [this](const std::string& v) {
        BoolExprEvaluator beEvaluator;
        bool bVal = false;
        if (false == beEvaluator.evaluate(v, bVal)) return false;
        setTlsEnabled(bVal);
        return true;
    });
    sSettings.Bind(K_TLS_CA,          m_strTlsCaPath);
    sSettings.Bind(K_TLS_CLIENT_CERT, m_strTlsCertPath);
    sSettings.Bind(K_TLS_CLIENT_KEY,  m_strTlsKeyPath);
    sSettings.Bind(K_READ_TIMEOUT,    [this](const std::string& v) { return setReadTimeout(v); });
    sSettings.Bind(K_RECEIVE_TOPIC,   m_bReceiveIncludeTopic);
    sSettings.Bind(K_CLIENT_ID,       m_strClientId);
    sSettings.Bind(K_USERNAME,        m_strUsername);
    sSettings.Bind(K_PASSWORD,        m_strPassword);
    sSettings.Bind(K_WILL_TOPIC,      m_strWillTopic);
    sSettings.Bind(K_WILL_PAYLOAD,    m_strWillPayload);
    sSettings.Bind(K_WILL_QOS,        [this](const std::string& v) { return setWillQos(v); });
    sSettings.Bind(K_WILL_RETAIN, [this](const std::string& v) {
        BoolExprEvaluator beEvaluator;
        bool bVal = false;
        if (false == beEvaluator.evaluate(v, bVal)) return false;
        setWillRetain(bVal);
        return true;
    });
    sSettings.Bind(K_CLEAN_SESSION, [this](const std::string& v) {
        BoolExprEvaluator beEvaluator;
        bool bVal = false;
        if (false == beEvaluator.evaluate(v, bVal)) return false;
        setCleanSession(bVal);
        return true;
    });

    sSettings.Apply(psSetParams->mapSettings, nullptr, /*bStopOnFirstError=*/false);

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Config updated. Host:") LOG_STRING(m_strHost)
              LOG_STRING(" TLS:") LOG_BOOL(m_bUseTls));
    return true;
}

// -----------------------------------------------------------------------
// Session management — see mqtt_plugin.hpp's "Session lifetime" doc comment
// -----------------------------------------------------------------------

bool MqttPlugin::m_EnsureSession() const
{
    if (m_pTransport && m_pTransport->isOpen()) {
        return true;
    }

    if (m_strHost.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Host not configured — MQTT.CONFIG h=<host> first"));
        return false;
    }

    m_pTransport = std::make_shared<MqttTransport>(m_strHost);

    MqttTransport::Config tcfg;
    tcfg.host = m_strHost;
    tcfg.port = m_u16Port;
    tcfg.connectTimeoutMs = 5000;
    tcfg.useTls = m_bUseTls;
    tcfg.caCertPath = m_strTlsCaPath;
    tcfg.clientCertPath = m_strTlsCertPath;
    tcfg.clientKeyPath = m_strTlsKeyPath;

    if (m_pTransport->open(tcfg) != ICommDriver::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Transport open failed"));
        m_pTransport.reset();
        return false;
    }

    m_protocol.resetPacketIdSequence();

    MqttProtocol::ConnectParams cp;
    cp.clientId = m_strClientId.empty()
        ? ("mqtt_plugin_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))
        : m_strClientId;
    cp.username     = m_strUsername;
    cp.password     = m_strPassword;
    cp.willTopic    = m_strWillTopic;
    cp.willPayload  = m_strWillPayload;
    cp.willQos      = m_u8WillQos;
    cp.willRetain   = m_bWillRetain;
    cp.cleanSession = m_bCleanSession;
    cp.keepAlive    = kKeepAliveSeconds;

    auto connectPkt = m_protocol.buildConnect(cp);
    if (m_SendPacket(connectPkt) != ICommDriver::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to send CONNECT"));
        m_pTransport->close();
        m_pTransport.reset();
        return false;
    }

    std::vector<uint8_t> ackPacket;
    auto st = m_readPacket(ackPacket, kAckTimeoutMs);
    if (st != ICommDriver::Status::SUCCESS || MqttProtocol::packetType(ackPacket) != MqttProtocol::kConnAck) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Did not receive CONNACK"));
        m_pTransport->close();
        m_pTransport.reset();
        return false;
    }

    auto result = m_protocol.decodeConnAck(ackPacket);
    if (!result.ok()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CONNACK refused, code:"); LOG_UINT32(result.returnCode));
        m_pTransport->close();
        m_pTransport.reset();
        return false;
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Session established, sessionPresent="); LOG_BOOL(result.sessionPresent));
    return true;
}

ICommDriver::Status MqttPlugin::m_SendPacket(const std::vector<uint8_t>& packet) const
{
    auto st = m_pTransport->send(std::span<const uint8_t>(packet.data(), packet.size()), 5000);
    if (st == ICommDriver::Status::SUCCESS) {
        m_lastActivity = std::chrono::steady_clock::now();
    }
    return st;
}

ICommDriver::Status MqttPlugin::m_readPacket(std::vector<uint8_t>& packetOut, uint32_t timeoutMs) const
{
    packetOut.clear();

    // 1. First byte (Fixed Header) — the only part of a packet that can
    // legitimately take a while to arrive, so the only part bounded by the
    // caller's timeoutMs.
    uint8_t firstByte = 0;
    {
        uint8_t buf[1];
        size_t got = 0;
        auto st = m_pTransport->recv(std::span<uint8_t>(buf, 1), timeoutMs, got);
        if (st != ICommDriver::Status::SUCCESS || got == 0) {
            return ICommDriver::Status::READ_TIMEOUT;
        }
        firstByte = buf[0];
    }
    packetOut.push_back(firstByte);

    // 2. Remaining Length (Variable Byte Integer, 1-4 bytes, MSB=continuation)
    int multiplier = 1;
    uint32_t remLen = 0;
    while (true) {
        if (packetOut.size() >= 5) { // 1 fixed-header byte + at most 4 length bytes
            return ICommDriver::Status::PROTOCOL_ERROR;
        }
        uint8_t buf[1];
        size_t got = 0;
        auto st = m_pTransport->recv(std::span<uint8_t>(buf, 1), kPacketContinuationTimeoutMs, got);
        if (st != ICommDriver::Status::SUCCESS || got == 0) {
            return ICommDriver::Status::READ_TIMEOUT;
        }
        packetOut.push_back(buf[0]);
        remLen += (buf[0] & 0x7F) * multiplier;
        multiplier *= 128;
        if ((buf[0] & 0x80) == 0) {
            break;
        }
    }

    // 3. Payload (exactly remLen bytes)
    if (remLen > 0) {
        std::vector<uint8_t> payloadBuf(remLen);
        size_t totalRead = 0;
        while (totalRead < remLen) {
            size_t got = 0;
            auto st = m_pTransport->recv(
                std::span<uint8_t>(payloadBuf.data() + totalRead, remLen - totalRead),
                kPacketContinuationTimeoutMs, got);
            if (st != ICommDriver::Status::SUCCESS || got == 0) {
                return ICommDriver::Status::READ_TIMEOUT;
            }
            totalRead += got;
        }
        packetOut.insert(packetOut.end(), payloadBuf.begin(), payloadBuf.end());
    }

    return ICommDriver::Status::SUCCESS;
}

bool MqttPlugin::m_WaitForAckPacket(uint8_t expectedType, uint16_t expectedPacketId,
                                     uint32_t timeoutMs, std::vector<uint8_t>& outPacket) const
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (true) {
        const auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::milliseconds(0)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Timed out waiting for ack, type=0x"); LOG_HEX8(expectedType));
            return false;
        }
        const uint32_t remainingMs = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count());

        std::vector<uint8_t> packet;
        auto st = m_readPacket(packet, remainingMs);
        if (st != ICommDriver::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Read failed waiting for ack:"); LOG_STRING(ICommDriver::to_string(st)));
            return false;
        }

        if (MqttProtocol::packetType(packet) != expectedType) {
            LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Unexpected packet while waiting for ack, got type=0x");
                      LOG_HEX8(MqttProtocol::packetType(packet)));
            continue;
        }

        uint16_t gotPacketId = 0;
        if (!MqttProtocol::decodeSimpleAck(packet, &gotPacketId)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Malformed ack packet"));
            return false;
        }
        if (gotPacketId != expectedPacketId) {
            LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Ack packet id mismatch, expected"); LOG_UINT32(expectedPacketId);
                      LOG_STRING("got"); LOG_UINT32(gotPacketId); LOG_STRING("— still waiting"));
            continue; // could be a stray/late ack for an older packet id
        }

        outPacket = std::move(packet);
        return true;
    }
}

bool MqttPlugin::m_EnsureKeepAlive() const
{
    const auto elapsed = std::chrono::steady_clock::now() - m_lastActivity;
    const auto threshold = std::chrono::milliseconds(static_cast<uint32_t>(kKeepAliveSeconds) * 800 /* 0.8 * 1000ms */);
    if (elapsed < threshold) {
        return true; // not due yet
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Keepalive due — sending PINGREQ"));
    auto pkt = m_protocol.buildPingReq();
    if (m_SendPacket(pkt) != ICommDriver::Status::SUCCESS) {
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kAckTimeoutMs);
    while (true) {
        const auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::milliseconds(0)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Timed out waiting for PINGRESP"));
            return false;
        }
        const uint32_t remainingMs = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count());
        std::vector<uint8_t> packet;
        auto st = m_readPacket(packet, remainingMs);
        if (st != ICommDriver::Status::SUCCESS) {
            return false;
        }
        if (MqttProtocol::packetType(packet) == MqttProtocol::kPingResp) {
            return true;
        }
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Unexpected packet while waiting for PINGRESP: 0x");
                  LOG_HEX8(MqttProtocol::packetType(packet)));
    }
}

// -----------------------------------------------------------------------
// MQTT.CMD intermediary layer — see mqtt_plugin.hpp's class doc comment
// -----------------------------------------------------------------------

void MqttPlugin::m_SplitSendReceive(const std::string& text, std::string& sendPart, std::string& receivePart)
{
    bool inQuotes = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == '|' && !inQuotes) {
            sendPart = ustring::trim(text.substr(0, i));
            receivePart = ustring::trim(text.substr(i + 1));
            return;
        }
    }
    sendPart = ustring::trim(text);
    receivePart.clear();
}

bool MqttPlugin::m_TokenizeArgs(const std::string& text, std::vector<std::string>& outTokens)
{
    outTokens.clear();
    const size_t n = text.size();
    size_t i = 0;

    while (i < n) {
        while (i < n && std::isspace(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
        if (i >= n) break;

        if (text[i] == '"') {
            ++i;
            std::string tok;
            while (i < n && text[i] != '"') {
                tok.push_back(text[i]);
                ++i;
            }
            if (i < n) ++i; // skip closing quote
            outTokens.push_back(std::move(tok));
        } else if ((text[i] == 'H' || text[i] == 'h') && i + 1 < n && text[i + 1] == '"') {
            i += 2;
            std::string hex;
            while (i < n && text[i] != '"') {
                hex.push_back(text[i]);
                ++i;
            }
            if (i < n) ++i;
            std::vector<uint8_t> decoded;
            if (!hexutils::stringUnhexlify(hex, decoded)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid hex token: H\""); LOG_STRING(hex); LOG_STRING("\""));
                return false;
            }
            outTokens.emplace_back(reinterpret_cast<const char*>(decoded.data()), decoded.size());
        } else {
            std::string tok;
            while (i < n && !std::isspace(static_cast<unsigned char>(text[i]))) {
                tok.push_back(text[i]);
                ++i;
            }
            outTokens.push_back(std::move(tok));
        }
    }
    return true;
}

bool MqttPlugin::m_ExecuteCmdString(const std::string& rawArgs) const
{
    const std::string args = ustring::trim(rawArgs);
    if (args.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Usage: MQTT.CMD > <SUBSCRIBE|UNSUBSCRIBE|PING|PUBLISH> ... , or MQTT.CMD <"));
        return false;
    }

    const char direction = args[0];
    const std::string rest = ustring::trim(args.substr(1));

    if (direction == '<') {
        if (!rest.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("MQTT.CMD < takes no arguments"));
            return false;
        }
        if (!m_bIsEnabled) {
            return true; // dry-run: syntax is valid, nothing to actually receive yet
        }
        return m_DoReceive();
    }

    if (direction != '>') {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("MQTT.CMD requires a leading '>' or '<'"));
        return false;
    }

    std::string sendPart, receivePart;
    m_SplitSendReceive(rest, sendPart, receivePart);

    std::vector<std::string> tokens;
    if (!m_TokenizeArgs(sendPart, tokens)) {
        return false; // m_TokenizeArgs already logged why
    }
    if (tokens.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("MQTT.CMD > requires a command: SUBSCRIBE, UNSUBSCRIBE, PING or PUBLISH"));
        return false;
    }

    std::string cmdKeyword = tokens[0];
    std::transform(cmdKeyword.begin(), cmdKeyword.end(), cmdKeyword.begin(),
                    [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    auto it = m_mapMqttCmds.find(cmdKeyword);
    if (it == m_mapMqttCmds.end()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown MQTT command:"); LOG_STRING(tokens[0]));
        return false;
    }

    const std::vector<std::string> cmdArgs(tokens.begin() + 1, tokens.end());

    std::string confirmation;
    if (!(this->*(it->second))(cmdArgs, confirmation)) {
        return false;
    }

    if (!m_bIsEnabled) {
        return true; // dry-run: args validated, no real confirmation to check
    }

    if (!receivePart.empty()) {
        if (confirmation != receivePart) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected '"); LOG_STRING(receivePart);
                      LOG_STRING("', got '"); LOG_STRING(confirmation.empty() ? "<nothing>" : confirmation);
                      LOG_STRING("'"));
            return false;
        }
    }

    m_strResultData = confirmation;
    return true;
}

bool MqttPlugin::m_DoReceive() const
{
    if (!m_pTransport || !m_pTransport->isOpen()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("No active session — MQTT.CMD > SUBSCRIBE first"));
        return false;
    }

    if (!m_EnsureKeepAlive()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Keepalive failed — session may be dead"));
        return false;
    }

    std::vector<uint8_t> packet;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(m_u32ReadTimeout);
    while (true) {
        const auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::milliseconds(0)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("RECEIVE timed out"));
            return false;
        }
        const uint32_t remainingMs = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count());

        auto st = m_readPacket(packet, remainingMs);
        if (st != ICommDriver::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("RECEIVE failed:"); LOG_STRING(ICommDriver::to_string(st)));
            return false;
        }
        if (MqttProtocol::isPublish(packet)) {
            break;
        }
        if (MqttProtocol::packetType(packet) == MqttProtocol::kPingResp) {
            // a stray PINGRESP to a keepalive PINGREQ — not an error, just not what we're waiting for
            continue;
        }
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Unexpected packet while waiting for PUBLISH: 0x");
                  LOG_HEX8(MqttProtocol::packetType(packet)));
    }

    auto msg = m_protocol.decodePublish(packet);

    if (msg.qos == 1) {
        auto pkt = m_protocol.buildPubAck(msg.packetId);
        m_SendPacket(pkt);
    } else if (msg.qos == 2) {
        auto pkt = m_protocol.buildPubRec(msg.packetId);
        if (m_SendPacket(pkt) == ICommDriver::Status::SUCCESS) {
            std::vector<uint8_t> rel;
            if (m_WaitForAckPacket(MqttProtocol::kPubRel, msg.packetId, m_u32ReadTimeout, rel)) {
                auto comp = m_protocol.buildPubComp(msg.packetId);
                m_SendPacket(comp);
            } else {
                LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Failed to complete QoS 2 handshake for incoming PUBLISH on topic:"); LOG_STRING(msg.topic));
            }
        }
    }

    m_strResultData = m_bReceiveIncludeTopic ? (msg.topic + ":" + msg.payload) : msg.payload;

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("PUBLISH received ["); LOG_STRING(msg.topic);
              LOG_STRING("] qos="); LOG_UINT32(msg.qos); LOG_STRING("bytes="); LOG_SIZET(msg.payload.size()));
    return true;
}

// -----------------------------------------------------------------------
// MQTT sub-command handlers
// -----------------------------------------------------------------------

bool MqttPlugin::m_MQTTCB_SUBSCRIBE(const std::vector<std::string>& args, std::string& outConfirmation) const
{
    if (args.empty() || args.size() > 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Usage: SUBSCRIBE <topic> [qos]"));
        return false;
    }
    const std::string& topic = args[0];
    if (topic.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SUBSCRIBE: empty topic filter"));
        return false;
    }

    uint8_t qos = m_u16Qos;
    if (args.size() == 2) {
        uint8_t q = 0;
        if (!numeric::str2uint8(args[1], q) || q > 2) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SUBSCRIBE: qos must be 0-2, got:"); LOG_STRING(args[1]));
            return false;
        }
        qos = q;
    }

    if (!m_bIsEnabled) {
        outConfirmation = "SUBACK";
        return true;
    }

    if (!m_EnsureSession()) return false;

    uint16_t packetId = 0;
    auto pkt = m_protocol.buildSubscribe(topic, qos, &packetId);
    if (m_SendPacket(pkt) != ICommDriver::Status::SUCCESS) return false;

    std::vector<uint8_t> ack;
    if (!m_WaitForAckPacket(MqttProtocol::kSubAck, packetId, kAckTimeoutMs, ack)) return false;

    auto result = m_protocol.decodeSubAck(ack);
    if (!result.ok()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SUBSCRIBE refused by broker for topic:"); LOG_STRING(topic));
        return false;
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("SUBSCRIBE ["); LOG_STRING(topic);
              LOG_STRING("] granted qos="); LOG_UINT32(result.returnCode));
    outConfirmation = "SUBACK";
    return true;
}

bool MqttPlugin::m_MQTTCB_UNSUBSCRIBE(const std::vector<std::string>& args, std::string& outConfirmation) const
{
    if (args.size() != 1) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Usage: UNSUBSCRIBE <topic>"));
        return false;
    }
    const std::string& topic = args[0];
    if (topic.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("UNSUBSCRIBE: empty topic filter"));
        return false;
    }

    if (!m_bIsEnabled) {
        outConfirmation = "UNSUBACK";
        return true;
    }

    if (!m_pTransport || !m_pTransport->isOpen()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("UNSUBSCRIBE: no active session — SUBSCRIBE first"));
        return false;
    }

    uint16_t packetId = 0;
    auto pkt = m_protocol.buildUnsubscribe(topic, &packetId);
    if (m_SendPacket(pkt) != ICommDriver::Status::SUCCESS) return false;

    std::vector<uint8_t> ack;
    if (!m_WaitForAckPacket(MqttProtocol::kUnsubAck, packetId, kAckTimeoutMs, ack)) return false;

    outConfirmation = "UNSUBACK";
    return true;
}

bool MqttPlugin::m_MQTTCB_PING(const std::vector<std::string>& args, std::string& outConfirmation) const
{
    if (!args.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Usage: PING (no arguments)"));
        return false;
    }

    if (!m_bIsEnabled) {
        outConfirmation = "PONG";
        return true;
    }

    if (!m_pTransport || !m_pTransport->isOpen()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("PING: no active session — SUBSCRIBE or PUBLISH first"));
        return false;
    }

    auto pkt = m_protocol.buildPingReq();
    if (m_SendPacket(pkt) != ICommDriver::Status::SUCCESS) return false;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kAckTimeoutMs);
    while (true) {
        const auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::milliseconds(0)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("PING: timed out waiting for PINGRESP"));
            return false;
        }
        const uint32_t remainingMs = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count());

        std::vector<uint8_t> packet;
        auto st = m_readPacket(packet, remainingMs);
        if (st != ICommDriver::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("PING: read failed:"); LOG_STRING(ICommDriver::to_string(st)));
            return false;
        }
        if (MqttProtocol::packetType(packet) == MqttProtocol::kPingResp) {
            outConfirmation = "PONG";
            return true;
        }
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("PING: unexpected packet 0x"); LOG_HEX8(MqttProtocol::packetType(packet)));
    }
}

bool MqttPlugin::m_MQTTCB_PUBLISH(const std::vector<std::string>& args, std::string& outConfirmation) const
{
    if (args.size() != 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Usage: PUBLISH <payload> <topic>"));
        return false;
    }
    const std::string& payload = args[0];
    const std::string& topic   = args[1];
    if (topic.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("PUBLISH: empty topic"));
        return false;
    }

    if (!m_bIsEnabled) {
        outConfirmation.clear(); // depends on runtime QoS — dry-run only validates shape
        return true;
    }

    if (!m_EnsureSession()) return false;

    uint16_t packetId = 0;
    auto pkt = m_protocol.buildPublish(topic, payload, m_u16Qos, m_bRetain, &packetId);
    if (m_SendPacket(pkt) != ICommDriver::Status::SUCCESS) return false;

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("PUBLISH ["); LOG_STRING(topic);
              LOG_STRING("] qos="); LOG_UINT32(m_u16Qos); LOG_STRING("bytes="); LOG_SIZET(payload.size()));

    if (m_u16Qos == 0) {
        outConfirmation.clear(); // QoS 0 — nothing is ever acknowledged
        return true;
    }

    if (m_u16Qos == 1) {
        std::vector<uint8_t> ack;
        if (!m_WaitForAckPacket(MqttProtocol::kPubAck, packetId, kAckTimeoutMs, ack)) return false;
        outConfirmation = "PUBACK";
        return true;
    }

    // QoS 2
    std::vector<uint8_t> rec;
    if (!m_WaitForAckPacket(MqttProtocol::kPubRec, packetId, kAckTimeoutMs, rec)) return false;

    auto relPkt = m_protocol.buildPubRel(packetId);
    if (m_SendPacket(relPkt) != ICommDriver::Status::SUCCESS) return false;

    std::vector<uint8_t> comp;
    if (!m_WaitForAckPacket(MqttProtocol::kPubComp, packetId, kAckTimeoutMs, comp)) return false;

    outConfirmation = "PUBCOMP";
    return true;
}

// -----------------------------------------------------------------------
// Top-level commands
// -----------------------------------------------------------------------

bool MqttPlugin::m_MQTT_INFO(const std::string& args, std::stop_token st) const
{
    (void)args; (void)st;
    resetData();
    std::ostringstream oss;
    oss << MQTT_PLUGIN_NAME " v" << m_strVersion
        << " host=" << m_strHost
        << " port=" << m_u16Port
        << " tls=" << (m_bUseTls ? "true" : "false")
        << " qos=" << (int)m_u16Qos
        << " cleanSession=" << (m_bCleanSession ? "true" : "false")
        << " auth=" << (m_strUsername.empty() ? "none" : "username/password")
        << " will=" << (m_strWillTopic.empty() ? "none" : m_strWillTopic);
    m_strResultData = oss.str();

    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING(MQTT_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: publish/subscribe against an MQTT v3.1.1 broker (e.g. Mosquitto)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Architecture: protocol (MqttProtocol) / driver (MqttTransport) / plugin (this) — see mqtt_plugin.hpp"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set the broker host, port, TLS, auth, Will and transfer parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [h=host] [p=port] [q=qos] [t=tls] [r=retain] [ca=capath] [crt=certpath] [key=keypath]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [rt=read_tout] [id=clientid] [u=username] [pw=password] [cs=cleansession]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [wt=will_topic] [wp=will_payload] [wq=will_qos] [wr=will_retain] [it=include_topic]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : MQTT.CONFIG h=broker.local p=1883 q=1"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : one MQTT operation, on the plugin's single persistent session (opened on first use)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : > <SUBSCRIBE|UNSUBSCRIBE|PING|PUBLISH> ... [| expected]   |   <"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : MQTT.CMD > SUBSCRIBE sensors/temp 1"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         MQTT.CMD > UNSUBSCRIBE sensors/temp"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         MQTT.CMD > PING"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         MQTT.CMD > PUBLISH OPEN actuators/valve3/cmd | PUBACK"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         MQTT.CMD <                 // one blocking receive; requires an active SUBSCRIBE"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         reading ?= MQTT.CMD < &    // background thread; $reading tracks the latest message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : PUBLISH's QoS/retain come from CONFIG (q=/r=), not from the CMD line."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         '| expected' asserts the confirmation text: PUBACK/PUBCOMP (publish), SUBACK, UNSUBACK, PONG."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         Avoid an unquoted '|' inside a topic/payload token — wrap it in \"...\" instead."));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : run several MQTT.CMD-style lines from a file over the same session"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : scriptpathname [delay_ms]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : MQTT.SCRIPT script.txt 100"));
    LOG_SEP();

    return true;
}

bool MqttPlugin::m_MQTT_CONFIG(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();
    if (args.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing config args"));
        return false;
    }

    std::istringstream stream(args);
    std::string token;
    bool bRetVal = true;
    BoolExprEvaluator beEvaluator;

    while (stream >> token) {
        auto eqPos = token.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = token.substr(0, eqPos);
        std::string val = token.substr(eqPos + 1);

        if (!val.empty() && val[0] == '$') {
            // Unexpanded macro reference during script VALIDATION (dry run) —
            // real execution always resolves $macros before the plugin sees
            // the string; defer the actual value check to then.
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Deferring '"); LOG_STRING(key);
                      LOG_STRING("=" ); LOG_STRING(val);
                      LOG_STRING("' - value is a macro, resolved at execution time"));
            continue;
        }

        if (key == SK_HOST) setHost(val);
        else if (key == SK_PORT) { if (!setPort(val)) bRetVal = false; }
        else if (key == SK_QOS)  { if (!setQos(val))  bRetVal = false; }
        else if (key == SK_TLS) {
            bool b = false;
            if (true == (bRetVal = beEvaluator.evaluate(val, b))) setTlsEnabled(b);
        }
        else if (key == SK_RET) {
            bool b = false;
            if (true == (bRetVal = beEvaluator.evaluate(val, b))) setRetain(b);
        }
        else if (key == SK_CA)  setTlsCaPath(val);
        else if (key == SK_CRT) setTlsCertPath(val);
        else if (key == SK_KEY) setTlsKeyPath(val);
        else if (key == SK_RTOUT) { if (!setReadTimeout(val)) bRetVal = false; }
        else if (key == SK_RTOPIC) {
            bool b = false;
            if (true == (bRetVal = beEvaluator.evaluate(val, b))) setReceiveIncludeTopic(b);
        }
        else if (key == SK_CID)  setClientId(val);
        else if (key == SK_USER) setUsername(val);
        else if (key == SK_PASS) setPassword(val);
        else if (key == SK_WTOPIC) setWillTopic(val);
        else if (key == SK_WPAY)   setWillPayload(val);
        else if (key == SK_WQOS)   { if (!setWillQos(val)) bRetVal = false; }
        else if (key == SK_WRET) {
            bool b = false;
            if (true == (bRetVal = beEvaluator.evaluate(val, b))) setWillRetain(b);
        }
        else if (key == SK_CLEAN) {
            bool b = false;
            if (true == (bRetVal = beEvaluator.evaluate(val, b))) setCleanSession(b);
        }
    }
    return bRetVal;
}

bool MqttPlugin::m_MQTT_CMD(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();
    return m_ExecuteCmdString(args);
}

bool MqttPlugin::m_MQTT_SCRIPT(const std::string& args, std::stop_token st) const
{
    resetData();

    std::istringstream iss(args);
    std::string filename;
    std::string delayStr;
    iss >> filename >> delayStr;

    if (filename.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Usage: MQTT.SCRIPT <file> [delay_ms]"));
        return false;
    }

    uint32_t delayMs = 0;
    if (!delayStr.empty() && !numeric::str2uint32(delayStr, delayMs)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid delay:"); LOG_STRING(delayStr));
        return false;
    }

    const std::string fullPath = m_strArtefactsPath.empty() ? filename
                                                              : ufile::buildFilePath(m_strArtefactsPath, filename);

    std::ifstream file(fullPath);
    if (!file.is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Cannot open script file:"); LOG_STRING(fullPath));
        return false;
    }

    std::string line;
    size_t lineNo = 0;
    while (std::getline(file, line)) {
        ++lineNo;
        const std::string trimmed = ustring::trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        if (st.stop_requested()) {
            LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("SCRIPT stopped by request at line"); LOG_SIZET(lineNo));
            return false;
        }

        if (!m_ExecuteCmdString(trimmed)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SCRIPT failed at line"); LOG_SIZET(lineNo); LOG_STRING(":"); LOG_STRING(trimmed));
            return false;
        }

        if (delayMs > 0) {
            utime::delay_ms(delayMs);
        }
    }

    return true;
}
