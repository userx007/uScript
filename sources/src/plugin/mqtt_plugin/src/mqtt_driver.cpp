#include "mqtt_driver.hpp"
#include "uLogger.hpp"
#include "uGuiNotify.hpp"
#include "uString.hpp"

#include <cctype>
#include <algorithm>
#include <cstring>

#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LOG_HDR "MQTT_DRV    |"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <poll.h> // TLS path only — mirrors how TCPIP itself bounds plain recv()/send() with poll() first

static constexpr uint32_t kAckTimeoutMs = 5000;
static constexpr uint32_t kPacketContinuationTimeoutMs = 5000;
static constexpr const char* kPluginNameForDump = "MQTT";

// -----------------------------------------------------------------------
// Ack-pending handoff between send() and the following receive() call on
// the same thread. thread_local rather than a plain mutable member:
// `MQTT.CMD < &` runs on its own background thread (concurrently with
// other MQTT.CMD activity on this same, persistent driver instance — see
// mqtt_plugin.hpp's "Session lifetime"), and thread_local storage is what
// keeps that background receive loop from racing a foreground
// `> ... | ...` pair's own pending-ack bookkeeping.
//
// Because send() has no visibility into whether a paired receive() call
// will actually follow (that depends on whether the script line has a
// trailing `| ...`, which only the interpreter's grammar sees), a QoS 1/2
// PUBLISH (or SUBSCRIBE/UNSUBSCRIBE/PING) whose line omits its expected-ack
// pipe leaves that acknowledgement unread on the wire; the next
// `MQTT.CMD <` on this driver will then consume it (returning that
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

MqttDriver::MqttDriver(Config config)
    : m_config(std::move(config))
{
    if (m_config.strInstanceName.empty()) {
        m_config.strInstanceName = kPluginNameForDump;
    }
    m_mapMqttCmds.insert({"SUBSCRIBE",   &MqttDriver::m_HandleSubscribe});
    m_mapMqttCmds.insert({"UNSUBSCRIBE", &MqttDriver::m_HandleUnsubscribe});
    m_mapMqttCmds.insert({"PING",        &MqttDriver::m_HandlePing});
    m_mapMqttCmds.insert({"PUBLISH",     &MqttDriver::m_HandlePublish});
}

MqttDriver::~MqttDriver()
{
    close();
}

void MqttDriver::close()
{
    if (m_sessionEstablished && m_pTcpip && m_pTcpip->is_open()) {
        auto pkt = m_protocol.buildDisconnect();
        m_SendPacket(pkt, {}); // best-effort; DISCONNECT is not acknowledged by the broker
    }
    m_sessionEstablished = false;

    if (m_ssl) { SSL_shutdown(m_ssl); SSL_free(m_ssl); m_ssl = nullptr; }
    if (m_sslCtx) { SSL_CTX_free(m_sslCtx); m_sslCtx = nullptr; }

    if (m_pTcpip) {
        m_pTcpip->close();
    }
    m_pTcpip.reset();
}

bool MqttDriver::is_open() const
{
    return m_pTcpip && m_pTcpip->is_open() && m_sessionEstablished;
}

CommDetails MqttDriver::describeConnection(std::string_view xtra_params) const
{
    return m_pTcpip->describeConnection(xtra_params);
}

ICommDriver::WriteResult MqttDriver::tout_write(uint32_t u32WriteTimeout, std::span<const uint8_t> buffer,
                                                 std::string_view xtra_params) const
{
    // Thin passthrough — see class doc comment. Never actually used by
    // MqttPlugin, which always goes through send() instead.
    return m_pTcpip->tout_write(u32WriteTimeout, buffer, xtra_params);
}

ICommDriver::ReadResult MqttDriver::tout_read(uint32_t u32ReadTimeout, std::span<uint8_t> buffer,
                                               const ICommDriver::ReadOptions& options, std::string_view xtra_params) const
{
    return m_pTcpip->tout_read(u32ReadTimeout, buffer, options, xtra_params);
}

// -----------------------------------------------------------------------
// TLS setup — layered directly onto the real driver's already-connected
// socket (TCPIP has no TLS awareness of its own).
// -----------------------------------------------------------------------

bool MqttDriver::m_SetupTls()
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

    if (!m_config.caCertPath.empty()) {
        if (SSL_CTX_load_verify_locations(m_sslCtx, m_config.caCertPath.c_str(), nullptr) != 1) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CA Cert load failed:"); LOG_STRING(m_config.caCertPath));
            return false;
        }
        SSL_CTX_set_verify(m_sslCtx, SSL_VERIFY_PEER, nullptr);
    } else {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("TLS enabled with no CA cert configured — server certificate will NOT be verified"));
        SSL_CTX_set_verify(m_sslCtx, SSL_VERIFY_NONE, nullptr);
    }

    if (!m_config.clientCertPath.empty() || !m_config.clientKeyPath.empty()) {
        if (m_config.clientCertPath.empty() || m_config.clientKeyPath.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("TLS client cert/key: both must be set for mutual TLS, only one was"));
            return false;
        }
        if (SSL_CTX_use_certificate_file(m_sslCtx, m_config.clientCertPath.c_str(), SSL_FILETYPE_PEM) != 1) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Client cert load failed:"); LOG_STRING(m_config.clientCertPath));
            return false;
        }
        if (SSL_CTX_use_PrivateKey_file(m_sslCtx, m_config.clientKeyPath.c_str(), SSL_FILETYPE_PEM) != 1) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Client key load failed:"); LOG_STRING(m_config.clientKeyPath));
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

    SSL_set_tlsext_host_name(m_ssl, m_config.host.c_str());
    if (!m_config.caCertPath.empty()) {
        SSL_set1_host(m_ssl, m_config.host.c_str());
    }
    SSL_set_fd(m_ssl, m_pTcpip->nativeHandle());

    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(m_config.connectTimeoutMs ? m_config.connectTimeoutMs : 5000);
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
        pfd.fd = m_pTcpip->nativeHandle();
        pfd.events = static_cast<short>(sslErr == SSL_ERROR_WANT_WRITE ? POLLOUT : POLLIN);
        ::poll(&pfd, 1, static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count()));
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("TLS handshake complete, cipher:"); LOG_STRING(SSL_get_cipher(m_ssl)));
    return true;
}

// -----------------------------------------------------------------------
// Session establishment
// -----------------------------------------------------------------------

bool MqttDriver::open()
{
    if (m_config.host.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Host not configured"));
        return false;
    }

    m_pTcpip = std::make_shared<TCPIP>(m_config.host, m_config.port, m_config.connectTimeoutMs, m_config.host);
    if (!m_pTcpip->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("TCPIP open failed"));
        m_pTcpip.reset();
        return false;
    }

    if (m_config.useTls) {
        if (!m_SetupTls()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("TLS setup failed"));
            m_pTcpip.reset();
            return false;
        }
    }

    m_protocol.resetPacketIdSequence();

    MqttProtocol::ConnectParams cp;
    cp.clientId     = m_config.clientId;
    cp.username     = m_config.username;
    cp.password     = m_config.password;
    cp.willTopic    = m_config.willTopic;
    cp.willPayload  = m_config.willPayload;
    cp.willQos      = m_config.willQos;
    cp.willRetain   = m_config.willRetain;
    cp.cleanSession = m_config.cleanSession;
    cp.keepAlive    = m_config.keepAlive;

    auto connectPkt = m_protocol.buildConnect(cp);
    if (m_SendPacket(connectPkt, {}) != ICommDriver::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to send CONNECT"));
        m_pTcpip.reset();
        return false;
    }

    std::vector<uint8_t> ackPacket;
    auto st = m_ReadPacket(ackPacket, kAckTimeoutMs, {});
    if (st != ICommDriver::Status::SUCCESS || MqttProtocol::packetType(ackPacket) != MqttProtocol::kConnAck) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Did not receive CONNACK"));
        m_pTcpip.reset();
        return false;
    }

    auto result = m_protocol.decodeConnAck(ackPacket);
    if (!result.ok()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CONNACK refused, code:"); LOG_UINT32(result.returnCode));
        m_pTcpip.reset();
        return false;
    }

    m_sessionEstablished = true;
    m_lastActivity = std::chrono::steady_clock::now();
    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Session established, sessionPresent="); LOG_BOOL(result.sessionPresent));
    return true;
}

// -----------------------------------------------------------------------
// Physical I/O — routes through SSL if set up, otherwise straight to the
// real driver's tout_write()/tout_read().
// -----------------------------------------------------------------------

ICommDriver::Status MqttDriver::m_PhysicalSend(std::span<const uint8_t> data, uint32_t timeoutMs) const
{
    if (!m_ssl) {
        auto res = m_pTcpip->tout_write(timeoutMs, data);
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
        pfd.fd = m_pTcpip->nativeHandle();
        pfd.events = static_cast<short>(sslErr == SSL_ERROR_WANT_WRITE ? POLLOUT : POLLIN);
        ::poll(&pfd, 1, static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count()));
    }
    return ICommDriver::Status::SUCCESS;
}

ICommDriver::Status MqttDriver::m_PhysicalRecv(std::span<uint8_t> buffer, uint32_t timeoutMs, size_t& outBytesRead) const
{
    outBytesRead = 0;

    if (!m_ssl) {
        auto res = m_pTcpip->tout_read(timeoutMs, buffer,
            ICommDriver::ReadOptions{.mode = ICommDriver::ReadMode::Exact});
        outBytesRead = res.bytes_read;
        return res.status;
    }

    struct pollfd pfd{};
    pfd.fd = m_pTcpip->nativeHandle();
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
// comm-dump panel by hand (see mqtt_driver.hpp's class doc comment for why
// — supplying send()/receive() as pfsend/pfrecv suppresses the
// interpreter's own automatic dumping, so this class must produce an
// accurate replacement).
// -----------------------------------------------------------------------

ICommDriver::Status MqttDriver::m_SendPacket(const std::vector<uint8_t>& packet, std::string_view xtra_params) const
{
    auto st = m_PhysicalSend(std::span<const uint8_t>(packet.data(), packet.size()), 5000);
    if (st == ICommDriver::Status::SUCCESS) {
        m_lastActivity = std::chrono::steady_clock::now();
        if (gui_mode_active()) {
            gui_notify_comm_dump(m_config.strInstanceName, describeConnection(xtra_params),
                                  CommDir::Tx, packet.data(), static_cast<uint32_t>(packet.size()));
        }
    }
    return st;
}

ICommDriver::Status MqttDriver::m_ReadPacket(std::vector<uint8_t>& packetOut, uint32_t timeoutMs, std::string_view xtra_params) const
{
    packetOut.clear();

    uint8_t firstByte = 0;
    {
        uint8_t buf[1];
        size_t got = 0;
        auto st = m_PhysicalRecv(std::span<uint8_t>(buf, 1), timeoutMs, got);
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
        auto st = m_PhysicalRecv(std::span<uint8_t>(buf, 1), kPacketContinuationTimeoutMs, got);
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
            auto st = m_PhysicalRecv(
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
    // comment in mqtt_driver.hpp for why not one row per physical byte read.
    if (gui_mode_active()) {
        gui_notify_comm_dump(m_config.strInstanceName, describeConnection(xtra_params),
                              CommDir::Rx, packetOut.data(), static_cast<uint32_t>(packetOut.size()));
    }

    return ICommDriver::Status::SUCCESS;
}

bool MqttDriver::m_WaitForAckPacket(uint8_t expectedType, uint16_t expectedPacketId,
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
        auto st = m_ReadPacket(packet, remainingMs, xtra_params);
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

bool MqttDriver::m_EnsureKeepAlive(std::string_view xtra_params) const
{
    if (m_config.keepAlive == 0) {
        return true;
    }
    const auto elapsed = std::chrono::steady_clock::now() - m_lastActivity;
    const auto threshold = std::chrono::milliseconds(static_cast<uint32_t>(m_config.keepAlive) * 800 /* 0.8*1000 */);
    if (elapsed < threshold) {
        return true;
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Keepalive due — sending PINGREQ"));
    auto pkt = m_protocol.buildPingReq();
    if (m_SendPacket(pkt, xtra_params) != ICommDriver::Status::SUCCESS) {
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
        auto st = m_ReadPacket(resp, remainingMs, xtra_params);
        if (st != ICommDriver::Status::SUCCESS) return false;
        if (MqttProtocol::packetType(resp) == MqttProtocol::kPingResp) return true;
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Unexpected packet while waiting for PINGRESP: 0x");
                  LOG_HEX8(MqttProtocol::packetType(resp)));
    }
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
void MqttDriver::m_TokenizeArgs(std::span<const uint8_t> dataSpan, std::vector<std::string>& outTokens)
{
    outTokens.clear();

    // Strip a trailing NUL — see this function's doc comment in mqtt_driver.hpp.
    size_t len = dataSpan.size();
    while (len > 0 && dataSpan[len - 1] == 0) {
        --len;
    }
    std::string text(reinterpret_cast<const char*>(dataSpan.data()), len);
    text = ustring::trim(text);

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

ICommDriver::WriteResult MqttDriver::send(uint32_t u32WriteTimeout, std::span<const uint8_t> dataSpan,
                                           std::string_view xtra_params) const
{
    (void)u32WriteTimeout;
    ICommDriver::WriteResult result;

    tl_bAwaitingAck = false; // clear any state left by an earlier, unrelated send() on this thread

    if (!is_open()) {
        result.status = ICommDriver::Status::PORT_ACCESS;
        return result;
    }

    std::vector<std::string> tokens;
    m_TokenizeArgs(dataSpan, tokens);
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
    if (!(this->*(it->second))(cmdArgs, xtra_params)) {
        result.status = ICommDriver::Status::OPERATION_FAILED;
        return result;
    }

    result.status = ICommDriver::Status::SUCCESS;
    result.bytes_written = dataSpan.size();
    return result;
}

ICommDriver::ReadResult MqttDriver::receive(uint32_t u32ReadTimeout, std::span<uint8_t> dataSpan,
                                             const ICommDriver::ReadOptions& options, std::string_view xtra_params) const
{
    (void)options;
    ICommDriver::ReadResult result;

    if (!is_open()) {
        result.status = ICommDriver::Status::PORT_ACCESS;
        return result;
    }

    if (!tl_bAwaitingAck) {
        // Standalone "MQTT.CMD <" — see class doc comment
        return m_DoStandaloneReceive(u32ReadTimeout, dataSpan, xtra_params);
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
            auto st = m_ReadPacket(packet, remainingMs, xtra_params);
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
    if (!m_WaitForAckPacket(ackType, packetId, u32ReadTimeout, ack, xtra_params)) {
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
            if (m_SendPacket(relPkt, xtra_params) != ICommDriver::Status::SUCCESS) {
                result.status = ICommDriver::Status::WRITE_ERROR;
                return result;
            }
            std::vector<uint8_t> comp;
            if (!m_WaitForAckPacket(MqttProtocol::kPubComp, packetId, u32ReadTimeout, comp, xtra_params)) {
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

ICommDriver::ReadResult MqttDriver::m_DoStandaloneReceive(uint32_t timeoutMs, std::span<uint8_t> buffer, std::string_view xtra_params) const
{
    ICommDriver::ReadResult result;

    if (!m_EnsureKeepAlive(xtra_params)) {
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

        auto st = m_ReadPacket(packet, remainingMs, xtra_params);
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
        m_SendPacket(pkt, xtra_params);
    } else if (msg.qos == 2) {
        auto pkt = m_protocol.buildPubRec(msg.packetId);
        if (m_SendPacket(pkt, xtra_params) == ICommDriver::Status::SUCCESS) {
            std::vector<uint8_t> rel;
            if (m_WaitForAckPacket(MqttProtocol::kPubRel, msg.packetId, timeoutMs, rel, xtra_params)) {
                auto comp = m_protocol.buildPubComp(msg.packetId);
                m_SendPacket(comp, xtra_params);
            } else {
                LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Failed to complete QoS 2 handshake for incoming PUBLISH on topic:"); LOG_STRING(msg.topic));
            }
        }
    }

    const std::string out = m_config.receiveIncludeTopic ? (msg.topic + " " + msg.payload) : msg.payload;
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

bool MqttDriver::m_HandleSubscribe(const std::vector<std::string>& args, std::string_view xtra_params) const
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
    uint8_t qos = m_config.qos;
    if (args.size() == 2) {
        if (args[1].size() != 1 || args[1][0] < '0' || args[1][0] > '2') {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SUBSCRIBE: qos must be 0-2, got:"); LOG_STRING(args[1]));
            return false;
        }
        qos = static_cast<uint8_t>(args[1][0] - '0');
    }

    uint16_t packetId = 0;
    auto pkt = m_protocol.buildSubscribe(topic, qos, &packetId);
    if (m_SendPacket(pkt, xtra_params) != ICommDriver::Status::SUCCESS) return false;

    tl_bAwaitingAck    = true;
    tl_pendingAckType  = MqttProtocol::kSubAck;
    tl_pendingPacketId = packetId;
    return true;
}

bool MqttDriver::m_HandleUnsubscribe(const std::vector<std::string>& args, std::string_view xtra_params) const
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
    if (m_SendPacket(pkt, xtra_params) != ICommDriver::Status::SUCCESS) return false;

    tl_bAwaitingAck    = true;
    tl_pendingAckType  = MqttProtocol::kUnsubAck;
    tl_pendingPacketId = packetId;
    return true;
}

bool MqttDriver::m_HandlePing(const std::vector<std::string>& args, std::string_view xtra_params) const
{
    if (!args.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Usage: PING (no arguments)"));
        return false;
    }

    auto pkt = m_protocol.buildPingReq();
    if (m_SendPacket(pkt, xtra_params) != ICommDriver::Status::SUCCESS) return false;

    tl_bAwaitingAck    = true;
    tl_pendingAckType  = MqttProtocol::kPingResp;
    tl_pendingPacketId = 0; // PINGRESP carries no packet id
    return true;
}

bool MqttDriver::m_HandlePublish(const std::vector<std::string>& args, std::string_view xtra_params) const
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
    auto pkt = m_protocol.buildPublish(topic, payload, m_config.qos, m_config.retain, &packetId);
    if (m_SendPacket(pkt, xtra_params) != ICommDriver::Status::SUCCESS) return false;

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("PUBLISH ["); LOG_STRING(topic);
              LOG_STRING("] qos="); LOG_UINT32(m_config.qos); LOG_STRING("bytes="); LOG_SIZET(payload.size()));

    if (m_config.qos == 0) {
        return true; // nothing to acknowledge
    }

    tl_bAwaitingAck    = true;
    tl_pendingAckType  = (m_config.qos == 1) ? MqttProtocol::kPubAck : MqttProtocol::kPubRec;
    tl_pendingPacketId = packetId;
    return true;
}
