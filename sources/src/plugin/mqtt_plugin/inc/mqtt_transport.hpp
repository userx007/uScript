#ifndef MQTT_TRANSPORT_HPP
#define MQTT_TRANSPORT_HPP

#include "uTcpip.hpp"
#include "ICommDriver.hpp"        // reused only for its Status enum / to_string() and CommDetails via ICommDumpProtocol.hpp
#include <memory>
#include <string>
#include <span>
#include <cstdio>

typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;

/**
 * @brief Pure byte transport: TCP, optionally wrapped in TLS.
 *
 * This is the "driver side" of the plugin's three-way split (see
 * mqtt_plugin.hpp's class doc comment). It knows nothing about MQTT at
 * all — no packet types, no Remaining Length, no packet ids. It is exactly
 * as MQTT-agnostic as the plain TCPIP driver it wraps; the only thing it
 * adds on top of TCPIP is optional TLS, wired directly onto TCPIP's socket
 * (see setupTls()) since TCPIP itself has no TLS awareness.
 *
 * MqttPlugin is the only thing that gives these bytes meaning: it builds
 * MQTT packets via MqttProtocol, hands the resulting bytes to send(), and
 * assembles complete packets from whatever recv() returns before handing
 * those to MqttProtocol to decode.
 */
class MqttTransport
{
public:
    struct Config {
        std::string host;
        uint16_t port;
        uint32_t connectTimeoutMs;
        bool useTls;
        std::string caCertPath;     // empty: server certificate chain is NOT verified — see setupTls()
        std::string clientCertPath; // both cert+key set: mutual TLS (Mosquitto's require_certificate)
        std::string clientKeyPath;
    };

    MqttTransport() = default;

    /// strIdentityLabel: display label for the GUI comm-dump panel (see describeConnection()).
    explicit MqttTransport(std::string strIdentityLabel)
        : m_strIdentityLabel(std::move(strIdentityLabel))
    {
    }

    ~MqttTransport();

    ICommDriver::Status open(const Config& config);
    void close();
    bool isOpen() const;

    /// Sends the entirety of 'data', looping internally until all bytes are
    /// written or timeoutMs elapses.
    ICommDriver::Status send(std::span<const uint8_t> data, uint32_t timeoutMs) const;

    /// Reads at most buffer.size() bytes, waiting up to timeoutMs for the
    /// FIRST byte to become available; returns whatever is available after
    /// that (mirrors TCPIP::tout_read()'s "poll once, read once" shape) —
    /// not a guaranteed-exact-size read. Callers that need an exact byte
    /// count (MQTT packet framing) call this in a loop themselves; see
    /// MqttPlugin::m_readPacket().
    ICommDriver::Status recv(std::span<uint8_t> buffer, uint32_t timeoutMs, size_t& outBytesRead) const;

    CommDetails describeConnection() const
    {
        char label[k_labelSize];
        std::snprintf(label, sizeof(label), "MQTT %s %s:%u",
                      !m_strIdentityLabel.empty() ? m_strIdentityLabel.c_str() : "session",
                      m_config.host.c_str(), m_config.port);
        return commdump_details(CommFamily::NET, label);
    }

private:
    std::shared_ptr<TCPIP> m_pTcpip;
    bool m_connected = false;

    // TLS context — see setupTls(). Every send()/recv() transparently
    // routes through SSL_write()/SSL_read() instead of TCPIP::tout_write()/
    // tout_read() when these are non-null.
    SSL_CTX* m_sslCtx = nullptr;
    SSL* m_ssl = nullptr;

    Config m_config;
    std::string m_strIdentityLabel;

    ICommDriver::Status setupTls();
};

#endif // MQTT_TRANSPORT_HPP
