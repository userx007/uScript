#ifndef U_KCAN_DRIVER_H
#define U_KCAN_DRIVER_H

#include "ICommDriver.hpp"

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

/**
 * @brief Linux SocketCAN frame driver for REAL CAN adapters (can0, can1 ...), implementing ICommDriver.
 *
 * Works with any SocketCAN network interface - a physical adapter exposed by a
 * kernel driver (PEAK PCAN-USB via peak_usb, Kvaser, gs_usb/candleLight, on-board
 * controllers ...) or a virtual one (vcan0). The driver opens a raw PF_CAN socket,
 * binds it to the requested interface and exposes the same ICommDriver
 * read/write surface used by the UART, I2C and SPI drivers.
 *
 * What KCAN adds on top of the plain-socket KVCAN driver:
 *   - bus speed / link control: see uKCanLink.hpp (kcan::link_apply(), ...), which
 *     programs bitrate, sample point, CAN FD data bitrate, controller modes and
 *     brings the interface up/down over rtnetlink (the `ip link set can0 type can
 *     bitrate ...` equivalent, no external tool needed). The frame driver itself
 *     never changes interface state; open() only needs the interface to be up.
 *   - real-bus write semantics: a TX queue that is full (no other node ACKs, bus
 *     off, cable unplugged) is waited on for the caller's write timeout instead of
 *     blocking forever / failing immediately.
 *   - optional error-frame reception (set_error_mask()): bus-off, error-passive,
 *     controller problems ... are logged and never returned as payload data.
 *
 * CAN is frame-based, not a byte-stream. The driver maps the abstraction as
 * follows:
 *
 *   tout_write()
 *     Packs buffer.data() into the payload of a single CAN frame and
 *     transmits it.  buffer.size() must be <= CAN_MAX_DLEN (8) for classic
 *     CAN or <= CANFD_MAX_DLEN (64) for CAN FD.  The CAN ID to stamp on
 *     outgoing frames is set via set_tx_id() (default: 0x000), or overridden
 *     per-call via the xtra_params parameter.
 *
 *   tout_read() - ReadMode::Exact
 *     Receives one CAN frame and copies its payload into buffer.  If
 *     buffer.size() < frame length the payload is truncated; bytes_read
 *     reflects actual copied length.
 *
 *   tout_read() - ReadMode::UntilDelimiter
 *     Receives frames one at a time, appending their payloads until the
 *     delimiter byte is found in a payload or the buffer is full.
 *
 *   tout_read() - ReadMode::UntilToken
 *     Streams payload bytes across consecutive frames, applying the KMP
 *     algorithm to detect the token sequence.  bytes_read = 0 on return.
 *
 * Per-call CAN ID (xtra_params parameter):
 *   Both tout_read() and tout_write() accept an optional xtra_params string.
 *   When non-empty it is parsed as a CAN ID (decimal or "0x"-prefixed hex,
 *   e.g. "0x1A0" or "416") and used for that single call only:
 *     - tout_write(): overrides the TX ID set by set_tx_id().
 *     - tout_read():  installs a temporary single-ID acceptance filter so
 *                     only frames matching that ID are received; the previous
 *                     filter set is restored when the call returns.
 *   An empty (or omitted) xtra_params falls back to the driver defaults.
 *
 * Timeout handling:
 *   poll(2) on the socket fd before every blocking receive / transmit.
 *   0 = no timeout (wait until the operation completes or a stop is requested).
 *
 * Filtering:
 *   set_filters() wraps setsockopt(SO_CAN_RAW_FILTER) and may be called at
 *   any time after open() to restrict which incoming CAN IDs are accepted.
 *
 * Thread safety:
 *   All public methods are protected by an internal mutex.
 */
class KCAN : public ICommDriver {
    public:
        static constexpr size_t CAN_DRV_MAX_DLEN            = 64;   /**< Max payload per frame (CAN FD).          */
        static constexpr size_t CAN_DRV_MAX_BUFLENGTH       = 256;  /**< Max assembled buffer length.               */
        static constexpr uint32_t CAN_READ_DEFAULT_TIMEOUT  = 5000; /**< Default read timeout in milliseconds.      */
        static constexpr uint32_t CAN_WRITE_DEFAULT_TIMEOUT = 5000; /**< Default write timeout in milliseconds.     */

        /**
         * @brief A single hardware acceptance filter (wraps struct can_filter).
         */
        struct CanFilter {
                uint32_t can_id;   /**< CAN ID to match (may include EFF/RTR/ERR flags). */
                uint32_t can_mask; /**< Mask applied before comparison.                    */
        };

        KCAN() = default;

        /**
         * @brief Construct and immediately open the interface.
         * @param strIface         SocketCAN interface name, e.g. "vcan0" or "can1".
         * @param strIdentityLabel Display text for the GUI comm-dump panel (see
         *                         describeConnection()), supplied separately from
         *                         strIface — e.g. "can1" or a friendlier bus name.
         */
        explicit KCAN(const std::string &strIface, const std::string &strIdentityLabel = {})
            : m_strIdentityLabel(strIdentityLabel)
        {
            open(strIface);
        }

        virtual ~KCAN()
        {
            close();
        }

        /**
         * @brief Open a raw SocketCAN socket and bind it to @p strIface.
         * @param strIface  Interface name (e.g. "vcan0").
         * @return Status::SUCCESS or an error code.
         */
        Status open(const std::string &strIface);

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
         * xtra_params empty: "<label> id=0x<m_u32TxId>".
         * xtra_params non-empty: same per-call CAN ID override tout_read()/
         * tout_write() apply (see class docs) — the string is already a valid
         * ID literal ("0x150" or "336"), so it's shown as-is rather than
         * re-parsed, since that's exactly what this exchange targeted.
         */
        CommDetails describeConnection(std::string_view xtra_params = {}) const override
        {
            const uint32_t id = resolveTxId(xtra_params);
            char label[k_labelSize];
            std::snprintf(label, sizeof(label), "%s id=0x%X",
                          m_strIdentityLabel.empty() ? "KCAN" : m_strIdentityLabel.c_str(),
                          id);
            return commdump_details(CommFamily::CAN, label);
        }

        /**
         * @brief Set the CAN ID stamped on every outgoing frame.
         *
         * This becomes the default TX ID used by tout_write() when no
         * xtra_params override is supplied.
         *
         * @param u32Id  11-bit (standard) or 29-bit (extended, set CAN_EFF_FLAG) ID.
         */
        void set_tx_id(uint32_t u32Id);

        /**
         * @brief Install a list of hardware acceptance filters via SO_CAN_RAW_FILTER.
         *
         * An empty list removes all filters (accept everything).
         * These filters act as the default when tout_read() is called without a
         * xtra_params override.
         *
         * @param filters  Vector of CanFilter entries.
         * @return Status::SUCCESS or Status::PORT_ACCESS on ioctl failure.
         */
        Status set_filters(const std::vector<CanFilter> &vFilters);

        /**
         * @brief Select which CAN error classes the kernel should report as error frames
         *        (setsockopt CAN_RAW_ERR_FILTER). 0 (default) = no error frames.
         *
         * Error frames are never handed to the caller as payload: tout_read() logs them
         * (bus-off, controller problems, TX timeout, ... ) and keeps waiting for the next
         * data frame. Use CAN_ERR_MASK (0x1FFFFFFF) to receive all classes.
         *
         * @param u32Mask  CAN_ERR_* class bitmask (see linux/can/error.h)
         * @return Status::SUCCESS or Status::PORT_ACCESS on setsockopt failure.
         */
        Status set_error_mask(uint32_t u32Mask);

        /** @brief Name of the SocketCAN interface this driver is bound to ("" if not open). */
        const std::string &iface() const
        {
            return m_strIface;
        }

        /**
         * @brief Unified read interface supporting multiple operation modes.
         *
         * @param u32ReadTimeout  Timeout in milliseconds (0 = block indefinitely / infinite timeout).
         * @param buffer          Buffer to receive payload data into.
         * @param options         Read operation configuration.
         * @param xtra_params      Optional CAN ID to receive from for this call only.
         *                        Accepted formats: decimal ("336") or hex ("0x150").
         *                        When non-empty a temporary single-ID acceptance filter
         *                        is installed and removed before this method returns,
         *                        leaving the filter state unchanged for other callers.
         *                        An empty string (default) uses the filters installed
         *                        by set_filters() / the socket default.
         * @return ReadResult containing status, bytes read, and terminator found flag.
         *
         * @details
         * - ReadMode::Exact:          Receive one frame; copy payload to buffer.
         * - ReadMode::UntilDelimiter: Accumulate payloads across frames until delimiter found.
         * - ReadMode::UntilToken:     KMP search across frame payloads; bytes_read = 0.
         */
        ReadResult tout_read(uint32_t u32ReadTimeout,
                             std::span<uint8_t> buffer,
                             const ReadOptions &sOptions,
                             std::string_view xtra_params = {},
                             std::stop_token stop_tok     = {}) const override;

        /**
         * @brief Unified write interface.
         *
         * Packs buffer into the payload of a single CAN frame and transmits it.
         * buffer.size() must be ≤ CAN_DRV_MAX_DLEN (64 bytes).
         *
         * @param u32WriteTimeout  Timeout in milliseconds (0 = block indefinitely / infinite timeout).
         * @param buffer           Payload to transmit (max CAN_DRV_MAX_DLEN bytes).
         * @param xtra_params       Optional CAN TX ID for this call only.
         *                         Accepted formats: decimal ("336") or hex ("0x150").
         *                         When non-empty it overrides the TX ID set by
         *                         set_tx_id() for this single transmission only.
         *                         An empty string (default) uses the set_tx_id() value.
         * @return WriteResult containing status and bytes written.
         */
        WriteResult tout_write(uint32_t u32WriteTimeout,
                               std::span<const uint8_t> buffer,
                               std::string_view xtra_params = {},
                               std::stop_token stop_tok     = {}) const override;

    private:
        int m_iHandle = -1;                        /**< Socket file descriptor.                    */
        std::string m_strIface;                    /**< Interface name the socket is bound to.      */
        uint32_t m_u32ErrMask = 0;                 /**< CAN_RAW_ERR_FILTER currently installed.     */
        uint32_t m_u32TxId    = 0x000u;            /**< Default CAN ID for outgoing frames.        */
        mutable std::vector<CanFilter> m_vFilters; /**< Mirrors the filter set currently installed
                                                  on the socket (empty == accept-all). Kept
                                                  in sync by set_filters() and used by
                                                  tout_read() to snapshot/restore around a
                                                  transient per-call filter.               */
        mutable std::mutex m_mutex;                /**< Protects concurrent access.                */
        std::string m_strIdentityLabel;            /**< GUI comm-dump display label, see describeConnection(). */

        // -----------------------------------------------------------------------
        uint32_t resolveTxId(std::string_view xtra_params) const;
        // Internal transport primitives
        // -----------------------------------------------------------------------

        /**
         * @brief Receive one CAN frame; copy its payload into buffer.
         * Uses poll(2) for the timeout, then a single recv(2) call.
         * bytes_read is set to the received DLC (payload length).
         */
        Status timeout_read(uint32_t u32ReadTimeout,
                            std::span<uint8_t> buffer,
                            size_t &szBytesRead,
                            std::stop_token stop_tok = {}) const;

        /**
         * @brief Accumulate CAN frame payloads until cDelimiter is found or
         * buffer is full.  Null-terminates on Status::SUCCESS.
         */
        Status timeout_read_until(uint32_t u32ReadTimeout,
                                  std::span<uint8_t> buffer,
                                  uint8_t u8CDelimiter,
                                  size_t &szBytesRead,
                                  std::stop_token stop_tok = {}) const;

        /**
         * @brief Stream payload bytes across consecutive CAN frames, applying
         * the KMP algorithm to detect the token sequence.
         */
        Status timeout_wait_for_token(uint32_t u32ReadTimeout,
                                      std::span<const uint8_t> token,
                                      bool bUseBuffer,
                                      std::stop_token stop_tok = {}) const;

        /**
         * @brief Pack buffer into a CAN frame payload and transmit it.
         * Waits (poll POLLOUT / retry on ENOBUFS) up to u32WriteTimeout ms for room in the
         * adapter's TX queue; 0 = wait until room appears or a stop is requested.
         * @param u32TxId  The CAN ID to stamp on the outgoing frame.
         */
        Status timeout_write(uint32_t u32WriteTimeout,
                             std::span<const uint8_t> buffer,
                             size_t &szBytesWritten,
                             uint32_t u32TxId,
                             std::stop_token stop_tok = {}) const;

        // -----------------------------------------------------------------------
        // KMP helpers (identical strategy to UART / I2C / SPI drivers)
        // -----------------------------------------------------------------------

        /** @brief Run KMP stream matching over CAN frame payload bytes. */
        Status kmp_stream_match(std::span<const uint8_t> token,
                                const std::vector<int> &vViLps,
                                uint32_t u32Timeout,
                                bool bReturnOnTimeout,
                                bool bUseBuffer,
                                std::stop_token stop_tok = {}) const;

        /** @brief Build the KMP failure-function table for @p pattern. */
        void build_kmp_table(std::span<const uint8_t> pattern,
                             size_t szLength,
                             std::vector<int> &vViLps) const;
};

#endif // U_KCAN_DRIVER_H
