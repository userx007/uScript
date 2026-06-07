#ifndef U_CAN_DRIVER_H
#define U_CAN_DRIVER_H

#include "ICommDriver.hpp"

#include <string>
#include <vector>
#include <span>
#include <mutex>
#include <cstdint>


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
 *     outgoing frames is set via set_tx_id() (default: 0x000).
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

        static constexpr size_t   CAN_DRV_MAX_DLEN          = 64;   /**< Max payload per frame (KVCAN FD).             */
        static constexpr size_t   CAN_DRV_MAX_BUFLENGTH      = 256;  /**< Max assembled buffer length.                */
        static constexpr uint32_t CAN_READ_DEFAULT_TIMEOUT   = 5000; /**< Default read timeout in milliseconds.       */
        static constexpr uint32_t CAN_WRITE_DEFAULT_TIMEOUT  = 5000; /**< Default write timeout in milliseconds.      */

        /**
         * @brief A single hardware acceptance filter (wraps struct can_filter).
         */
        struct CanFilter
        {
            uint32_t can_id;   /**< KVCAN ID to match (may include EFF/RTR/ERR flags). */
            uint32_t can_mask; /**< Mask applied before comparison.                  */
        };

        KVCAN() = default;

        /**
         * @brief Construct and immediately open the interface.
         * @param strIface  SocketCAN interface name, e.g. "vcan0" or "can1".
         */
        explicit KVCAN(const std::string& strIface)
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
         * @brief Set the KVCAN ID stamped on every outgoing frame.
         * @param u32Id  11-bit (standard) or 29-bit (extended, set CAN_EFF_FLAG) ID.
         */
        void set_tx_id(uint32_t u32Id);

        /**
         * @brief Install a list of hardware acceptance filters via SO_CAN_RAW_FILTER.
         *
         * An empty list removes all filters (accept everything).
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
         * @return ReadResult containing status, bytes read, and terminator found flag.
         *
         * @details
         * - ReadMode::Exact:          Receive one frame; copy payload to buffer.
         * - ReadMode::UntilDelimiter: Accumulate payloads across frames until delimiter found.
         * - ReadMode::UntilToken:     KMP search across frame payloads; bytes_read = 0.
         */
        ReadResult tout_read(uint32_t u32ReadTimeout,
                             std::span<uint8_t> buffer,
                             const ReadOptions& options) const override;

        /**
         * @brief Unified write interface.
         *
         * Packs buffer into the payload of a single KVCAN frame and transmits it.
         * buffer.size() must be ≤ CAN_DRV_MAX_DLEN (64 bytes).
         *
         * @param u32WriteTimeout  Timeout in milliseconds (0 = use default).
         * @param buffer           Payload to transmit (max CAN_DRV_MAX_DLEN bytes).
         * @return WriteResult containing status and bytes written.
         */
        WriteResult tout_write(uint32_t u32WriteTimeout,
                               std::span<const uint8_t> buffer) const override;

    private:

        int                m_iHandle  = -1;      /**< Socket file descriptor.                    */
        uint32_t           m_u32TxId  = 0x000u;  /**< KVCAN ID for outgoing frames.               */
        mutable std::mutex m_mutex;               /**< Protects concurrent access.                */

        // -----------------------------------------------------------------------
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
         */
        Status timeout_write(uint32_t u32WriteTimeout,
                             std::span<const uint8_t> buffer,
                             size_t& szBytesWritten) const;

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
