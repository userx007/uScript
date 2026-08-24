#ifndef U_UDP_DRIVER_H
#define U_UDP_DRIVER_H

#include "ICommDriver.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <mutex>
#include <cstdint>


/**
 * @brief UDP client driver implementing the ICommDriver interface.
 *
 * connect()s a UDP socket to a default peer host:port and exposes the same
 * ICommDriver read/write surface used by the UART, I2C, SPI, CAN and TCP
 * (ETH) drivers. UDP is datagram-based and message-preserving, like CAN and
 * unlike the byte-stream TCP driver, so the mapping mirrors uKVCan's more
 * closely than uEth's:
 *
 *   tout_write()
 *     Sends buffer as the payload of a single UDP datagram. buffer.size()
 *     should stay at or below the path MTU (see UDP_SAFE_PAYLOAD below) to
 *     avoid IP fragmentation; the hard ceiling enforced here is
 *     UDP_MAX_DGRAM_LEN. By default the datagram goes to the peer passed to
 *     open(); see "xtra_params" below for a per-call override.
 *
 *   tout_read() — ReadMode::Exact
 *     Receives one datagram and copies its payload into buffer. If
 *     buffer.size() is smaller than the datagram, per normal UDP semantics
 *     the excess is truncated and silently discarded by the kernel (not
 *     just by this driver) — the return still reports bytes_read = the
 *     truncated amount, since this driver has no way to learn the original
 *     size. If you need to detect truncation, size buffer to
 *     UDP_MAX_DGRAM_LEN or a value you know your peer respects.
 *
 *   tout_read() — ReadMode::UntilDelimiter
 *     Receives datagrams one at a time, appending each payload to buffer
 *     until the delimiter byte is found within a payload or the buffer is
 *     full. As with the CAN driver, bytes received after the delimiter
 *     within the same datagram are discarded when the call returns.
 *
 *   tout_read() — ReadMode::UntilToken
 *     Streams payload bytes across consecutive datagrams, applying the KMP
 *     algorithm to detect the token sequence. bytes_read = 0 on return.
 *
 * xtra_params (write path only):
 *   Because the socket is connect()ed to a default peer, the kernel already
 *   filters incoming datagrams to that peer, so there is nothing useful for
 *   xtra_params to override on the read path — it is accepted (to satisfy
 *   ICommDriver) but ignored by tout_read(), and a non-empty value there is
 *   logged at WARNING level.
 *
 *   On the write path, a non-empty xtra_params is parsed as "host:port"
 *   (IPv6 literals must be bracketed, e.g. "[::1]:5000") and the datagram is
 *   sent there via sendto() instead of the connected default, for that
 *   single call only. To keep tout_write() non-blocking/allocation-light,
 *   this override accepts numeric IP literals only (no DNS lookups on the
 *   hot path) — resolution failure returns Status::INVALID_PARAM. An empty
 *   string (default) sends to the peer set by open().
 *
 * Timeout handling:
 *   poll(2) on the socket fd before every recv()/send(). u32*Timeout = 0
 *   selects the respective DEFAULT_TIMEOUT constant. Unlike TCP, a UDP
 *   send() either accepts the whole datagram or fails outright — there is
 *   no short-write/retry loop, so the write timeout only bounds the poll()
 *   wait for the socket send buffer to have room.
 *
 * Connection semantics:
 *   open() calls connect() on the UDP socket. This does not perform a
 *   handshake (there is none in UDP) — it only records a default peer
 *   address in the kernel so plain send()/recv() can be used and so the
 *   kernel will deliver asynchronous ICMP errors (e.g. port unreachable)
 *   back to this socket. Because there is no handshake, connection setup is
 *   effectively instantaneous; the connectTimeout parameter is accepted for
 *   API symmetry with the other drivers but is not expected to matter in
 *   practice.
 *
 * Thread safety:
 *   All public methods are protected by an internal mutex.
 */
class UDP : public ICommDriver
{
    public:

        static constexpr size_t   UDP_MAX_DGRAM_LEN         = 65507; /**< Max UDP payload (IPv4 theoretical ceiling: 65535 - 8-byte UDP hdr - 20-byte IP hdr). */
        static constexpr size_t   UDP_SAFE_PAYLOAD           = 1472;  /**< Payload that fits a standard 1500-byte-MTU Ethernet link without IP fragmentation. Advisory only — not enforced. */
        static constexpr uint32_t UDP_READ_DEFAULT_TIMEOUT    = 5000; /**< Default read timeout in milliseconds.    */
        static constexpr uint32_t UDP_WRITE_DEFAULT_TIMEOUT   = 5000; /**< Default write timeout in milliseconds.   */
        static constexpr uint32_t UDP_CONNECT_DEFAULT_TIMEOUT = 5000; /**< Accepted for API symmetry; see note above. */

        UDP() = default;

        /**
         * @brief Construct and immediately connect() to strHost:u16Port.
         * @param strHost           Hostname or IP address, e.g. "192.168.1.10" or "myhost.local".
         * @param u16Port           UDP port number.
         * @param u32ConnectTimeout Accepted for API symmetry with the other drivers; see class docs.
         * @param strIdentityLabel  Display text for the GUI comm-dump panel (see
         *                          describeConnection()), supplied separately from
         *                          strHost/u16Port by the caller — e.g. "192.168.1.10:5000".
         */
        explicit UDP(const std::string& strHost, uint16_t u16Port, uint32_t u32ConnectTimeout = 0,
                    const std::string& strIdentityLabel = {})
            : m_strIdentityLabel(strIdentityLabel)
        {
            open(strHost, u16Port, u32ConnectTimeout);
        }

        virtual ~UDP()
        {
            close();
        }

        /**
         * @brief Resolve strHost, open a UDP socket, and connect() it to the
         * resolved address so it becomes this driver's default peer.
         *
         * @param strHost            Hostname or IP address.
         * @param u16Port            UDP port number.
         * @param u32ConnectTimeout  Accepted for API symmetry; see class docs.
         * @return Status::SUCCESS or an error code.
         */
        Status open(const std::string& strHost, uint16_t u16Port, uint32_t u32ConnectTimeout = 0);

        /**
         * @brief Close the socket.
         * @return Status::SUCCESS.
         */
        Status close();

        /**
         * @brief Check whether the socket is open.
         * @return true if the socket fd is valid.
         */
        bool is_open() const override;

        /**
         * @brief Describe this connection for the GUI comm-dump panel.
         *
         * xtra_params empty: returns the default-peer label set at construction.
         * xtra_params non-empty: this exchange is going to a per-call destination
         * override (same "host:port" string tout_write() itself parses — see class
         * docs), so the label reflects THAT destination instead, since that is what
         * is actually happening on the wire for this specific call.
         */
        CommDetails describeConnection(std::string_view xtra_params = {}) const override
        {
            return commdump_details(CommFamily::NET,
                                     xtra_params.empty() ? m_strIdentityLabel : xtra_params);
        }

        /**
         * @brief Unified read interface supporting multiple operation modes.
         *
         * @param u32ReadTimeout  Timeout in milliseconds (0 = block indefinitely / infinite timeout).
         * @param buffer          Buffer to receive payload data into.
         * @param options         Read operation configuration.
         * @param xtra_params     Unused on the read path; see class docs. A
         *                        non-empty value is logged and ignored.
         * @return ReadResult containing status, bytes read, and terminator found flag.
         *
         * @details
         * - ReadMode::Exact:          Receive one datagram; copy payload to buffer.
         * - ReadMode::UntilDelimiter: Accumulate payloads across datagrams until delimiter found.
         * - ReadMode::UntilToken:     KMP search across datagram payloads; bytes_read = 0.
         */
        ReadResult tout_read(uint32_t u32ReadTimeout,
                             std::span<uint8_t> buffer,
                             const ReadOptions& options,
                             std::string_view xtra_params = {}) const override;

        /**
         * @brief Unified write interface.
         *
         * Sends buffer as the payload of a single UDP datagram.
         * buffer.size() must be ≤ UDP_MAX_DGRAM_LEN.
         *
         * @param u32WriteTimeout  Timeout in milliseconds (0 = block indefinitely / infinite timeout).
         * @param buffer           Payload to transmit.
         * @param xtra_params      Optional "host:port" destination override for
         *                        this single datagram (numeric literals only —
         *                        see class docs). Empty uses the peer set by open().
         * @return WriteResult containing status and bytes written.
         */
        WriteResult tout_write(uint32_t u32WriteTimeout,
                               std::span<const uint8_t> buffer,
                               std::string_view xtra_params = {}) const override;

    private:

        int                 m_iHandle = -1;      /**< Socket file descriptor.     */
        mutable std::mutex  m_mutex;             /**< Protects concurrent access. */
        std::string         m_strIdentityLabel;  /**< GUI comm-dump display label, see describeConnection(). */

        // -----------------------------------------------------------------------
        // Internal transport primitives
        // -----------------------------------------------------------------------

        /**
         * @brief Receive one UDP datagram; copy its payload into buffer.
         * Uses poll(2) for the timeout, then a single recv(2) call.
         * bytes_read is set to the (possibly truncated) payload length.
         */
        Status timeout_read(uint32_t u32ReadTimeout,
                            std::span<uint8_t> buffer,
                            size_t& szBytesRead) const;

        /**
         * @brief Accumulate UDP datagram payloads until cDelimiter is found
         * or buffer is full. Null-terminates on Status::SUCCESS.
         */
        Status timeout_read_until(uint32_t u32ReadTimeout,
                                  std::span<uint8_t> buffer,
                                  uint8_t cDelimiter,
                                  size_t& szBytesRead) const;

        /**
         * @brief Stream payload bytes across consecutive UDP datagrams,
         * applying the KMP algorithm to detect the token sequence.
         */
        Status timeout_wait_for_token(uint32_t u32ReadTimeout,
                                      std::span<const uint8_t> token,
                                      bool useBuffer) const;

        /**
         * @brief Send buffer as a single UDP datagram, either to the
         * connected default peer (send()) or, when pDestAddr is non-null,
         * to that address via sendto().
         */
        Status timeout_write(uint32_t u32WriteTimeout,
                             std::span<const uint8_t> buffer,
                             size_t& szBytesWritten,
                             const void* pDestAddr,
                             size_t szDestAddrLen) const;

        // -----------------------------------------------------------------------
        // KMP helpers (identical strategy to the UART / I2C / SPI / CAN / ETH drivers)
        // -----------------------------------------------------------------------

        /** @brief Run KMP stream matching over UDP datagram payload bytes. */
        Status kmp_stream_match(std::span<const uint8_t> token,
                                const std::vector<int>& viLps,
                                uint32_t u32Timeout,
                                bool bReturnOnTimeout,
                                bool useBuffer) const;

        /** @brief Build the KMP failure-function table for @p pattern. */
        void build_kmp_table(std::span<const uint8_t> pattern,
                             size_t szLength,
                             std::vector<int>& viLps) const;

        // -----------------------------------------------------------------------
        // Address parsing (implemented in the platform file — keeps
        // sockaddr_storage and friends out of this portable header)
        // -----------------------------------------------------------------------

        /**
         * @brief Parse "host:port" (or "[ipv6]:port") from xtra_params into a
         * raw sockaddr_* image, numeric literals only (no DNS lookups).
         * @param xtra_params  Destination string to parse.
         * @param vAddrStorage Filled with the raw sockaddr bytes on success.
         * @return true on success, false if xtra_params is malformed or not numeric.
         */
        bool resolve_numeric_host_port(std::string_view xtra_params,
                                       std::vector<uint8_t>& vAddrStorage) const;
};


#endif // U_UDP_DRIVER_H
