#include "mqtt_plugin.hpp"
#include "uBoolEvaluator.hpp"
#include "uCommandExec.hpp"
#include "uPluginSettings.hpp"
#include "uGuiNotify.hpp"

#include <sstream>
#include <chrono>
#include <cctype>
#include <algorithm>
#include <cstring>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <poll.h> // TLS path only — mirrors how TCPIP itself bounds plain recv()/send() with poll() first

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
#define K_READ_BUFSIZE   "READ_BUFFER_SIZE"
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
#define SK_RBUF  "rb"
#define SK_RTOPIC "it"
#define SK_CID   "id"
#define SK_USER  "u"
#define SK_PASS  "pw"
#define SK_WTOPIC "wt"
#define SK_WPAY   "wp"
#define SK_WQOS   "wq"
#define SK_WRET   "wr"
#define SK_CLEAN  "cs"

static constexpr uint16_t kKeepAliveSeconds = 60;
static constexpr uint32_t kAckTimeoutMs = 5000;
static constexpr uint32_t kPacketContinuationTimeoutMs = 5000;

// -----------------------------------------------------------------------
// Ack-pending handoff between m_Send() and the following m_Receive() call
// on the same thread. thread_local rather than a plain mutable member:
// `MQTT.CMD < &` runs on its own background thread (concurrently with
// other MQTT.CMD activity on this same, persistent driver instance — see
// mqtt_plugin.hpp's "Session lifetime"), and thread_local storage is what
// keeps that background receive loop from racing a foreground
// `> ... | ...` pair's own pending-ack bookkeeping.
//
// Because m_Send() has no visibility into whether a paired m_Receive()
// call will actually follow (that depends on whether the script line has
// a trailing `| ...`, which only the interpreter's grammar sees), a QoS
// 1/2 PUBLISH (or SUBSCRIBE/UNSUBSCRIBE/PING) whose line omits its
// expected-ack pipe leaves that acknowledgement unread on the wire; the
// next `MQTT.CMD <` on this driver will then consume it (returning that
// confirmation text once instead of waiting for a live message) rather
// than anything worse — a self-limiting one-time mismatch, not persistent
// corruption. Always pair a `>` command with its `| expected` to avoid it.
// -----------------------------------------------------------------------
namespace
{
    thread_local bool     tl_bAwaitingAck    = false;
    thread_local uint8_t  tl_pendingAckType  = 0;
    thread_local uint16_t tl_pendingPacketId = 0;
}

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

    if (m_sessionEstablished && m_pTcpip && m_pTcpip->is_open()) {
        auto pkt = m_protocol.buildDisconnect();
        m_SendPacket(m_pTcpip, pkt, {}); // best-effort; DISCONNECT is not acknowledged by the broker
    }
    m_sessionEstablished = false;

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
    }
    m_pTcpip.reset();

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

bool MqttPlugin::setReadBufferSize(const std::string& bufSizeStr) const
{
    uint32_t sz = 0;
    if (!numeric::str2uint32(bufSizeStr, sz)) return false;
    if (sz == 0) return false;
    m_u32ReadBufferSize = sz;
    return true;
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
    sSettings.Bind(K_READ_BUFSIZE,    [this](const std::string& v) { return setReadBufferSize(v); });
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
// TLS setup — layered directly onto the real driver's already-connected
// socket (TCPIP has no TLS awareness of its own).
// -----------------------------------------------------------------------

bool MqttPlugin::m_SetupTls(const std::shared_ptr<TCPIP>& pDriver) const
{
    static bool sslInited = false;
    if (!sslInited) {
        SSL_library_init();
        SSL_load_error_strings();
        sslInited = true;
    }

    m_sslCtx = SSL_CTX_new(TLS_client_method());
    if (!m_sslCtx) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SSL_CTX_new failed"));
        return false;
    }
    SSL_CTX_set_min_proto_version(m_sslCtx, TLS1_2_VERSION);

    if (!m_strTlsCaPath.empty()) {
        if (SSL_CTX_load_verify_locations(m_sslCtx, m_strTlsCaPath.c_str(), nullptr) != 1) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CA Cert load failed:"); LOG_STRING(m_strTlsCaPath));
            return false;
        }
        SSL_CTX_set_verify(m_sslCtx, SSL_VERIFY_PEER, nullptr);
    } else {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("TLS enabled with no CA cert configured — server certificate will NOT be verified"));
        SSL_CTX_set_verify(m_sslCtx, SSL_VERIFY_NONE, nullptr);
    }

    if (!m_strTlsCertPath.empty() || !m_strTlsKeyPath.empty()) {
        if (m_strTlsCertPath.empty() || m_strTlsKeyPath.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("TLS client cert/key: both must be set for mutual TLS, only one was"));
            return false;
        }
        if (SSL_CTX_use_certificate_file(m_sslCtx, m_strTlsCertPath.c_str(), SSL_FILETYPE_PEM) != 1) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Client cert load failed:"); LOG_STRING(m_strTlsCertPath));
            return false;
        }
        if (SSL_CTX_use_PrivateKey_file(m_sslCtx, m_strTlsKeyPath.c_str(), SSL_FILETYPE_PEM) != 1) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Client key load failed:"); LOG_STRING(m_strTlsKeyPath));
            return false;
        }
        if (SSL_CTX_check_private_key(m_sslCtx) != 1) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Client cert/key mismatch"));
            return false;
        }
    }

    m_ssl = SSL_new(m_sslCtx);
    if (!m_ssl) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SSL_new failed"));
        return false;
    }

    SSL_set_tlsext_host_name(m_ssl, m_strHost.c_str());
    if (!m_strTlsCaPath.empty()) {
        SSL_set1_host(m_ssl, m_strHost.c_str());
    }
    SSL_set_fd(m_ssl, pDriver->nativeHandle());

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(5000);
    while (true) {
        const int rc = SSL_connect(m_ssl);
        if (rc == 1) break;

        const int sslErr = SSL_get_error(m_ssl, rc);
        if (sslErr != SSL_ERROR_WANT_READ && sslErr != SSL_ERROR_WANT_WRITE) {
            char errBuf[256];
            ERR_error_string_n(ERR_get_error(), errBuf, sizeof(errBuf));
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("TLS handshake failed:"); LOG_STRING(errBuf));
            return false;
        }
        const auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::milliseconds(0)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("TLS handshake timed out"));
            return false;
        }
        struct pollfd pfd{};
        pfd.fd = pDriver->nativeHandle();
        pfd.events = static_cast<short>(sslErr == SSL_ERROR_WANT_WRITE ? POLLOUT : POLLIN);
        ::poll(&pfd, 1, static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count()));
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("TLS handshake complete, cipher:"); LOG_STRING(SSL_get_cipher(m_ssl)));
    return true;
}

// -----------------------------------------------------------------------
// Physical I/O — routes through SSL if set up, otherwise straight to the
// real driver's tout_write()/tout_read().
// -----------------------------------------------------------------------

ICommDriver::Status MqttPlugin::m_PhysicalSend(const std::shared_ptr<const TCPIP>& shpDriver,
                                                std::span<const uint8_t> data, uint32_t timeoutMs) const
{
    if (!m_ssl) {
        auto res = shpDriver->tout_write(timeoutMs, data);
        return res.status;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    size_t totalWritten = 0;
    while (totalWritten < data.size()) {
        const int rc = SSL_write(m_ssl, data.data() + totalWritten, static_cast<int>(data.size() - totalWritten));
        if (rc > 0) {
            totalWritten += static_cast<size_t>(rc);
            continue;
        }
        const int sslErr = SSL_get_error(m_ssl, rc);
        if (sslErr != SSL_ERROR_WANT_READ && sslErr != SSL_ERROR_WANT_WRITE) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SSL_write failed, SSL error:"); LOG_INT32(sslErr));
            return ICommDriver::Status::WRITE_ERROR;
        }
        const auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::milliseconds(0)) {
            return ICommDriver::Status::WRITE_TIMEOUT;
        }
        struct pollfd pfd{};
        pfd.fd = shpDriver->nativeHandle();
        pfd.events = static_cast<short>(sslErr == SSL_ERROR_WANT_WRITE ? POLLOUT : POLLIN);
        ::poll(&pfd, 1, static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count()));
    }
    return ICommDriver::Status::SUCCESS;
}

ICommDriver::Status MqttPlugin::m_PhysicalRecv(const std::shared_ptr<const TCPIP>& shpDriver,
                                                std::span<uint8_t> buffer, uint32_t timeoutMs, size_t& outBytesRead) const
{
    outBytesRead = 0;

    if (!m_ssl) {
        auto res = shpDriver->tout_read(timeoutMs, buffer,
            ICommDriver::ReadOptions{.mode = ICommDriver::ReadMode::Exact});
        outBytesRead = res.bytes_read;
        return res.status;
    }

    struct pollfd pfd{};
    pfd.fd = shpDriver->nativeHandle();
    pfd.events = POLLIN;
    const int pollRc = ::poll(&pfd, 1, static_cast<int>(timeoutMs));
    if (pollRc <= 0) {
        return ICommDriver::Status::READ_TIMEOUT;
    }

    const int rc = SSL_read(m_ssl, buffer.data(), static_cast<int>(buffer.size()));
    if (rc > 0) {
        outBytesRead = static_cast<size_t>(rc);
        return ICommDriver::Status::SUCCESS;
    }

    const int sslErr = SSL_get_error(m_ssl, rc);
    if (sslErr == SSL_ERROR_WANT_READ || sslErr == SSL_ERROR_WANT_WRITE) {
        return ICommDriver::Status::READ_TIMEOUT; // indistinguishable from "nothing yet" to the caller
    }
    if (sslErr == SSL_ERROR_ZERO_RETURN) {
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("TLS peer closed the connection"));
        return ICommDriver::Status::READ_ERROR;
    }
    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SSL_read failed, SSL error:"); LOG_INT32(sslErr));
    return ICommDriver::Status::READ_ERROR;
}

// -----------------------------------------------------------------------
// Packet-level send/receive — every physical MQTT exchange goes through
// these two, which report the real, complete packet bytes to the GUI
// comm-dump panel by hand (see mqtt_plugin.hpp's class doc comment for why
// — supplying pfsend/pfrecv to the interpreter suppresses its own
// automatic dumping, so this plugin must produce an accurate replacement).
// -----------------------------------------------------------------------

ICommDriver::Status MqttPlugin::m_SendPacket(const std::shared_ptr<const TCPIP>& shpDriver,
                                              const std::vector<uint8_t>& packet, std::string_view xtra_params) const
{
    auto st = m_PhysicalSend(shpDriver, std::span<const uint8_t>(packet.data(), packet.size()), 5000);
    if (st == ICommDriver::Status::SUCCESS) {
        m_lastActivity = std::chrono::steady_clock::now();
        if (gui_mode_active()) {
            gui_notify_comm_dump(MQTT_PLUGIN_NAME, shpDriver->describeConnection(xtra_params),
                                  CommDir::Tx, packet.data(), static_cast<uint32_t>(packet.size()));
        }
    }
    return st;
}

ICommDriver::Status MqttPlugin::m_ReadPacket(const std::shared_ptr<const TCPIP>& shpDriver,
                                              std::vector<uint8_t>& packetOut, uint32_t timeoutMs,
                                              std::string_view xtra_params) const
{
    packetOut.clear();

    uint8_t firstByte = 0;
    {
        uint8_t buf[1];
        size_t got = 0;
        auto st = m_PhysicalRecv(shpDriver, std::span<uint8_t>(buf, 1), timeoutMs, got);
        if (st != ICommDriver::Status::SUCCESS || got == 0) {
            return ICommDriver::Status::READ_TIMEOUT;
        }
        firstByte = buf[0];
    }
    packetOut.push_back(firstByte);

    int multiplier = 1;
    uint32_t remLen = 0;
    while (true) {
        if (packetOut.size() >= 5) {
            return ICommDriver::Status::PROTOCOL_ERROR;
        }
        uint8_t buf[1];
        size_t got = 0;
        auto st = m_PhysicalRecv(shpDriver, std::span<uint8_t>(buf, 1), kPacketContinuationTimeoutMs, got);
        if (st != ICommDriver::Status::SUCCESS || got == 0) {
            return ICommDriver::Status::READ_TIMEOUT;
        }
        packetOut.push_back(buf[0]);
        remLen += (buf[0] & 0x7F) * multiplier;
        multiplier *= 128;
        if ((buf[0] & 0x80) == 0) break;
    }

    if (remLen > 0) {
        std::vector<uint8_t> payloadBuf(remLen);
        size_t totalRead = 0;
        while (totalRead < remLen) {
            size_t got = 0;
            auto st = m_PhysicalRecv(shpDriver,
                std::span<uint8_t>(payloadBuf.data() + totalRead, remLen - totalRead),
                kPacketContinuationTimeoutMs, got);
            if (st != ICommDriver::Status::SUCCESS || got == 0) {
                return ICommDriver::Status::READ_TIMEOUT;
            }
            totalRead += got;
        }
        packetOut.insert(packetOut.end(), payloadBuf.begin(), payloadBuf.end());
    }

    // One dump row per complete MQTT packet — see this function's doc
    // comment in mqtt_plugin.hpp for why not one row per physical byte read.
    if (gui_mode_active()) {
        gui_notify_comm_dump(MQTT_PLUGIN_NAME, shpDriver->describeConnection(xtra_params),
                              CommDir::Rx, packetOut.data(), static_cast<uint32_t>(packetOut.size()));
    }

    return ICommDriver::Status::SUCCESS;
}

bool MqttPlugin::m_WaitForAckPacket(const std::shared_ptr<const TCPIP>& shpDriver,
                                     uint8_t expectedType, uint16_t expectedPacketId,
                                     uint32_t timeoutMs, std::vector<uint8_t>& outPacket, std::string_view xtra_params) const
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
        auto st = m_ReadPacket(shpDriver, packet, remainingMs, xtra_params);
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
            continue;
        }
        outPacket = std::move(packet);
        return true;
    }
}

bool MqttPlugin::m_EnsureKeepAlive(const std::shared_ptr<const TCPIP>& shpDriver, std::string_view xtra_params) const
{
    const auto elapsed = std::chrono::steady_clock::now() - m_lastActivity;
    const auto threshold = std::chrono::milliseconds(static_cast<uint32_t>(kKeepAliveSeconds) * 800 /* 0.8*1000 */);
    if (elapsed < threshold) {
        return true;
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Keepalive due — sending PINGREQ"));
    auto pkt = m_protocol.buildPingReq();
    if (m_SendPacket(shpDriver, pkt, xtra_params) != ICommDriver::Status::SUCCESS) {
        return false;
    }
    std::vector<uint8_t> resp;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kAckTimeoutMs);
    while (true) {
        const auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::milliseconds(0)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Timed out waiting for PINGRESP"));
            return false;
        }
        const uint32_t remainingMs = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count());
        auto st = m_ReadPacket(shpDriver, resp, remainingMs, xtra_params);
        if (st != ICommDriver::Status::SUCCESS) return false;
        if (MqttProtocol::packetType(resp) == MqttProtocol::kPingResp) return true;
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Unexpected packet while waiting for PINGRESP: 0x");
                  LOG_HEX8(MqttProtocol::packetType(resp)));
    }
}

// -----------------------------------------------------------------------
// Driver factory — opens the real driver and completes both handshakes
// (TLS, then MQTT CONNECT/CONNACK) before ever handing it to the
// interpreter, matching KVCANPlugin's "open, then configure, then return"
// factory pattern.
// -----------------------------------------------------------------------

std::shared_ptr<TCPIP> MqttPlugin::m_OpenDriver(void) const
{
    if (m_pTcpip && m_pTcpip->is_open() && m_sessionEstablished) {
        return m_pTcpip;
    }

    if (m_strHost.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Host not configured — MQTT.CONFIG h=<host> first"));
        return nullptr;
    }

    // Fresh TCP connection needs a fresh MQTT session (and fresh TLS state,
    // if any leftover from a previous, now-dead connection).
    if (m_ssl) { SSL_shutdown(m_ssl); SSL_free(m_ssl); m_ssl = nullptr; }
    if (m_sslCtx) { SSL_CTX_free(m_sslCtx); m_sslCtx = nullptr; }
    m_sessionEstablished = false;

    m_pTcpip = std::make_shared<TCPIP>(m_strHost, m_u16Port, 5000, m_strHost);
    if (!m_pTcpip->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("TCPIP open failed"));
        m_pTcpip.reset();
        return nullptr;
    }

    if (m_bUseTls) {
        if (!m_SetupTls(m_pTcpip)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("TLS setup failed"));
            m_pTcpip.reset();
            return nullptr;
        }
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
    if (m_SendPacket(m_pTcpip, connectPkt, {}) != ICommDriver::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to send CONNECT"));
        m_pTcpip.reset();
        return nullptr;
    }

    std::vector<uint8_t> ackPacket;
    auto st = m_ReadPacket(m_pTcpip, ackPacket, kAckTimeoutMs, {});
    if (st != ICommDriver::Status::SUCCESS || MqttProtocol::packetType(ackPacket) != MqttProtocol::kConnAck) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Did not receive CONNACK"));
        m_pTcpip.reset();
        return nullptr;
    }

    auto result = m_protocol.decodeConnAck(ackPacket);
    if (!result.ok()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CONNACK refused, code:"); LOG_UINT32(result.returnCode));
        m_pTcpip.reset();
        return nullptr;
    }

    m_sessionEstablished = true;
    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Session established, sessionPresent="); LOG_BOOL(result.sessionPresent));
    return m_pTcpip;
}

// -----------------------------------------------------------------------
// Intermediary layer
// -----------------------------------------------------------------------

// Tokenizes on whitespace only. The shared CommScriptCommandValidator
// grammar (see uCommScriptCommandValidator.hpp) accepts an unquoted field
// verbatim (spaces included) ONLY if it contains no '"' character at all —
// any '"' forces the WHOLE field to match a single H/R/F-decorated or plain
// quoted string, which "PUBLISH "some payload" some/topic" does not. So
// there is no room for this layer to support its own embedded quoting on
// top of that; a payload or topic containing a literal '"' cannot be
// expressed through MQTT.CMD at all under the current grammar.
void MqttPlugin::m_TokenizeArgs(const std::string& text, std::vector<std::string>& outTokens)
{
    outTokens.clear();
    size_t i = 0;
    const size_t n = text.size();
    while (i < n) {
        while (i < n && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        if (i >= n) break;
        size_t start = i;
        while (i < n && !std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        outTokens.push_back(text.substr(start, i - start));
    }
}

ICommDriver::WriteResult MqttPlugin::m_Send(uint32_t u32WriteTimeout, std::span<const uint8_t> dataSpan,
                                             std::shared_ptr<const TCPIP> shpDriver, std::string_view xtra_params) const
{
    (void)u32WriteTimeout;
    ICommDriver::WriteResult result;

    tl_bAwaitingAck = false; // clear any state left by an earlier, unrelated m_Send() on this thread

    if (!shpDriver || !shpDriver->is_open()) {
        result.status = ICommDriver::Status::PORT_ACCESS;
        return result;
    }

    // The generic STRING_RAW conversion the interpreter applies to
    // everything after '>' (ustring::stringToVector(), used to build
    // dataSpan) appends a trailing NUL terminator by default. An MQTT
    // topic/payload string must not contain an embedded NUL (MQTT 3.1.1
    // §1.5.3 forbids U+0000 in its UTF-8 encoded strings) — Mosquitto (and
    // any spec-compliant broker) rejects a packet containing one as
    // malformed and drops the connection. Strip it before this text is
    // ever treated as an MQTT command.
    size_t len = dataSpan.size();
    while (len > 0 && dataSpan[len - 1] == 0) {
        --len;
    }
    std::string text(reinterpret_cast<const char*>(dataSpan.data()), len);
    text = ustring::trim(text);

    std::vector<std::string> tokens;
    m_TokenizeArgs(text, tokens);
    if (tokens.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("MQTT.CMD > requires a command: SUBSCRIBE, UNSUBSCRIBE, PING or PUBLISH"));
        result.status = ICommDriver::Status::INVALID_PARAM;
        return result;
    }

    std::string cmdKeyword = tokens[0];
    std::transform(cmdKeyword.begin(), cmdKeyword.end(), cmdKeyword.begin(),
                    [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    auto it = m_mapMqttCmds.find(cmdKeyword);
    if (it == m_mapMqttCmds.end()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown MQTT command:"); LOG_STRING(tokens[0]));
        result.status = ICommDriver::Status::INVALID_PARAM;
        return result;
    }

    const std::vector<std::string> cmdArgs(tokens.begin() + 1, tokens.end());
    if (!(this->*(it->second))(shpDriver, cmdArgs, xtra_params)) {
        result.status = ICommDriver::Status::OPERATION_FAILED;
        return result;
    }

    result.status = ICommDriver::Status::SUCCESS;
    result.bytes_written = dataSpan.size();
    return result;
}

ICommDriver::ReadResult MqttPlugin::m_Receive(uint32_t u32ReadTimeout, std::span<uint8_t> dataSpan,
                                               const ICommDriver::ReadOptions& options,
                                               std::shared_ptr<const TCPIP> shpDriver, std::string_view xtra_params) const
{
    (void)options;
    ICommDriver::ReadResult result;

    if (!shpDriver || !shpDriver->is_open()) {
        result.status = ICommDriver::Status::PORT_ACCESS;
        return result;
    }

    if (!tl_bAwaitingAck) {
        // Standalone "MQTT.CMD <" — see mqtt_plugin.hpp's class doc comment
        return m_DoStandaloneReceive(shpDriver, u32ReadTimeout, dataSpan, xtra_params);
    }

    const uint8_t  ackType  = tl_pendingAckType;
    const uint16_t packetId = tl_pendingPacketId;
    tl_bAwaitingAck = false; // consume-once

    // PING's PINGRESP carries no packet id — wait for it directly rather
    // than through m_WaitForAckPacket() (which assumes a Packet Identifier
    // field right after Remaining Length, which PINGRESP doesn't have).
    if (ackType == MqttProtocol::kPingResp) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(u32ReadTimeout);
        while (true) {
            const auto remaining = deadline - std::chrono::steady_clock::now();
            if (remaining <= std::chrono::milliseconds(0)) {
                result.status = ICommDriver::Status::READ_TIMEOUT;
                return result;
            }
            const uint32_t remainingMs = static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count());
            std::vector<uint8_t> packet;
            auto st = m_ReadPacket(shpDriver, packet, remainingMs, xtra_params);
            if (st != ICommDriver::Status::SUCCESS) {
                result.status = st;
                return result;
            }
            if (MqttProtocol::packetType(packet) == MqttProtocol::kPingResp) {
                static const char* pszPong = "PONG";
                const size_t len = std::min(dataSpan.size(), std::strlen(pszPong));
                std::memcpy(dataSpan.data(), pszPong, len);
                result.status = ICommDriver::Status::SUCCESS;
                result.bytes_read = len;
                return result;
            }
        }
    }

    std::vector<uint8_t> ack;
    if (!m_WaitForAckPacket(shpDriver, ackType, packetId, u32ReadTimeout, ack, xtra_params)) {
        result.status = ICommDriver::Status::READ_TIMEOUT;
        return result;
    }

    const char* pszConfirm = "";
    switch (ackType) {
        case MqttProtocol::kSubAck: {
            auto sub = m_protocol.decodeSubAck(ack);
            if (!sub.ok()) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SUBSCRIBE refused by broker"));
                result.status = ICommDriver::Status::PROTOCOL_ERROR;
                return result;
            }
            pszConfirm = "SUBACK";
            break;
        }
        case MqttProtocol::kUnsubAck: pszConfirm = "UNSUBACK"; break;
        case MqttProtocol::kPubAck:   pszConfirm = "PUBACK";   break;
        case MqttProtocol::kPubRec: {
            // QoS 2: send PUBREL, then wait PUBCOMP
            auto relPkt = m_protocol.buildPubRel(packetId);
            if (m_SendPacket(shpDriver, relPkt, xtra_params) != ICommDriver::Status::SUCCESS) {
                result.status = ICommDriver::Status::WRITE_ERROR;
                return result;
            }
            std::vector<uint8_t> comp;
            if (!m_WaitForAckPacket(shpDriver, MqttProtocol::kPubComp, packetId, u32ReadTimeout, comp, xtra_params)) {
                result.status = ICommDriver::Status::READ_TIMEOUT;
                return result;
            }
            pszConfirm = "PUBCOMP";
            break;
        }
        default:
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Internal error: unexpected pending ack type"));
            result.status = ICommDriver::Status::PROTOCOL_ERROR;
            return result;
    }

    const size_t len = std::min(dataSpan.size(), std::strlen(pszConfirm));
    std::memcpy(dataSpan.data(), pszConfirm, len);
    result.status = ICommDriver::Status::SUCCESS;
    result.bytes_read = len;
    return result;
}

ICommDriver::ReadResult MqttPlugin::m_DoStandaloneReceive(const std::shared_ptr<const TCPIP>& shpDriver,
                                                            uint32_t timeoutMs, std::span<uint8_t> buffer,
                                                            std::string_view xtra_params) const
{
    ICommDriver::ReadResult result;

    if (!m_EnsureKeepAlive(shpDriver, xtra_params)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Keepalive failed — session may be dead"));
        result.status = ICommDriver::Status::OPERATION_FAILED;
        return result;
    }

    std::vector<uint8_t> packet;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (true) {
        const auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::milliseconds(0)) {
            result.status = ICommDriver::Status::READ_TIMEOUT;
            return result;
        }
        const uint32_t remainingMs = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count());

        auto st = m_ReadPacket(shpDriver, packet, remainingMs, xtra_params);
        if (st != ICommDriver::Status::SUCCESS) {
            result.status = st;
            return result;
        }
        if (MqttProtocol::isPublish(packet)) break;
        if (MqttProtocol::packetType(packet) == MqttProtocol::kPingResp) {
            continue; // stray keepalive PINGRESP — not an error, just not what we're waiting for
        }
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Unexpected packet while waiting for PUBLISH: 0x");
                  LOG_HEX8(MqttProtocol::packetType(packet)));
    }

    auto msg = m_protocol.decodePublish(packet);

    if (msg.qos == 1) {
        auto pkt = m_protocol.buildPubAck(msg.packetId);
        m_SendPacket(shpDriver, pkt, xtra_params);
    } else if (msg.qos == 2) {
        auto pkt = m_protocol.buildPubRec(msg.packetId);
        if (m_SendPacket(shpDriver, pkt, xtra_params) == ICommDriver::Status::SUCCESS) {
            std::vector<uint8_t> rel;
            if (m_WaitForAckPacket(shpDriver, MqttProtocol::kPubRel, msg.packetId, timeoutMs, rel, xtra_params)) {
                auto comp = m_protocol.buildPubComp(msg.packetId);
                m_SendPacket(shpDriver, comp, xtra_params);
            } else {
                LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Failed to complete QoS 2 handshake for incoming PUBLISH on topic:"); LOG_STRING(msg.topic));
            }
        }
    }

    const std::string out = m_bReceiveIncludeTopic ? (msg.topic + ":" + msg.payload) : msg.payload;
    const size_t len = std::min(buffer.size(), out.size());
    std::memcpy(buffer.data(), out.data(), len);

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("PUBLISH received ["); LOG_STRING(msg.topic);
              LOG_STRING("] qos="); LOG_UINT32(msg.qos); LOG_STRING("bytes="); LOG_SIZET(msg.payload.size()));

    result.status = ICommDriver::Status::SUCCESS;
    result.bytes_read = len;
    return result;
}

// -----------------------------------------------------------------------
// MQTT sub-command handlers
// -----------------------------------------------------------------------

bool MqttPlugin::m_HandleSubscribe(const std::shared_ptr<const TCPIP>& shpDriver, const std::vector<std::string>& args, std::string_view xtra_params) const
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
        if (args[1].size() != 1 || args[1][0] < '0' || args[1][0] > '2') {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SUBSCRIBE: qos must be 0-2, got:"); LOG_STRING(args[1]));
            return false;
        }
        qos = static_cast<uint8_t>(args[1][0] - '0');
    }

    uint16_t packetId = 0;
    auto pkt = m_protocol.buildSubscribe(topic, qos, &packetId);
    if (m_SendPacket(shpDriver, pkt, xtra_params) != ICommDriver::Status::SUCCESS) return false;

    tl_bAwaitingAck    = true;
    tl_pendingAckType  = MqttProtocol::kSubAck;
    tl_pendingPacketId = packetId;
    return true;
}

bool MqttPlugin::m_HandleUnsubscribe(const std::shared_ptr<const TCPIP>& shpDriver, const std::vector<std::string>& args, std::string_view xtra_params) const
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

    uint16_t packetId = 0;
    auto pkt = m_protocol.buildUnsubscribe(topic, &packetId);
    if (m_SendPacket(shpDriver, pkt, xtra_params) != ICommDriver::Status::SUCCESS) return false;

    tl_bAwaitingAck    = true;
    tl_pendingAckType  = MqttProtocol::kUnsubAck;
    tl_pendingPacketId = packetId;
    return true;
}

bool MqttPlugin::m_HandlePing(const std::shared_ptr<const TCPIP>& shpDriver, const std::vector<std::string>& args, std::string_view xtra_params) const
{
    if (!args.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Usage: PING (no arguments)"));
        return false;
    }

    auto pkt = m_protocol.buildPingReq();
    if (m_SendPacket(shpDriver, pkt, xtra_params) != ICommDriver::Status::SUCCESS) return false;

    tl_bAwaitingAck    = true;
    tl_pendingAckType  = MqttProtocol::kPingResp;
    tl_pendingPacketId = 0; // PINGRESP carries no packet id
    return true;
}

bool MqttPlugin::m_HandlePublish(const std::shared_ptr<const TCPIP>& shpDriver, const std::vector<std::string>& args, std::string_view xtra_params) const
{
    // <payload> <topic>: the topic is always the LAST token (MQTT topics
    // never contain whitespace); everything before it is the payload,
    // rejoined with single spaces — this is what lets a multi-word payload
    // work without quoting, which the shared grammar this text arrived
    // through does not support here (see m_TokenizeArgs()'s doc comment).
    if (args.size() < 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Usage: PUBLISH <payload> <topic>"));
        return false;
    }
    const std::string& topic = args.back();
    if (topic.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("PUBLISH: empty topic"));
        return false;
    }
    std::string payload = args[0];
    for (size_t i = 1; i + 1 < args.size(); ++i) {
        payload += ' ';
        payload += args[i];
    }

    uint16_t packetId = 0;
    auto pkt = m_protocol.buildPublish(topic, payload, m_u16Qos, m_bRetain, &packetId);
    if (m_SendPacket(shpDriver, pkt, xtra_params) != ICommDriver::Status::SUCCESS) return false;

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("PUBLISH ["); LOG_STRING(topic);
              LOG_STRING("] qos="); LOG_UINT32(m_u16Qos); LOG_STRING("bytes="); LOG_SIZET(payload.size()));

    if (m_u16Qos == 0) {
        return true; // nothing to acknowledge
    }

    tl_bAwaitingAck    = true;
    tl_pendingAckType  = (m_u16Qos == 1) ? MqttProtocol::kPubAck : MqttProtocol::kPubRec;
    tl_pendingPacketId = packetId;
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
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Architecture: MqttProtocol (protocol) / TCPIP (real driver, undecorated) / this plugin's m_Send()/m_Receive() (pfsend/pfrecv)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set the broker host, port, TLS, auth, Will and transfer parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [h=host] [p=port] [q=qos] [t=tls] [r=retain] [ca=capath] [crt=certpath] [key=keypath]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [rt=read_tout] [rb=read_bufsize] [id=clientid] [u=username] [pw=password] [cs=cleansession]"));
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
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         PUBLISH's payload may contain spaces (the topic is always the LAST token); no quoting is available here."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         Always pair a QoS>0 PUBLISH/SUBSCRIBE/UNSUBSCRIBE/PING with its '| expected' — an omitted ack is read (and mismatched) by the next MQTT.CMD <."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         The GUI comm-dump panel shows the real bytes exchanged with the broker (one row per complete MQTT packet)."));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : run several MQTT.CMD-style lines from a file over the same session"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : scriptpathname [|delay]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : MQTT.SCRIPT script.txt"));
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
        else if (key == SK_RBUF)  { if (!setReadBufferSize(val)) bRetVal = false; }
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

// -----------------------------------------------------------------------
// MQTT.CMD / MQTT.SCRIPT — see class doc comment (mqtt_plugin.hpp)
// -----------------------------------------------------------------------

bool MqttPlugin::m_MQTT_CMD(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    return ucmdexec::generic_cmd(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<TCPIP> { return m_OpenDriver(); },
        MQTT_PLUGIN_NAME,
        m_u32ReadBufferSize, m_u32ReadTimeout, LOG_HDR, &m_strResultData,
        // Route every send/receive through m_Send()/m_Receive() instead of
        // the interpreter's default driver->tout_write()/tout_read() — see
        // their doc comments in mqtt_plugin.hpp. This is what turns the
        // MQTT.CMD argument text into an actual protocol exchange, and
        // what makes the GUI comm-dump panel show the real driver's actual
        // wire traffic instead of the pre-parse text.
        [this](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const TCPIP> drv, std::string_view x) {
            return m_Send(t, d, drv, x);
        },
        [this](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const TCPIP> drv, std::string_view x) {
            return m_Receive(t, b, o, drv, x);
        });
}

bool MqttPlugin::m_MQTT_SCRIPT(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    return ucmdexec::generic_script(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<TCPIP> { return m_OpenDriver(); },
        MQTT_PLUGIN_NAME,
        m_strArtefactsPath, m_u32ReadBufferSize, m_u32ReadTimeout, LOG_HDR,
        // Same rationale as m_MQTT_CMD() above.
        [this](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const TCPIP> drv, std::string_view x) {
            return m_Send(t, d, drv, x);
        },
        [this](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const TCPIP> drv, std::string_view x) {
            return m_Receive(t, b, o, drv, x);
        });
}
