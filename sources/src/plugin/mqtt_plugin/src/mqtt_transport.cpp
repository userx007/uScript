#include "mqtt_transport.hpp"
#include "uLogger.hpp"
#include <chrono>

#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LOG_HDR "MQTT_XPORT  |"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>

// POSIX poll(), used only by the TLS path — mirrors how TCPIP itself bounds
// plain recv()/send() with poll() first, just for SSL_read()/SSL_write().
#include <poll.h>

MqttTransport::~MqttTransport()
{
    close();
}

ICommDriver::Status MqttTransport::open(const Config& config)
{
    if (m_pTcpip) {
        close();
    }

    m_config = config;
    m_connected = false;
    m_sslCtx = nullptr;
    m_ssl = nullptr;

    m_pTcpip = std::make_shared<TCPIP>();
    auto tcpStatus = m_pTcpip->open(m_config.host, m_config.port, m_config.connectTimeoutMs);
    if (tcpStatus != ICommDriver::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("TCPIP Open failed"));
        return ICommDriver::Status::PORT_ACCESS;
    }

    if (m_config.useTls) {
        auto tlsStatus = setupTls();
        if (tlsStatus != ICommDriver::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("TLS Setup failed"));
            close();
            return ICommDriver::Status::OPERATION_FAILED;
        }
    }

    m_connected = true;
    return ICommDriver::Status::SUCCESS;
}

void MqttTransport::close()
{
    if (m_ssl) {
        SSL_shutdown(m_ssl);
        SSL_free(m_ssl); // also detaches from the fd; TCPIP still owns the fd itself
        m_ssl = nullptr;
    }
    if (m_sslCtx) {
        SSL_CTX_free(m_sslCtx);
        m_sslCtx = nullptr;
    }
    if (m_pTcpip) {
        m_pTcpip->close();
        m_pTcpip.reset();
    }
    m_connected = false;
}

bool MqttTransport::isOpen() const
{
    return m_connected && m_pTcpip && m_pTcpip->is_open();
}

ICommDriver::Status MqttTransport::setupTls()
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
        return ICommDriver::Status::OPERATION_FAILED;
    }
    SSL_CTX_set_min_proto_version(m_sslCtx, TLS1_2_VERSION);

    if (!m_config.caCertPath.empty()) {
        if (SSL_CTX_load_verify_locations(m_sslCtx, m_config.caCertPath.c_str(), nullptr) != 1) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CA Cert load failed:"); LOG_STRING(m_config.caCertPath));
            return ICommDriver::Status::OPERATION_FAILED;
        }
        SSL_CTX_set_verify(m_sslCtx, SSL_VERIFY_PEER, nullptr);
    } else {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("TLS enabled with no CA cert configured — server certificate will NOT be verified"));
        SSL_CTX_set_verify(m_sslCtx, SSL_VERIFY_NONE, nullptr);
    }

    if (!m_config.clientCertPath.empty() || !m_config.clientKeyPath.empty()) {
        if (m_config.clientCertPath.empty() || m_config.clientKeyPath.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("TLS client cert/key: both must be set for mutual TLS, only one was"));
            return ICommDriver::Status::INVALID_PARAM;
        }
        if (SSL_CTX_use_certificate_file(m_sslCtx, m_config.clientCertPath.c_str(), SSL_FILETYPE_PEM) != 1) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Client cert load failed:"); LOG_STRING(m_config.clientCertPath));
            return ICommDriver::Status::OPERATION_FAILED;
        }
        if (SSL_CTX_use_PrivateKey_file(m_sslCtx, m_config.clientKeyPath.c_str(), SSL_FILETYPE_PEM) != 1) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Client key load failed:"); LOG_STRING(m_config.clientKeyPath));
            return ICommDriver::Status::OPERATION_FAILED;
        }
        if (SSL_CTX_check_private_key(m_sslCtx) != 1) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Client cert/key mismatch"));
            return ICommDriver::Status::OPERATION_FAILED;
        }
    }

    m_ssl = SSL_new(m_sslCtx);
    if (!m_ssl) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SSL_new failed"));
        return ICommDriver::Status::OPERATION_FAILED;
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
        if (rc == 1) {
            break;
        }
        const int sslErr = SSL_get_error(m_ssl, rc);
        if (sslErr != SSL_ERROR_WANT_READ && sslErr != SSL_ERROR_WANT_WRITE) {
            char errBuf[256];
            ERR_error_string_n(ERR_get_error(), errBuf, sizeof(errBuf));
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("TLS handshake failed:"); LOG_STRING(errBuf));
            return ICommDriver::Status::OPERATION_FAILED;
        }
        const auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::milliseconds(0)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("TLS handshake timed out"));
            return ICommDriver::Status::OPERATION_FAILED;
        }
        struct pollfd pfd{};
        pfd.fd = m_pTcpip->nativeHandle();
        pfd.events = static_cast<short>(sslErr == SSL_ERROR_WANT_WRITE ? POLLOUT : POLLIN);
        ::poll(&pfd, 1, static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count()));
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("TLS handshake complete, cipher:"); LOG_STRING(SSL_get_cipher(m_ssl)));
    return ICommDriver::Status::SUCCESS;
}

ICommDriver::Status MqttTransport::send(std::span<const uint8_t> data, uint32_t timeoutMs) const
{
    if (!isOpen()) {
        return ICommDriver::Status::PORT_ACCESS;
    }

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
        ::poll(&pfd, 1, static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count()));
    }
    return ICommDriver::Status::SUCCESS;
}

ICommDriver::Status MqttTransport::recv(std::span<uint8_t> buffer, uint32_t timeoutMs, size_t& outBytesRead) const
{
    outBytesRead = 0;

    if (!isOpen()) {
        return ICommDriver::Status::PORT_ACCESS;
    }

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
        // poll() said data was ready but SSL needed a protocol-level round-trip
        // first (e.g. a session ticket) — indistinguishable from "nothing yet"
        // to the caller; its own retry loop will call back in.
        return ICommDriver::Status::READ_TIMEOUT;
    }
    if (sslErr == SSL_ERROR_ZERO_RETURN) {
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("TLS peer closed the connection"));
        return ICommDriver::Status::READ_ERROR;
    }

    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SSL_read failed, SSL error:"); LOG_INT32(sslErr));
    return ICommDriver::Status::READ_ERROR;
}
