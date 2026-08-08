#ifndef U_WEBSOCKET_DRIVER_H
#define U_WEBSOCKET_DRIVER_H

#include "ICommDriver.hpp"
#include "uTcpip.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <mutex>
#include <cstdint>


/**
 * @brief WebSocket (RFC 6455) client driver implementing the ICommDriver interface.
 *
 * Layered directly on top of the TCPIP driver the same way the codebase's
 * other "protocol on top of a byte stream" drivers are (see MqttDriver in
 * the mqtt plugin) - it owns a TCPIP instance as its transport, performs the
 * HTTP/1.1 Upgrade handshake over it, and then frames/unframes WebSocket
 * messages over the same connection. Because everything above the raw
 * socket is plain C++ (no direct syscalls), this driver has no POSIX/Win32
 * split at all - it builds and runs anywhere uTcpip does.
 *
 * Like TCPIP, this is a byte-stream-shaped driver (ICommDriver's read modes
 * map onto it), but the underlying transport is message oriented, so the
 * "chunk" unit tout_read() works with is one complete (defragmented) WS
 * message rather than one recv(2):
 *
 *   tout_write()
 *     Sends buffer as a single WebSocket message (fragmentation is never
 *     used on the way out - the whole buffer becomes one frame's payload).
 *     xtra_params selects the opcode: "text" sends a Text frame (0x1);
 *     empty or any other value sends a Binary frame (0x2, the default).
 *     Per RFC 6455 s.5.1, client->server frames are always masked with a
 *     fresh random 32-bit key generated per frame.
 *
 *   tout_read() - ReadMode::Exact
 *     Waits for and defragments one complete WS data message (Continuation
 *     frames are merged transparently), then copies up to buffer.size()
 *     bytes of its payload into buffer. Ping frames received while waiting
 *     are answered with a Pong automatically and do not count as the
 *     message being waited for; a Close frame received from the peer closes
 *     the connection and is reported as Status::READ_ERROR, the same status
 *     TCPIP::timeout_read() uses for a peer-initiated TCP shutdown.
 *
 *   tout_read() - ReadMode::UntilDelimiter / ReadMode::UntilToken
 *     Same semantics as TCPIP: bytes are accumulated - here, one complete WS
 *     message's payload at a time, standing in for TCPIP's one recv(2)
 *     worth of bytes - until the delimiter/token is found or the buffer
 *     is full. See uTcpip.hpp's class doc comment for the same caveat about
 *     back-to-back delimited messages landing in one chunk.
 *
 * xtra_params (read side):
 *   Single-peer client, no per-call destination - accepted only to satisfy
 *   ICommDriver and otherwise ignored (logged at WARNING if non-empty), the
 *   same convention TCPIP uses.
 *
 * Handshake / connection lifetime:
 *   open() performs the full sequence: TCP connect (via the internal TCPIP
 *   instance) -> send the HTTP Upgrade request -> read and validate the
 *   HTTP 101 response, including checking Sec-WebSocket-Accept against the
 *   RFC 6455 s.4.2.2 formula. Any bytes read past the end of the HTTP
 *   headers (a server that pipelines its first WS frame immediately after
 *   the handshake) are kept and consumed first by the next frame read.
 *   close() sends a Close frame (best effort - failures are logged, not
 *   propagated) and then closes the TCP connection.
 *
 * Thread safety:
 *   Mirrors TCPIP: an internal mutex protects open()/close()/is_open(), and
 *   read/write are not required to serialize against each other beyond what
 *   the underlying TCPIP transport already provides.
 */
class WebSocket : public ICommDriver
{
    public:

        static constexpr size_t   WS_MAX_BUFLENGTH          = 4096;  /**< Max assembled message length (delimiter/token modes and Exact truncation). */
        static constexpr uint32_t WS_READ_DEFAULT_TIMEOUT    = 5000; /**< Default read timeout in milliseconds.                                      */
        static constexpr uint32_t WS_WRITE_DEFAULT_TIMEOUT   = 5000; /**< Default write timeout in milliseconds.                                     */
        static constexpr uint32_t WS_CONNECT_DEFAULT_TIMEOUT = 5000; /**< Default connect timeout in milliseconds (covers TCP connect + HTTP handshake, split evenly). */

        WebSocket() = default;

        /**
         * @brief Construct and immediately connect + handshake against
         *        ws://strHost:u16Port/strPath.
         * @param strHost           Hostname or IP address.
         * @param u16Port           TCP port number.
         * @param strPath           HTTP request-target, e.g. "/" or "/ws" (must start with '/').
         * @param u32ConnectTimeout Overall connect+handshake timeout in milliseconds (0 = use default).
         * @param strIdentityLabel  Display text for the GUI comm-dump panel (see describeConnection()).
         * @param strSubprotocol    Optional value for the Sec-WebSocket-Protocol request header; empty omits it.
         */
        explicit WebSocket(const std::string& strHost, uint16_t u16Port, const std::string& strPath = "/",
                           uint32_t u32ConnectTimeout = 0, const std::string& strIdentityLabel = {},
                           const std::string& strSubprotocol = {})
            : m_strIdentityLabel(strIdentityLabel)
        {
            open(strHost, u16Port, strPath, u32ConnectTimeout, strSubprotocol);
        }

        virtual ~WebSocket()
        {
            close();
        }

        /**
         * @brief Connect to strHost:u16Port and perform the WebSocket opening handshake.
         * @return Status::SUCCESS or an error code (Status::PROTOCOL_ERROR for a
         *         malformed/refused handshake, otherwise the same codes TCPIP::open() returns).
         */
        Status open(const std::string& strHost, uint16_t u16Port, const std::string& strPath = "/",
                    uint32_t u32ConnectTimeout = 0, const std::string& strSubprotocol = {});

        /**
         * @brief Send a Close frame (best effort) and close the underlying TCP connection.
         * @return Status::SUCCESS.
         */
        Status close();

        /**
         * @brief Check whether the connection is open and the handshake completed.
         */
        bool is_open() const override;

        /**
         * @brief Describe this connection for the GUI comm-dump panel.
         * Single-peer WebSocket client - xtra_params is ignored, same as TCPIP.
         */
        CommDetails describeConnection(std::string_view /*xtra_params*/ = {}) const override
        {
            return commdump_details(CommFamily::NET, m_strIdentityLabel);
        }

        /**
         * @brief Unified read interface supporting multiple operation modes.
         * @see class doc comment above for exact per-mode semantics.
         */
        ReadResult tout_read(uint32_t u32ReadTimeout,
                             std::span<uint8_t> buffer,
                             const ReadOptions& options,
                             std::string_view xtra_params = {}) const override;

        /**
         * @brief Unified write interface. xtra_params == "text" sends a Text frame,
         *        otherwise (including empty) a Binary frame is sent.
         */
        WriteResult tout_write(uint32_t u32WriteTimeout,
                               std::span<const uint8_t> buffer,
                               std::string_view xtra_params = {}) const override;

    private:

        mutable TCPIP       m_transport;         /**< Underlying TCP client connection.                    */
        mutable std::mutex  m_mutex;              /**< Protects open()/close()/is_open().                   */
        mutable std::mutex  m_recvMutex;          /**< Protects m_recvLeftover, separately from m_mutex so a blocking read never holds the open/close lock. */
        mutable std::vector<uint8_t> m_recvLeftover; /**< Bytes read past the handshake response, or past a previously parsed frame, not yet consumed. */
        mutable bool        m_bHandshakeOk = false;  /**< True once the opening handshake completed successfully. */
        std::string         m_strIdentityLabel;      /**< GUI comm-dump display label, see describeConnection(). */

        // -----------------------------------------------------------------------
        // Handshake
        // -----------------------------------------------------------------------

        /** @brief Perform the HTTP/1.1 Upgrade handshake over an already-connected m_transport. */
        Status ws_handshake(uint32_t u32Timeout, const std::string& strHost, uint16_t u16Port,
                            const std::string& strPath, const std::string& strSubprotocol) const;

        // -----------------------------------------------------------------------
        // Internal transport primitives
        // -----------------------------------------------------------------------

        /**
         * @brief Read exactly szLen bytes (draining m_recvLeftover first, then the
         * socket), bounded by an overall deadline derived from u32Timeout.
         */
        Status recv_exact(uint32_t u32Timeout, uint8_t* pBuffer, size_t szLen) const;

        /** @brief Send the whole buffer as one masked WebSocket frame of the given opcode. */
        Status ws_send_frame(uint32_t u32Timeout, uint8_t u8Opcode, std::span<const uint8_t> payload) const;

        /**
         * @brief Wait for and defragment one complete WS data message (Text/Binary),
         * transparently answering Ping with Pong and consuming Pong frames.
         * A Close frame from the peer closes the connection and returns Status::READ_ERROR.
         */
        Status ws_recv_message(uint32_t u32Timeout, std::vector<uint8_t>& payload) const;

        /**
         * @brief Receive one complete WS message (see ws_recv_message()) and copy up to
         * buffer.size() bytes of its payload into buffer. Mirrors TCPIP::timeout_read()'s
         * role as the chunk-source primitive for the Exact / UntilDelimiter / UntilToken modes.
         */
        Status timeout_read(uint32_t u32ReadTimeout, std::span<uint8_t> buffer, size_t& szBytesRead) const;

        /**
         * @brief Accumulate WS-message payload bytes (one message per chunk) until
         * cDelimiter is found or the buffer is full. Null-terminates on Status::SUCCESS.
         */
        Status timeout_read_until(uint32_t u32ReadTimeout, std::span<uint8_t> buffer,
                                  uint8_t cDelimiter, size_t& szBytesRead) const;

        /** @brief Stream WS-message payload bytes, applying the KMP algorithm to detect the token sequence. */
        Status timeout_wait_for_token(uint32_t u32ReadTimeout, std::span<const uint8_t> token, bool useBuffer) const;

        // -----------------------------------------------------------------------
        // KMP helpers (identical strategy to the UART / I2C / SPI / CAN / TCPIP / UDP drivers)
        // -----------------------------------------------------------------------

        /** @brief Run KMP stream matching over WS-message payload bytes. */
        Status kmp_stream_match(std::span<const uint8_t> token,
                                const std::vector<int>& viLps,
                                uint32_t u32Timeout,
                                bool bReturnOnTimeout,
                                bool useBuffer) const;

        /** @brief Build the KMP failure-function table for @p pattern. */
        void build_kmp_table(std::span<const uint8_t> pattern,
                             size_t szLength,
                             std::vector<int>& viLps) const;
};


#endif // U_WEBSOCKET_DRIVER_H
