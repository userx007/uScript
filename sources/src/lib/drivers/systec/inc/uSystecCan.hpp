#ifndef U_SYSTEC_CAN_DRIVER_H
#define U_SYSTEC_CAN_DRIVER_H

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
 * @brief Linux SocketCAN driver implementing the ICommDriver interface, for
 *        SYS TEC electronic USB-CANmodul devices (kernel driver: systec_can.ko).
 *
 * Works with any interface registered by systec_can.ko (physical can0, can1 …
 * one per USB-CANmodul channel; register_candev() in the kernel driver makes
 * this indistinguishable from any other SocketCAN interface at the socket
 * level). The driver opens a raw PF_CAN socket, binds it to the requested
 * interface, and exposes the same ICommDriver read/write surface used by the
 * UART, I2C, SPI and KVCAN drivers.
 *
 * @note Unlike uKVCan's SocketCAN wrapper, this driver does NOT enable
 *       CAN_RAW_FD_FRAMES / emit canfd_frame: systec_can.ko never sets
 *       CAN_CTRLMODE_FD on the netdev (see systec_can.c — ctrlmode_supported
 *       is only CAN_CTRLMODE_3_SAMPLES | CAN_CTRLMODE_LISTENONLY [|
 *       CAN_CTRLMODE_ONE_SHOT]) and never raises netdev->mtu past CAN_MTU,
 *       so every USB-CANmodul channel is classic-CAN only: payloads are
 *       capped at CAN_DRV_MAX_DLEN (8) bytes, matching struct can_frame.
 *
 * SYSTECCAN is frame-based, not a byte-stream.  The driver maps the
 * abstraction as follows:
 *
 *   tout_write()
 *     Packs buffer.data() into the payload of a single classic CAN frame and
 *     transmits it.  buffer.size() must be ≤ CAN_MAX_DLEN (8).  The CAN ID
 *     to stamp on outgoing frames is set via set_tx_id() (default: 0x000),
 *     or overridden per-call via the xtra_params parameter.
 *
 *   tout_read() — ReadMode::Exact
 *     Receives one CAN frame and copies its payload into buffer.  If
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
 *   any time after open() to restrict which incoming CAN IDs are accepted.
 *
 * SYS TEC hardware extras (see the hw_get_/hw_set_ methods below):
 *   systec_can.ko exposes device- and channel-specific controls that have no
 *   SocketCAN-generic equivalent, via sysfs rather than the CAN_RAW socket:
 *     - device-scoped  (one USB-CANmodul, shared by both channels on a dual
 *       channel unit): /sys/class/net/<iface>/device/{devicenr,reset,
 *       dual_channel,status_timeout,high_performance}
 *     - channel-scoped (this netdev only):
 *       /sys/class/net/<iface>/{channel,tx_timeout_ms}
 *   These are plain sysfs attribute reads/writes (implemented in
 *   uSystecCanLinux.cpp alongside open()/close(), using ifstream/ofstream —
 *   not part of the CAN_RAW socket path), and do not require the socket to
 *   be open — only the interface name.
 *
 * Thread safety:
 *   All public methods are protected by an internal mutex.
 */
class SYSTECCAN : public ICommDriver
{
    public:

        static constexpr size_t   CAN_DRV_MAX_DLEN           = 8;    /**< Max payload per frame (classic CAN only — see class docs). */
        static constexpr size_t   CAN_DRV_MAX_BUFLENGTH      = 256;  /**< Max assembled buffer length.               */
        static constexpr uint32_t CAN_READ_DEFAULT_TIMEOUT   = 5000; /**< Default read timeout in milliseconds.      */
        static constexpr uint32_t CAN_WRITE_DEFAULT_TIMEOUT  = 5000; /**< Default write timeout in milliseconds.     */

        /**
         * @brief A single hardware acceptance filter (wraps struct can_filter).
         */
        struct CanFilter
        {
            uint32_t can_id;   /**< CAN ID to match (may include EFF/RTR/ERR flags). */
            uint32_t can_mask; /**< Mask applied before comparison.                   */
        };

        SYSTECCAN() = default;

        /**
         * @brief Construct and immediately open the interface.
         * @param strIface         SocketCAN interface name, e.g. "can0" or "can1"
         *                         (systec_can.ko registers one such netdev per
         *                         USB-CANmodul channel).
         * @param strIdentityLabel Display text for the GUI comm-dump panel (see
         *                         describeConnection()), supplied separately from
         *                         strIface — e.g. "can1" or a friendlier bus name.
         */
        explicit SYSTECCAN(const std::string& strIface, const std::string& strIdentityLabel = {})
            : m_strIdentityLabel(strIdentityLabel)
        {
            open(strIface);
        }

        virtual ~SYSTECCAN()
        {
            close();
        }

        /**
         * @brief Open a raw SocketCAN socket and bind it to @p strIface.
         * @param strIface  Interface name (e.g. "can0").
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
                              m_strIdentityLabel.empty() ? "SYSTEC" : m_strIdentityLabel.c_str(),
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
        Status set_filters(const std::vector<CanFilter>& filters);

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
                             const ReadOptions& options,
                             std::string_view xtra_params = {}) const override;

        /**
         * @brief Unified write interface.
         *
         * Packs buffer into the payload of a single classic CAN frame and transmits it.
         * buffer.size() must be ≤ CAN_DRV_MAX_DLEN (8 bytes).
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
                               std::string_view xtra_params = {}) const override;

        // ================================================================
        // SYS TEC hardware extras — sysfs-backed, no open socket required.
        // Mirror the device_attribute / interface sysfs groups registered
        // by systec_can.ko (see systec_can.c: systec_can_device_sysfs_attr_group,
        // systec_can_interface_sysfs_attr_group). All take the SocketCAN
        // interface name directly so they can be used before/without open().
        // ================================================================

        /** @brief Device-scoped (shared across both channels of a dual-channel unit). */
        static Status hw_get_devicenr        (const std::string& strIface, uint32_t& u32DeviceNr);
        static Status hw_set_devicenr        (const std::string& strIface, uint32_t u32DeviceNr); ///< valid range 0-254
        static Status hw_reset               (const std::string& strIface); ///< USBCAN_CMD_RESET_HW — write-only trigger
        static Status hw_get_dual_channel    (const std::string& strIface, bool& bDualChannel);
        static Status hw_get_status_timeout  (const std::string& strIface, uint32_t& u32TimeoutMs);
        static Status hw_set_status_timeout  (const std::string& strIface, uint32_t u32TimeoutMs);
        static Status hw_get_high_performance(const std::string& strIface, bool& bHighPerformance);
        static Status hw_set_high_performance(const std::string& strIface, bool bHighPerformance);

        /** @brief Channel-scoped (this netdev only). */
        static Status hw_get_channel         (const std::string& strIface, uint32_t& u32ChanNo);
        static Status hw_get_tx_timeout_ms   (const std::string& strIface, uint32_t& u32TimeoutMs);
        static Status hw_set_tx_timeout_ms   (const std::string& strIface, uint32_t u32TimeoutMs); ///< dual-channel units only (ENOSYS otherwise)

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
         * @brief Receive one classic CAN frame; copy its payload into buffer.
         * Uses poll(2) for the timeout, then a single recv(2) call.
         * bytes_read is set to the received DLC (payload length).
         */
        Status timeout_read(uint32_t u32ReadTimeout,
                            std::span<uint8_t> buffer,
                            size_t& szBytesRead) const;

        /**
         * @brief Accumulate CAN frame payloads until cDelimiter is found or
         * buffer is full.  Null-terminates on Status::SUCCESS.
         */
        Status timeout_read_until(uint32_t u32ReadTimeout,
                                  std::span<uint8_t> buffer,
                                  uint8_t cDelimiter,
                                  size_t& szBytesRead) const;

        /**
         * @brief Stream payload bytes across consecutive CAN frames, applying
         * the KMP algorithm to detect the token sequence.
         */
        Status timeout_wait_for_token(uint32_t u32ReadTimeout,
                                      std::span<const uint8_t> token,
                                      bool useBuffer) const;

        /**
         * @brief Pack buffer into a classic CAN frame payload and transmit it.
         * @param u32TxId  The CAN ID to stamp on the outgoing frame.
         */
        Status timeout_write(uint32_t u32WriteTimeout,
                             std::span<const uint8_t> buffer,
                             size_t& szBytesWritten,
                             uint32_t u32TxId) const;

        // -----------------------------------------------------------------------
        // KMP helpers (identical strategy to UART / I2C / SPI / KVCAN drivers)
        // -----------------------------------------------------------------------

        /** @brief Run KMP stream matching over CAN frame payload bytes. */
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


#endif // U_SYSTEC_CAN_DRIVER_H
