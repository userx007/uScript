#include "uWebSocket.hpp"
#include "uLogger.hpp"
#include "uKmpMatch.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <optional>
#include <random>
#include <sstream>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR   "WS_DRV      |"
#define LOG_HDR  LOG_STRING(LT_HDR)


namespace
{
    // RFC 6455 s.1.3 - fixed GUID concatenated with the client's Sec-WebSocket-Key
    // nonce, SHA-1 hashed, then base64 encoded, to derive Sec-WebSocket-Accept.
    constexpr const char* WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    // Sanity bound on the *assembled* payload of one WS message (after
    // defragmenting Continuation frames), independent of WS_MAX_BUFLENGTH
    // (which only bounds what tout_read() ultimately copies out / accumulates
    // across the delimiter+token read modes). This exists purely to stop a
    // broken or hostile peer from growing payload vectors without bound; it
    // is intentionally generous (1 MiB) since legitimate JSON/text payloads
    // can comfortably exceed WS_MAX_BUFLENGTH.
    constexpr size_t WS_MAX_MESSAGE_LENGTH = 1u * 1024u * 1024u;

    // -----------------------------------------------------------------------
    // SHA-1 (RFC 3174) - self-contained, used only to derive Sec-WebSocket-Accept.
    // -----------------------------------------------------------------------
    void sha1(std::span<const uint8_t> data, std::array<uint8_t, 20>& digestOut)
    {
        uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE,
                 h3 = 0x10325476, h4 = 0xC3D2E1F0;

        std::vector<uint8_t> msg(data.begin(), data.end());
        const uint64_t ml = static_cast<uint64_t>(data.size()) * 8ULL;

        msg.push_back(0x80);
        while ((msg.size() % 64) != 56)
        {
            msg.push_back(0x00);
        }
        for (int i = 7; i >= 0; --i)
        {
            msg.push_back(static_cast<uint8_t>((ml >> (8 * i)) & 0xFF));
        }

        for (size_t chunkStart = 0; chunkStart < msg.size(); chunkStart += 64)
        {
            uint32_t w[80];
            for (int i = 0; i < 16; ++i)
            {
                w[i] = (static_cast<uint32_t>(msg[chunkStart + static_cast<size_t>(i) * 4]) << 24) |
                       (static_cast<uint32_t>(msg[chunkStart + static_cast<size_t>(i) * 4 + 1]) << 16) |
                       (static_cast<uint32_t>(msg[chunkStart + static_cast<size_t>(i) * 4 + 2]) << 8) |
                        static_cast<uint32_t>(msg[chunkStart + static_cast<size_t>(i) * 4 + 3]);
            }
            for (int i = 16; i < 80; ++i)
            {
                const uint32_t v = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
                w[i] = (v << 1) | (v >> 31);
            }

            uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
            for (int i = 0; i < 80; ++i)
            {
                uint32_t f, k;
                if (i < 20)      { f = (b & c) | ((~b) & d);        k = 0x5A827999; }
                else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
                else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
                else             { f = b ^ c ^ d;                   k = 0xCA62C1D6; }

                const uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
                e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = temp;
            }

            h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
        }

        const uint32_t hs[5] = {h0, h1, h2, h3, h4};
        for (int i = 0; i < 5; ++i)
        {
            digestOut[static_cast<size_t>(i) * 4]     = static_cast<uint8_t>((hs[i] >> 24) & 0xFF);
            digestOut[static_cast<size_t>(i) * 4 + 1] = static_cast<uint8_t>((hs[i] >> 16) & 0xFF);
            digestOut[static_cast<size_t>(i) * 4 + 2] = static_cast<uint8_t>((hs[i] >> 8) & 0xFF);
            digestOut[static_cast<size_t>(i) * 4 + 3] = static_cast<uint8_t>(hs[i] & 0xFF);
        }
    }

    // -----------------------------------------------------------------------
    // Small helpers: RNG, header parsing
    // -----------------------------------------------------------------------
    void fill_random(uint8_t* pBuffer, size_t szLen)
    {
        static thread_local std::mt19937 rng{std::random_device{}()};
        std::uniform_int_distribution<int> dist(0, 255);
        for (size_t i = 0; i < szLen; ++i)
        {
            pBuffer[i] = static_cast<uint8_t>(dist(rng));
        }
    }

    std::string generate_websocket_key()
    {
        std::vector<uint8_t> nonce(16);
        fill_random(nonce.data(), nonce.size());
        return commdump_base64_encode(nonce);
    }

    std::string compute_accept_key(const std::string& strClientKey)
    {
        const std::string strConcat = strClientKey + WS_GUID;
        std::array<uint8_t, 20> digest{};
        sha1(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(strConcat.data()), strConcat.size()), digest);
        return commdump_base64_encode(std::vector<uint8_t>(digest.begin(), digest.end()));
    }

    std::string to_lower(std::string_view sv)
    {
        std::string s(sv);
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    bool ci_contains(std::string_view haystack, std::string_view needle)
    {
        return to_lower(haystack).find(to_lower(needle)) != std::string::npos;
    }

    // Looks up a header (case-insensitive name) in an HTTP header block
    // (status line + "Name: value" lines separated by "\r\n", no trailing
    // blank line). Returns the trimmed value, or nullopt if absent.
    std::optional<std::string> find_header_value(const std::string& strHeaderBlock, std::string_view svName)
    {
        std::istringstream iss(strHeaderBlock);
        std::string strLine;
        bool bFirst = true;

        while (std::getline(iss, strLine))
        {
            if (!strLine.empty() && strLine.back() == '\r')
            {
                strLine.pop_back();
            }
            if (bFirst) { bFirst = false; continue; } // skip the HTTP status line

            const auto colonPos = strLine.find(':');
            if (colonPos == std::string::npos)
            {
                continue;
            }

            std::string_view svKey(strLine.data(), colonPos);
            if (to_lower(svKey) != to_lower(svName))
            {
                continue;
            }

            std::string_view svVal(strLine.data() + colonPos + 1, strLine.size() - colonPos - 1);
            while (!svVal.empty() && (svVal.front() == ' ' || svVal.front() == '\t')) { svVal.remove_prefix(1); }
            while (!svVal.empty() && (svVal.back()  == ' ' || svVal.back()  == '\t')) { svVal.remove_suffix(1); }
            return std::string(svVal);
        }
        return std::nullopt;
    }

} // namespace


// ============================================================================
// OPEN / CLOSE / STATE
// ============================================================================

bool WebSocket::is_open() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_bHandshakeOk && m_transport.is_open();
}


WebSocket::Status WebSocket::open(const std::string& strHost, uint16_t u16Port, const std::string& strPath,
                                   uint32_t u32ConnectTimeout, const std::string& strSubprotocol)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (strHost.empty() || u16Port == 0 || strPath.empty() || strPath.front() != '/')
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Invalid parameter: empty host/path, port 0, or path not starting with '/'"));
        return Status::INVALID_PARAM;
    }

    const uint32_t u32Timeout = (u32ConnectTimeout == 0) ? WS_CONNECT_DEFAULT_TIMEOUT : u32ConnectTimeout;

    // The connect and handshake stages each get the full configured budget
    // rather than splitting it - a slow-but-eventually-successful TCP
    // connect shouldn't starve the handshake read of time it never needed.
    Status eResult = m_transport.open(strHost, u16Port, u32Timeout);
    if (eResult != Status::SUCCESS)
    {
        return eResult;
    }

    eResult = ws_handshake(u32Timeout, strHost, u16Port, strPath, strSubprotocol);
    if (eResult != Status::SUCCESS)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Handshake failed for"); LOG_STRING(strHost.c_str());
                  LOG_STRING(":"); LOG_UINT32(u16Port); LOG_STRING(strPath.c_str()));
        m_transport.close();
        return eResult;
    }

    m_bHandshakeOk = true;

    LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("Connected to ws://"); LOG_STRING(strHost.c_str());
              LOG_STRING(":"); LOG_UINT32(u16Port); LOG_STRING(strPath.c_str()));

    return Status::SUCCESS;
}


WebSocket::Status WebSocket::close()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_bHandshakeOk && m_transport.is_open())
    {
        // Best effort - a peer that is already gone (or slow) must never
        // make close() itself block for long or fail loudly.
        const Status eCloseResult = ws_send_frame(500, 0x8, std::span<const uint8_t>());
        if (eCloseResult != Status::SUCCESS)
        {
            LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("Close frame not sent (peer likely already gone)"));
        }
    }

    m_bHandshakeOk = false;
    m_transport.close();

    {
        std::lock_guard<std::mutex> recvLock(m_recvMutex);
        m_recvLeftover.clear();
    }

    return Status::SUCCESS;
}


// ============================================================================
// HANDSHAKE
// ============================================================================

WebSocket::Status WebSocket::ws_handshake(uint32_t u32Timeout, const std::string& strHost, uint16_t u16Port,
                                           const std::string& strPath, const std::string& strSubprotocol) const
{
    const std::string strKey = generate_websocket_key();

    std::ostringstream oss;
    oss << "GET " << strPath << " HTTP/1.1\r\n"
        << "Host: " << strHost << ":" << u16Port << "\r\n"
        << "Upgrade: websocket\r\n"
        << "Connection: Upgrade\r\n"
        << "Sec-WebSocket-Key: " << strKey << "\r\n"
        << "Sec-WebSocket-Version: 13\r\n";
    if (!strSubprotocol.empty())
    {
        oss << "Sec-WebSocket-Protocol: " << strSubprotocol << "\r\n";
    }
    oss << "\r\n";

    const std::string strRequest = oss.str();
    const WriteResult wres = m_transport.tout_write(
        u32Timeout, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(strRequest.data()), strRequest.size()));

    if (wres.status != Status::SUCCESS || wres.bytes_written != strRequest.size())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to send handshake request"));
        return (wres.status == Status::SUCCESS) ? Status::WRITE_ERROR : wres.status;
    }

    // Read the HTTP response one chunk at a time until "\r\n\r\n" is seen,
    // bounded by an overall deadline (not a per-chunk one) so a peer that
    // trickles the response one byte at a time can't make the handshake
    // take longer than u32Timeout in aggregate.
    static constexpr size_t   HEADER_CHUNK_SIZE = 512;
    static constexpr size_t   HEADER_MAX_SIZE   = 8192;
    const auto tDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(u32Timeout);

    std::string strAccum;
    size_t szTerminatorPos = std::string::npos;

    while (szTerminatorPos == std::string::npos)
    {
        const auto tNow = std::chrono::steady_clock::now();
        if (tNow >= tDeadline)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Handshake response timed out"));
            return Status::READ_TIMEOUT;
        }
        const auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(tDeadline - tNow).count();

        std::array<uint8_t, HEADER_CHUNK_SIZE> chunk{};
        const ReadResult rres = m_transport.tout_read(static_cast<uint32_t>(remainingMs),
                                                       std::span<uint8_t>(chunk.data(), chunk.size()),
                                                       ReadOptions{ReadMode::Exact});
        if (rres.status != Status::SUCCESS)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed reading handshake response"));
            return rres.status;
        }
        if (rres.bytes_read == 0)
        {
            continue;
        }

        strAccum.append(reinterpret_cast<const char*>(chunk.data()), rres.bytes_read);

        if (strAccum.size() > HEADER_MAX_SIZE)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Handshake response headers too large"));
            return Status::BUFFER_OVERFLOW;
        }

        szTerminatorPos = strAccum.find("\r\n\r\n");
    }

    const std::string strHeaderBlock = strAccum.substr(0, szTerminatorPos);
    const std::string strLeftover    = strAccum.substr(szTerminatorPos + 4);

    // Status line must be "HTTP/1.x 101 ...".
    const auto szFirstSpace  = strHeaderBlock.find(' ');
    const auto szSecondSpace = (szFirstSpace == std::string::npos) ? std::string::npos
                                                                    : strHeaderBlock.find(' ', szFirstSpace + 1);
    const std::string strStatusCode = (szFirstSpace != std::string::npos && szSecondSpace != std::string::npos)
        ? strHeaderBlock.substr(szFirstSpace + 1, szSecondSpace - szFirstSpace - 1)
        : std::string();

    if (strStatusCode != "101")
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Server did not upgrade, status:");
                  LOG_STRING(strStatusCode.empty() ? "<malformed>" : strStatusCode.c_str()));
        return Status::PROTOCOL_ERROR;
    }

    const auto oUpgrade    = find_header_value(strHeaderBlock, "Upgrade");
    const auto oConnection = find_header_value(strHeaderBlock, "Connection");
    const auto oAccept     = find_header_value(strHeaderBlock, "Sec-WebSocket-Accept");

    if (!oUpgrade || !ci_contains(*oUpgrade, "websocket") ||
        !oConnection || !ci_contains(*oConnection, "upgrade"))
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing/invalid Upgrade or Connection header"));
        return Status::PROTOCOL_ERROR;
    }

    if (!oAccept || *oAccept != compute_accept_key(strKey))
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Sec-WebSocket-Accept mismatch"));
        return Status::PROTOCOL_ERROR;
    }

    if (!strLeftover.empty())
    {
        std::lock_guard<std::mutex> recvLock(m_recvMutex);
        m_recvLeftover.assign(strLeftover.begin(), strLeftover.end());
    }

    return Status::SUCCESS;
}


// ============================================================================
// INTERNAL TRANSPORT PRIMITIVES
// ============================================================================

WebSocket::Status WebSocket::recv_exact(uint32_t u32Timeout, uint8_t* pBuffer, size_t szLen) const
{
    if (pBuffer == nullptr && szLen > 0)
    {
        return Status::INVALID_PARAM;
    }

    size_t szCopied = 0;

    {
        std::lock_guard<std::mutex> recvLock(m_recvMutex);
        const size_t szTake = std::min(m_recvLeftover.size(), szLen);
        if (szTake > 0)
        {
            std::memcpy(pBuffer, m_recvLeftover.data(), szTake);
            m_recvLeftover.erase(m_recvLeftover.begin(), m_recvLeftover.begin() + static_cast<long>(szTake));
            szCopied = szTake;
        }
    }

    if (szCopied == szLen)
    {
        return Status::SUCCESS;
    }

    const auto tDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(u32Timeout);

    while (szCopied < szLen)
    {
        const auto tNow = std::chrono::steady_clock::now();
        if (tNow >= tDeadline)
        {
            return Status::READ_TIMEOUT;
        }
        const auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(tDeadline - tNow).count();

        const ReadResult rres = m_transport.tout_read(static_cast<uint32_t>(remainingMs),
                                                       std::span<uint8_t>(pBuffer + szCopied, szLen - szCopied),
                                                       ReadOptions{ReadMode::Exact});
        if (rres.status != Status::SUCCESS)
        {
            return rres.status;
        }
        szCopied += rres.bytes_read;
    }

    return Status::SUCCESS;
}


WebSocket::Status WebSocket::ws_send_frame(uint32_t u32Timeout, uint8_t u8Opcode, std::span<const uint8_t> payload) const
{
    std::vector<uint8_t> frame;
    frame.reserve(payload.size() + 14);

    frame.push_back(static_cast<uint8_t>(0x80 | (u8Opcode & 0x0F))); // FIN=1, RSV=0, opcode

    const size_t szLen = payload.size();
    static constexpr uint8_t MASK_BIT = 0x80; // client->server frames are always masked (RFC 6455 s.5.1)

    if (szLen <= 125)
    {
        frame.push_back(static_cast<uint8_t>(MASK_BIT | szLen));
    }
    else if (szLen <= 0xFFFF)
    {
        frame.push_back(static_cast<uint8_t>(MASK_BIT | 126));
        frame.push_back(static_cast<uint8_t>((szLen >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(szLen & 0xFF));
    }
    else
    {
        frame.push_back(static_cast<uint8_t>(MASK_BIT | 127));
        for (int i = 7; i >= 0; --i)
        {
            frame.push_back(static_cast<uint8_t>((static_cast<uint64_t>(szLen) >> (8 * i)) & 0xFF));
        }
    }

    uint8_t maskKey[4];
    fill_random(maskKey, sizeof(maskKey));
    frame.insert(frame.end(), maskKey, maskKey + sizeof(maskKey));

    const size_t szHeaderLen = frame.size();
    frame.insert(frame.end(), payload.begin(), payload.end());
    for (size_t i = 0; i < payload.size(); ++i)
    {
        frame[szHeaderLen + i] = static_cast<uint8_t>(frame[szHeaderLen + i] ^ maskKey[i % 4]);
    }

    const WriteResult wres = m_transport.tout_write(u32Timeout, std::span<const uint8_t>(frame.data(), frame.size()));
    if (wres.status != Status::SUCCESS)
    {
        return wres.status;
    }
    if (wres.bytes_written != frame.size())
    {
        return Status::WRITE_ERROR;
    }
    return Status::SUCCESS;
}


WebSocket::Status WebSocket::ws_recv_message(uint32_t u32Timeout, std::vector<uint8_t>& payload) const
{
    payload.clear();
    bool bFragmentInProgress = false;

    const auto tDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(u32Timeout);

    auto remainingMsOrTimeout = [&](uint32_t& outMs) -> bool
    {
        const auto tNow = std::chrono::steady_clock::now();
        if (tNow >= tDeadline)
        {
            return false;
        }
        outMs = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(tDeadline - tNow).count());
        return true;
    };

    while (true)
    {
        uint32_t remainingMs = 0;
        if (!remainingMsOrTimeout(remainingMs))
        {
            return Status::READ_TIMEOUT;
        }

        uint8_t hdr[2];
        Status eStatus = recv_exact(remainingMs, hdr, sizeof(hdr));
        if (eStatus != Status::SUCCESS)
        {
            return eStatus;
        }

        const bool    bFin    = (hdr[0] & 0x80) != 0;
        const uint8_t u8Opcode = hdr[0] & 0x0F;
        const bool    bMasked = (hdr[1] & 0x80) != 0;
        uint64_t      u64Len  = hdr[1] & 0x7F;

        if (u64Len == 126)
        {
            if (!remainingMsOrTimeout(remainingMs)) { return Status::READ_TIMEOUT; }
            uint8_t ext[2];
            eStatus = recv_exact(remainingMs, ext, sizeof(ext));
            if (eStatus != Status::SUCCESS) { return eStatus; }
            u64Len = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
        }
        else if (u64Len == 127)
        {
            if (!remainingMsOrTimeout(remainingMs)) { return Status::READ_TIMEOUT; }
            uint8_t ext[8];
            eStatus = recv_exact(remainingMs, ext, sizeof(ext));
            if (eStatus != Status::SUCCESS) { return eStatus; }
            u64Len = 0;
            for (int i = 0; i < 8; ++i) { u64Len = (u64Len << 8) | ext[i]; }
        }

        if (u64Len > WS_MAX_MESSAGE_LENGTH)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Frame payload exceeds sanity limit"));
            return Status::BUFFER_OVERFLOW;
        }

        uint8_t maskKey[4] = {0, 0, 0, 0};
        if (bMasked)
        {
            // Not expected from a spec-compliant server (RFC 6455 s.5.1), but
            // some permissive test/loopback servers mask anyway - unmask
            // rather than reject, for interoperability.
            if (!remainingMsOrTimeout(remainingMs)) { return Status::READ_TIMEOUT; }
            eStatus = recv_exact(remainingMs, maskKey, sizeof(maskKey));
            if (eStatus != Status::SUCCESS) { return eStatus; }
        }

        std::vector<uint8_t> framePayload(static_cast<size_t>(u64Len));
        if (u64Len > 0)
        {
            if (!remainingMsOrTimeout(remainingMs)) { return Status::READ_TIMEOUT; }
            eStatus = recv_exact(remainingMs, framePayload.data(), framePayload.size());
            if (eStatus != Status::SUCCESS) { return eStatus; }
            if (bMasked)
            {
                for (size_t i = 0; i < framePayload.size(); ++i)
                {
                    framePayload[i] = static_cast<uint8_t>(framePayload[i] ^ maskKey[i % 4]);
                }
            }
        }

        switch (u8Opcode)
        {
            case 0x0: // Continuation
                if (!bFragmentInProgress)
                {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Continuation frame with no message in progress"));
                    return Status::PROTOCOL_ERROR;
                }
                payload.insert(payload.end(), framePayload.begin(), framePayload.end());
                if (payload.size() > WS_MAX_MESSAGE_LENGTH)
                {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Assembled message exceeds sanity limit"));
                    return Status::BUFFER_OVERFLOW;
                }
                if (bFin) { return Status::SUCCESS; }
                break;

            case 0x1: // Text
            case 0x2: // Binary
                if (bFragmentInProgress)
                {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("New data frame before previous fragmented message finished"));
                    return Status::PROTOCOL_ERROR;
                }
                payload.assign(framePayload.begin(), framePayload.end());
                if (bFin) { return Status::SUCCESS; }
                bFragmentInProgress = true;
                break;

            case 0x8: // Close
                LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("Peer sent Close"));
                if (remainingMsOrTimeout(remainingMs))
                {
                    // Best effort echo; ignore result - we're tearing down either way.
                    ws_send_frame(std::min<uint32_t>(remainingMs, 500), 0x8,
                                  std::span<const uint8_t>(framePayload.data(), framePayload.size()));
                }
                return Status::READ_ERROR;

            case 0x9: // Ping -> answer with Pong carrying the same payload, keep waiting
                if (remainingMsOrTimeout(remainingMs))
                {
                    const Status pongStatus = ws_send_frame(remainingMs, 0xA,
                        std::span<const uint8_t>(framePayload.data(), framePayload.size()));
                    if (pongStatus != Status::SUCCESS)
                    {
                        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Failed to answer Ping with Pong"));
                    }
                }
                continue;

            case 0xA: // Pong - nothing to do, keep waiting for the real message
                continue;

            default:
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown/reserved opcode:"); LOG_UINT8(u8Opcode));
                return Status::PROTOCOL_ERROR;
        }
    }
}


// ============================================================================
// CHUNK-SOURCE PRIMITIVE (mirrors TCPIP::timeout_read(), message granularity)
// ============================================================================

WebSocket::Status WebSocket::timeout_read(uint32_t u32ReadTimeout, std::span<uint8_t> buffer, size_t& szBytesRead) const
{
    if (buffer.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("timeout_read: invalid parameter"));
        return Status::INVALID_PARAM;
    }

    szBytesRead = 0;

    std::vector<uint8_t> payload;
    const Status eStatus = ws_recv_message(u32ReadTimeout, payload);
    if (eStatus != Status::SUCCESS)
    {
        return eStatus;
    }

    const size_t szToCopy = std::min(payload.size(), buffer.size());
    if (szToCopy < payload.size())
    {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Message truncated, buffer too small, dropped bytes:");
                  LOG_UINT32(static_cast<uint32_t>(payload.size() - szToCopy)));
    }
    if (szToCopy > 0)
    {
        std::memcpy(buffer.data(), payload.data(), szToCopy);
    }
    szBytesRead = szToCopy;

    LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("RX bytes:"); LOG_UINT32(static_cast<uint32_t>(szBytesRead)));

    return Status::SUCCESS;
}


// ============================================================================
// PUBLIC UNIFIED INTERFACE IMPLEMENTATION
// ============================================================================

WebSocket::ReadResult WebSocket::tout_read(uint32_t u32ReadTimeout,
                           std::span<uint8_t> buffer,
                           const ReadOptions& options,
                           std::string_view xtra_params) const
{
    ReadResult result;

    if (!xtra_params.empty())
    {
        // Single-peer WebSocket client: no per-call destination, so xtra_params
        // is accepted only to satisfy ICommDriver's shared surface and
        // otherwise ignored here, same convention as TCPIP.
        LOG_PRINT(LOG_WARNING, LOG_HDR;
                  LOG_STRING("tout_read: xtra_params is not used by this driver, ignored"));
    }

    const uint32_t u32Timeout = (u32ReadTimeout == 0) ? WS_READ_DEFAULT_TIMEOUT : u32ReadTimeout;

    switch (options.mode)
    {
        case ReadMode::Exact:
        {
            size_t bytes_read = 0;
            result.status           = timeout_read(u32Timeout, buffer, bytes_read);
            result.bytes_read       = bytes_read;
            result.found_terminator = false;
            break;
        }

        case ReadMode::UntilDelimiter:
        {
            size_t bytes_read = 0;
            result.status           = timeout_read_until(u32Timeout, buffer, options.delimiter, bytes_read);
            result.bytes_read       = bytes_read;
            result.found_terminator = (result.status == Status::SUCCESS);
            break;
        }

        case ReadMode::UntilToken:
        {
            result.status           = timeout_wait_for_token(u32Timeout, options.token, options.use_buffer);
            result.bytes_read       = 0; // Token search does not fill the caller's buffer
            result.found_terminator = (result.status == Status::SUCCESS);
            break;
        }

        default:
            result.status           = Status::INVALID_PARAM;
            result.bytes_read       = 0;
            result.found_terminator = false;
            break;
    }

    return result;
}


WebSocket::WriteResult WebSocket::tout_write(uint32_t u32WriteTimeout,
                             std::span<const uint8_t> buffer,
                             std::string_view xtra_params) const
{
    WriteResult result;

    // xtra_params == "text" sends a Text frame (opcode 0x1); anything else
    // (including empty, the default) sends a Binary frame (opcode 0x2).
    const uint8_t u8Opcode = (xtra_params == "text") ? 0x1 : 0x2;

    const uint32_t u32Timeout = (u32WriteTimeout == 0) ? WS_WRITE_DEFAULT_TIMEOUT : u32WriteTimeout;

    result.status        = ws_send_frame(u32Timeout, u8Opcode, buffer);
    result.bytes_written = (result.status == Status::SUCCESS) ? buffer.size() : 0;

    return result;
}


// ============================================================================
// UntilDelimiter / UntilToken (identical structure to TCPIP's, message-chunked)
// ============================================================================

WebSocket::Status WebSocket::timeout_read_until(uint32_t u32ReadTimeout,
                                std::span<uint8_t> buffer,
                                uint8_t cDelimiter,
                                size_t& szBytesRead) const
{
    if (buffer.size() < 2)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Buffer too small for delimiter + null terminator"));
        return Status::INVALID_PARAM;
    }

    szBytesRead = 0;
    Status eResult = Status::RETVAL_NOT_SET;

    std::array<uint8_t, WS_MAX_BUFLENGTH> chunk = {};

    while (eResult == Status::RETVAL_NOT_SET)
    {
        const size_t bytesRemaining = buffer.size() - szBytesRead - 1; // reserve for '\0'
        if (bytesRemaining == 0)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Buffer full before delimiter found"));
            return Status::BUFFER_OVERFLOW;
        }

        size_t chunkBytes = 0;
        const Status readResult = timeout_read(u32ReadTimeout, std::span<uint8_t>(chunk.data(), chunk.size()), chunkBytes);

        if (readResult == Status::SUCCESS && chunkBytes > 0)
        {
            // As with TCPIP: any bytes received after the delimiter within
            // this same chunk (here: within this same WS message) are
            // discarded when we return early below.
            for (size_t i = 0; i < chunkBytes && szBytesRead < buffer.size() - 1; ++i)
            {
                const uint8_t ch = chunk[i];

                if (ch == cDelimiter)
                {
                    buffer[szBytesRead] = '\0';
                    return Status::SUCCESS;
                }
                buffer[szBytesRead++] = ch;
            }
        }
        else if (readResult == Status::READ_TIMEOUT)
        {
            eResult = (u32ReadTimeout > 0) ? Status::READ_TIMEOUT : Status::PORT_ACCESS;
        }
        else
        {
            eResult = Status::PORT_ACCESS;
        }
    }

    return eResult;
}


WebSocket::Status WebSocket::timeout_wait_for_token(uint32_t u32ReadTimeout,
                                    std::span<const uint8_t> token,
                                    bool useBuffer) const
{
    const size_t szTokenLength = token.size();
    if (token.empty() || szTokenLength == 0 || szTokenLength >= WS_MAX_BUFLENGTH)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid token or length"));
        return Status::INVALID_PARAM;
    }

    std::vector<int> viLps;
    build_kmp_table(token, szTokenLength, viLps);

    return kmp_stream_match(token, viLps, u32ReadTimeout, /*bReturnOnTimeout=*/true, useBuffer);
}


void WebSocket::build_kmp_table(std::span<const uint8_t> pattern, size_t szLength, std::vector<int>& viLps) const
{
    ukmp::build_kmp_table(pattern, szLength, viLps);
}


WebSocket::Status WebSocket::kmp_stream_match(std::span<const uint8_t> token,
                              const std::vector<int>& viLps,
                              uint32_t u32Timeout,
                              bool bReturnOnTimeout,
                              bool useBuffer) const
{
    return ukmp::kmp_stream_match(
        [this](uint32_t timeout, std::span<uint8_t> buf, size_t& bytesRead) { return timeout_read(timeout, buf, bytesRead); },
        token, viLps, u32Timeout, bReturnOnTimeout, useBuffer,
        /*szChunkBufferSize=*/WS_MAX_BUFLENGTH, /*szRingBufferSize=*/WS_MAX_BUFLENGTH);
}
