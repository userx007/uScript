#ifndef U_CAN_DRIVER_H
#define U_CAN_DRIVER_H

#include "ICommDriver.hpp"

#include <string>
#include <string_view>
#include <optional>
#include <vector>
#include <span>
#include <mutex>
#include <cstdint>
#include <cstdio>


/**
 * @brief Linux SocketCAN driver implementing the ICommDriver interface.
 *
 * Works with any SocketCAN network interface: physical (can0, can1 …) or
 * virtual (vcan0, vcan1 …).  The driver opens a raw PF_CAN socket, binds it
 * to the requested interface, and exposes the same ICommDriver read/write
 * surface used by the UART, I2C and SPI drivers.
 *
 * KVCAN is frame-based, not a byte-stream.  The driver maps the abstraction as
 * follows:
 *
 *   tout_write()
 *     Packs buffer.data() into the payload of a single KVCAN frame and
 *     transmits it.  buffer.size() must be ≤ CAN_MAX_DLEN (8) for classic
 *     KVCAN or ≤ CANFD_MAX_DLEN (64) for KVCAN FD.  The KVCAN ID to stamp on
 *     outgoing frames is set via set_tx_id() (default: 0x000), or overridden
 *     per-call via the xtra_params parameter.
 *
 *   tout_read() — ReadMode::Exact
 *     Receives one KVCAN frame and copies its payload into buffer.  If
 *     buffer.size() < frame.can_dlc the payload is truncated; bytes_read
 *     reflects actual payload length.
 *
 *   tout_read() — ReadMode::UntilDelimiter
 *     Receives frames one at a time, appending their payloads until the
 *     delimiter byte is found in a payload or the buffer is full.
 *
 *   tout_read() — ReadMode::UntilToken
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
 *   poll(2) on the socket fd before every blocking receive.
 *   u32*Timeout = 0 selects the respective DEFAULT_TIMEOUT constant.
 *
 * Filtering:
 *   set_filters() wraps setsockopt(SO_CAN_RAW_FILTER) and may be called at
 *   any time after open() to restrict which incoming KVCAN IDs are accepted.
 *
 * Thread safety:
 *   All public methods are protected by an internal mutex.
 */
class KVCAN : public ICommDriver
{
    public:

        static constexpr size_t   CAN_DRV_MAX_DLEN           = 64;   /**< Max payload per frame (KVCAN FD).          */
        static constexpr size_t   CAN_DRV_MAX_BUFLENGTH      = 256;  /**< Max assembled buffer length.               */
        static constexpr uint32_t CAN_READ_DEFAULT_TIMEOUT   = 5000; /**< Default read timeout in milliseconds.      */
        static constexpr uint32_t CAN_WRITE_DEFAULT_TIMEOUT  = 5000; /**< Default write timeout in milliseconds.     */

        /**
         * @brief A single hardware acceptance filter (wraps struct can_filter).
         */
        struct CanFilter
        {
            uint32_t can_id;   /**< KVCAN ID to match (may include EFF/RTR/ERR flags). */
            uint32_t can_mask; /**< Mask applied before comparison.                    */
        };

        KVCAN() = default;

        /**
         * @brief Construct and immediately open the interface.
         * @param strIface         SocketCAN interface name, e.g. "vcan0" or "can1".
         * @param strIdentityLabel Display text for the GUI comm-dump panel (see
         *                         describeConnection()), supplied separately from
         *                         strIface — e.g. "can1" or a friendlier bus name.
         */
        explicit KVCAN(const std::string& strIface, const std::string& strIdentityLabel = {})
            : m_strIdentityLabel(strIdentityLabel)
        {
            open(strIface);
        }

        virtual ~KVCAN()
        {
            close();
        }

        /**
         * @brief Open a raw SocketCAN socket and bind it to @p strIface.
         * @param strIface  Interface name (e.g. "vcan0").
         * @return Status::SUCCESS or an error code.
         */
        Status open(const std::string& strIface);

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
                              m_strIdentityLabel.empty() ? "KVCAN" : m_strIdentityLabel.c_str(),
                          id);
            return commdump_details(CommFamily::CAN, label);
        }

        /**
         * @brief Set the KVCAN ID stamped on every outgoing frame.
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
        Status set_filters(const std::vector<CanFilter>& filters);

        /**
         * @brief Unified read interface supporting multiple operation modes.
         *
         * @param u32ReadTimeout  Timeout in milliseconds (0 = use default).
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
                             const ReadOptions& options,
                             std::string_view xtra_params = {}) const override;

        /**
         * @brief Unified write interface.
         *
         * Packs buffer into the payload of a single KVCAN frame and transmits it.
         * buffer.size() must be ≤ CAN_DRV_MAX_DLEN (64 bytes).
         *
         * @param u32WriteTimeout  Timeout in milliseconds (0 = use default).
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
                               std::string_view xtra_params = {}) const override;

    private:

        int                     m_iHandle  = -1;      /**< Socket file descriptor.                    */
        uint32_t                m_u32TxId  = 0x000u;  /**< Default CAN ID for outgoing frames.        */
        mutable std::vector<CanFilter>  m_vFilters;           /**< Mirrors the filter set currently installed
                                                             on the socket (empty == accept-all). Kept
                                                             in sync by set_filters() and used by
                                                             tout_read() to snapshot/restore around a
                                                             transient per-call filter.               */
        mutable std::mutex      m_mutex;              /**< Protects concurrent access.                */
        std::string             m_strIdentityLabel;   /**< GUI comm-dump display label, see describeConnection(). */

        // -----------------------------------------------------------------------
        uint32_t resolveTxId(std::string_view xtra_params) const;
        // Internal transport primitives
        // -----------------------------------------------------------------------

        /**
         * @brief Receive one KVCAN frame; copy its payload into buffer.
         * Uses poll(2) for the timeout, then a single recv(2) call.
         * bytes_read is set to the received DLC (payload length).
         */
        Status timeout_read(uint32_t u32ReadTimeout,
                            std::span<uint8_t> buffer,
                            size_t& szBytesRead) const;

        /**
         * @brief Accumulate KVCAN frame payloads until cDelimiter is found or
         * buffer is full.  Null-terminates on Status::SUCCESS.
         */
        Status timeout_read_until(uint32_t u32ReadTimeout,
                                  std::span<uint8_t> buffer,
                                  uint8_t cDelimiter,
                                  size_t& szBytesRead) const;

        /**
         * @brief Stream payload bytes across consecutive KVCAN frames, applying
         * the KMP algorithm to detect the token sequence.
         */
        Status timeout_wait_for_token(uint32_t u32ReadTimeout,
                                      std::span<const uint8_t> token,
                                      bool useBuffer) const;

        /**
         * @brief Pack buffer into a KVCAN frame payload and transmit it.
         * @param u32TxId  The CAN ID to stamp on the outgoing frame.
         */
        Status timeout_write(uint32_t u32WriteTimeout,
                             std::span<const uint8_t> buffer,
                             size_t& szBytesWritten,
                             uint32_t u32TxId) const;

        // -----------------------------------------------------------------------
        // KMP helpers (identical strategy to UART / I2C / SPI drivers)
        // -----------------------------------------------------------------------

        /** @brief Run KMP stream matching over KVCAN frame payload bytes. */
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


#endif // U_CAN_DRIVER_H
