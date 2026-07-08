#ifndef U_TCPIP_DRIVER_H
#define U_TCPIP_DRIVER_H

#include "ICommDriver.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <mutex>
#include <cstdint>


/**
 * @brief TCP client driver implementing the ICommDriver interface.
 *
 * Connects to a remote host:port over TCP and exposes the same ICommDriver
 * read/write surface used by the UART, I2C, SPI and CAN drivers. Unlike CAN,
 * TCP is a byte stream rather than frame based, so the ICommDriver read
 * modes map onto it directly with no packing/unpacking step:
 *
 *   tout_write()
 *     Sends buffer over the connected socket. A single send(2) call is not
 *     guaranteed to accept the whole buffer, so the implementation loops,
 *     reissuing send() for the remainder, until either the whole buffer has
 *     gone out or the overall write timeout elapses.
 *
 *   tout_read() — ReadMode::Exact
 *     Issues one poll(2) + recv(2) pair and copies whatever arrived (up to
 *     buffer.size()) into buffer. Mirrors the "single syscall per call"
 *     behaviour of the sibling drivers rather than blocking until the
 *     buffer is completely full.
 *
 *   tout_read() — ReadMode::UntilDelimiter
 *     Receives bytes in chunks, appending them to buffer, until the
 *     delimiter byte is found in a chunk or the buffer is full. NOTE: any
 *     bytes received after the delimiter within the same chunk are
 *     discarded — the same trade-off the CAN driver makes with the tail of
 *     a frame's payload. Because TCP chunk boundaries rarely line up with
 *     message boundaries, this is more likely to bite here than on CAN; if
 *     the peer can pipeline multiple delimited messages back-to-back,
 *     prefer ReadMode::UntilToken or read exactly one message at a time.
 *
 *   tout_read() — ReadMode::UntilToken
 *     Streams bytes off the socket, applying the KMP algorithm to detect
 *     the token sequence. bytes_read = 0 on return.
 *
 * xtra_params:
 *   This driver is a single-peer TCP client — there is no per-call
 *   destination to select the way a CAN ID selects a frame, so xtra_params
 *   is accepted (to satisfy the ICommDriver interface) but ignored on every
 *   call. A non-empty value is logged at WARNING level and otherwise has no
 *   effect.
 *
 * Timeout handling:
 *   poll(2) on the socket fd before every recv(2)/send(2). u32*Timeout = 0
 *   selects the respective DEFAULT_TIMEOUT constant. Connection
 *   establishment uses its own timeout (see open()), implemented as a
 *   non-blocking connect(2) followed by poll(2) for POLLOUT.
 *
 * Thread safety:
 *   All public mTCPIPods are protected by an internal mutex. tout_read() /
 *   tout_write() do not need to release the lock around blocking I/O the
 *   way the CAN driver does (there is no shared filter/TX-id state to
 *   serialize access to here), but is_open()/open()/close() are still
 *   mutex-protected against concurrent use.
 */
class TCPIP : public ICommDriver
{
    public:

        static constexpr size_t   TCPIP_MAX_BUFLENGTH          = 256;   /**< Max assembled buffer length (delimiter/token modes). */
        static constexpr uint32_t TCPIP_READ_DEFAULT_TIMEOUT    = 5000; /**< Default read timeout in milliseconds.                */
        static constexpr uint32_t TCPIP_WRITE_DEFAULT_TIMEOUT   = 5000; /**< Default write timeout in milliseconds.               */
        static constexpr uint32_t TCPIP_CONNECT_DEFAULT_TIMEOUT = 5000; /**< Default connect timeout in milliseconds.             */

        TCPIP() = default;

        /**
         * @brief Construct and immediately connect to strHost:u16Port.
         * @param strHost           Hostname or IP address, e.g. "192.168.1.10" or "myhost.local".
         * @param u16Port           TCP port number.
         * @param u32ConnectTimeout Connect timeout in milliseconds (0 = use default).
         */
        explicit TCPIP(const std::string& strHost, uint16_t u16Port, uint32_t u32ConnectTimeout = 0)
        {
            open(strHost, u16Port, u32ConnectTimeout);
        }

        virtual ~TCPIP()
        {
            close();
        }

        /**
         * @brief Resolve strHost, open a TCP socket, and connect() to it.
         *
         * Tries every address returned by getaddrinfo() (e.g. a hostname that
         * resolves to both IPv4 and IPv6) in turn until one connects or the
         * list is exhausted.
         *
         * @param strHost            Hostname or IP address.
         * @param u16Port            TCP port number.
         * @param u32ConnectTimeout  Connect timeout in milliseconds (0 = use default),
         *                           applied per candidate address.
         * @return Status::SUCCESS or an error code.
         */
        Status open(const std::string& strHost, uint16_t u16Port, uint32_t u32ConnectTimeout = 0);

        /**
         * @brief Close the socket.
         * @return Status::SUCCESS.
         */
        Status close();

        /**
         * @brief Check whTCPIPer the socket is open (connected).
         * @return true if the socket fd is valid.
         */
        bool is_open() const override;

        /**
         * @brief Unified read interface supporting multiple operation modes.
         *
         * @param u32ReadTimeout  Timeout in milliseconds (0 = use default).
         * @param buffer          Buffer to receive data into.
         * @param options         Read operation configuration.
         * @param xtra_params     Unused by this driver (single-peer TCP client);
         *                        accepted only to satisfy ICommDriver. A non-empty
         *                        value is logged and otherwise ignored.
         * @return ReadResult containing status, bytes read, and terminator found flag.
         *
         * @details
         * - ReadMode::Exact:          One recv(2); copies up to buffer.size() bytes.
         * - ReadMode::UntilDelimiter: Accumulate bytes across chunks until delimiter found.
         * - ReadMode::UntilToken:     KMP search across streamed bytes; bytes_read = 0.
         */
        ReadResult tout_read(uint32_t u32ReadTimeout,
                             std::span<uint8_t> buffer,
                             const ReadOptions& options,
                             std::string_view xtra_params = {}) const override;

        /**
         * @brief Unified write interface.
         *
         * @param u32WriteTimeout  Timeout in milliseconds (0 = use default).
         * @param buffer           Data to send.
         * @param xtra_params      Unused by this driver; see tout_read().
         * @return WriteResult containing status and bytes written.
         */
        WriteResult tout_write(uint32_t u32WriteTimeout,
                               std::span<const uint8_t> buffer,
                               std::string_view xtra_params = {}) const override;

    private:

        int                 m_iHandle = -1;      /**< Socket file descriptor.     */
        mutable std::mutex  m_mutex;             /**< Protects concurrent access. */

        // -----------------------------------------------------------------------
        // Internal transport primitives
        // -----------------------------------------------------------------------

        /**
         * @brief Receive whatever is available (up to buffer.size()) in one
         * poll(2) + recv(2) pair. bytes_read is set to the number of bytes
         * actually received.
         */
        Status timeout_read(uint32_t u32ReadTimeout,
                            std::span<uint8_t> buffer,
                            size_t& szBytesRead) const;

        /**
         * @brief Accumulate received bytes until cDelimiter is found or the
         * buffer is full. Null-terminates on Status::SUCCESS.
         */
        Status timeout_read_until(uint32_t u32ReadTimeout,
                                  std::span<uint8_t> buffer,
                                  uint8_t cDelimiter,
                                  size_t& szBytesRead) const;

        /**
         * @brief Stream bytes off the socket, applying the KMP algorithm to
         * detect the token sequence.
         */
        Status timeout_wait_for_token(uint32_t u32ReadTimeout,
                                      std::span<const uint8_t> token,
                                      bool useBuffer) const;

        /**
         * @brief Send buffer over the socket, looping over send(2) as needed
         * to cover short writes, until the overall write timeout elapses.
         */
        Status timeout_write(uint32_t u32WriteTimeout,
                             std::span<const uint8_t> buffer,
                             size_t& szBytesWritten) const;

        // -----------------------------------------------------------------------
        // KMP helpers (identical strategy to the UART / I2C / SPI / CAN drivers)
        // -----------------------------------------------------------------------

        /** @brief Run KMP stream matching over bytes received from the socket. */
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


#endif // U_TCPIP_DRIVER_H
