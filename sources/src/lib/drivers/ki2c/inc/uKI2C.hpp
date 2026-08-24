#ifndef U_KI2C_DRIVER_H
#define U_KI2C_DRIVER_H

#include "ICommDriver.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <mutex>
#include <cstdint>
#include <cstdio>


/**
 * @brief Linux kernel KI2C driver implementing the ICommDriver interface.
 *
 * Wraps /dev/i2c-N character devices using the Linux i2c-dev kernel interface.
 * Each instance is bound to a single slave address set at open() time.
 *
 * Read semantics:
 *   - ReadMode::Exact          — plain i2c_read() of exactly buffer.size() bytes.
 *   - ReadMode::UntilDelimiter — repeated single-byte reads until the delimiter
 *                                byte is received or the buffer is full.
 *   - ReadMode::UntilToken     — KMP streaming search; does not fill the user
 *                                buffer (bytes_read = 0), only signals detection.
 *
 * Write semantics:
 *   - tout_write() performs a single i2c_write() of the full buffer.
 *
 * Per-call slave address (xtra_params parameter):
 *   Both tout_read() and tout_write() accept an optional xtra_params string.
 *   When non-empty it is parsed as a 7-bit slave address (decimal or
 *   "0x"-prefixed hex, e.g. "0x50" or "80") and used for that single call
 *   only, exactly like the CAN drivers' per-call TX/RX id override:
 *     - tout_write(): the write targets this address instead of the one
 *                     bound at open() (m_u8Addr).
 *     - tout_read():  the read targets this address instead of m_u8Addr.
 *   Internally this is implemented as a transient ioctl(I2C_SLAVE, addr)
 *   issued before the call and restored to m_u8Addr immediately afterwards.
 *   Unlike SocketCAN's kernel filters, I2C addressing has no equivalent of a
 *   concurrent third party that could "miss" a frame while the address is
 *   being changed — the exchange is a synchronous point-to-point request on
 *   this same fd — and tout_read()/tout_write() already hold m_mutex for
 *   their entire duration, so no other call on this KI2C instance can
 *   interleave a different address in between. An empty (or unparsable)
 *   xtra_params falls back to m_u8Addr, exactly like an empty xtra_params on
 *   the CAN drivers falls back to their configured defaults.
 *
 * Timeout handling:
 *   - Implemented via poll(2) on the file descriptor before every read attempt.
 *   - A timeout of 0 selects KI2C_READ_DEFAULT_TIMEOUT.
 *
 * Thread safety:
 *   - All public methods are protected by an internal mutex.
 */
class KI2C : public ICommDriver
{
    public:

        static constexpr size_t   KI2C_MAX_BUFLENGTH          = 256;  /**< Maximum KI2C buffer length.                    */
        static constexpr uint32_t KI2C_READ_DEFAULT_TIMEOUT   = 5000; /**< Default KI2C read timeout in milliseconds.     */
        static constexpr uint32_t KI2C_WRITE_DEFAULT_TIMEOUT  = 5000; /**< Default KI2C write timeout in milliseconds.    */

        KI2C() = default;

        /**
         * @brief Construct and immediately open the bus/device.
         * @param strDevice        Path to the i2c-dev node, e.g. "/dev/i2c-1".
         * @param u8Address        7-bit slave address (e.g. 0x48).
         * @param strIdentityLabel Display text for the GUI comm-dump panel (see
         *                         describeConnection()), supplied separately from
         *                         strDevice — e.g. "/dev/i2c-1" or a friendlier bus name.
         */
        explicit KI2C(const std::string& strDevice, uint8_t u8Address,
                     const std::string& strIdentityLabel = {})
            : m_strIdentityLabel(strIdentityLabel)
        {
            open(strDevice, u8Address);
        }

        virtual ~KI2C()
        {
            close();
        }

        /**
         * @brief Open an KI2C bus and bind it to a slave address.
         * @param strDevice  Path to the i2c-dev character device.
         * @param u8Address  7-bit slave address.
         * @return Status::SUCCESS or an error code.
         */
        Status open(const std::string& strDevice, uint8_t u8Address);

        /**
         * @brief Close the KI2C bus file descriptor.
         * @return Status::SUCCESS.
         */
        Status close();

        /**
         * @brief Check whether the bus is currently open.
         * @return true if the file descriptor is valid.
         */
        bool is_open() const override;

        /**
         * @brief Describe this connection for the GUI comm-dump panel.
         *
         * xtra_params empty: "<label> addr=<m_u8Addr, as 0xNN>".
         * xtra_params non-empty: same per-call address override tout_read()/
         * tout_write() apply (see class docs) — the string is already a valid
         * address literal ("0x50" or "80"), so it's shown as-is rather than
         * re-parsed, since that's exactly what this exchange targeted.
         */
        CommDetails describeConnection(std::string_view xtra_params = {}) const override
        {
            char label[k_labelSize];
            if (!xtra_params.empty()) {
                std::snprintf(label, sizeof(label), "%s addr=%.*s",
                              m_strIdentityLabel.c_str(),
                              static_cast<int>(xtra_params.size()), xtra_params.data());
            } else {
                std::snprintf(label, sizeof(label), "%s addr=0x%02X",
                              m_strIdentityLabel.c_str(), m_u8Addr);
            }
            return commdump_details(CommFamily::I2C, label);
        }

        /**
         * @brief Unified read interface supporting multiple operation modes.
         *
         * @param u32ReadTimeout  Timeout in milliseconds (0 = block indefinitely / infinite timeout).
         * @param buffer          Buffer to read data into.
         * @param options         Read operation configuration.
         * @param xtra_params     Optional 7-bit slave address override for this call
         *                        only (decimal or "0x" hex, e.g. "0x50" or "80").
         *                        An empty string (default) uses the address bound
         *                        by open() (m_u8Addr).
         * @return ReadResult containing status, bytes read, and terminator found flag.
         *
         * @details
         * - ReadMode::Exact:          Reads exactly buffer.size() bytes from the slave.
         * - ReadMode::UntilDelimiter: Reads single bytes until delimiter found; null-terminates.
         * - ReadMode::UntilToken:     Uses KMP algorithm to detect a token sequence; bytes_read = 0.
         */
        ReadResult tout_read(uint32_t u32ReadTimeout,
                             std::span<uint8_t> buffer,
                             const ReadOptions& options,
                             std::string_view xtra_params = {}) const override;

        /**
         * @brief Unified write interface.
         *
         * @param u32WriteTimeout  Timeout in milliseconds (0 = block indefinitely / infinite timeout, currently unused).
         * @param buffer           Data to write to the slave.
         * @param xtra_params      Optional 7-bit slave address override for this call
         *                         only (decimal or "0x" hex, e.g. "0x50" or "80").
         *                         An empty string (default) uses the address bound
         *                         by open() (m_u8Addr).
         * @return WriteResult containing status and bytes written.
         */
        WriteResult tout_write(uint32_t u32WriteTimeout,
                               std::span<const uint8_t> buffer,
                               std::string_view xtra_params = {}) const override;

    private:

        int                m_iHandle  = -1;   /**< File descriptor for the i2c-dev node.         */
        uint8_t            m_u8Addr   = 0x00; /**< Bound 7-bit slave address.                    */
        mutable std::mutex m_mutex;            /**< Protects concurrent access.                   */
        std::string        m_strIdentityLabel; /**< GUI comm-dump display label, see describeConnection(). */

        // -----------------------------------------------------------------------
        // Internal transport primitives
        // -----------------------------------------------------------------------

        /**
         * @brief Read up to buffer.size() bytes from the bound slave.
         * Uses poll(2) for the timeout, then a single ::read(2) call.
         */
        Status timeout_read(uint32_t u32ReadTimeout,
                            std::span<uint8_t> buffer,
                            size_t& szBytesRead) const;

        /**
         * @brief Read bytes one at a time until cDelimiter is found or buffer is full.
         * Null-terminates the result on success (Status::SUCCESS).
         */
        Status timeout_read_until(uint32_t u32ReadTimeout,
                                  std::span<uint8_t> buffer,
                                  uint8_t cDelimiter,
                                  size_t& szBytesRead) const;

        /**
         * @brief Stream-search for a token sequence using the KMP algorithm.
         * Bytes are consumed one at a time via timeout_read().
         */
        Status timeout_wait_for_token(uint32_t u32ReadTimeout,
                                      std::span<const uint8_t> token,
                                      bool useBuffer) const;

        /**
         * @brief Write the entire buffer to the bound slave in one ::write(2) call.
         */
        Status timeout_write(uint32_t u32WriteTimeout,
                             std::span<const uint8_t> buffer,
                             size_t& szBytesWritten) const;

        // -----------------------------------------------------------------------
        // KMP helpers (identical strategy to UART driver)
        // -----------------------------------------------------------------------

        /** @brief Run KMP stream matching over single-byte reads. */
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


#endif // U_KI2C_DRIVER_H
