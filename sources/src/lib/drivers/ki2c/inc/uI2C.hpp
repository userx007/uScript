#ifndef U_I2C_DRIVER_H
#define U_I2C_DRIVER_H

#include "ICommDriver.hpp"

#include <string>
#include <vector>
#include <span>
#include <mutex>
#include <cstdint>


/**
 * @brief Linux I2C driver implementing the ICommDriver interface.
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
 * Timeout handling:
 *   - Implemented via poll(2) on the file descriptor before every read attempt.
 *   - A timeout of 0 selects I2C_READ_DEFAULT_TIMEOUT.
 *
 * Thread safety:
 *   - All public methods are protected by an internal mutex.
 */
class I2C : public ICommDriver
{
    public:

        static constexpr size_t   I2C_MAX_BUFLENGTH          = 256;  /**< Maximum I2C buffer length.                    */
        static constexpr uint32_t I2C_READ_DEFAULT_TIMEOUT   = 5000; /**< Default I2C read timeout in milliseconds.     */
        static constexpr uint32_t I2C_WRITE_DEFAULT_TIMEOUT  = 5000; /**< Default I2C write timeout in milliseconds.    */

        I2C() = default;

        /**
         * @brief Construct and immediately open the bus/device.
         * @param strDevice  Path to the i2c-dev node, e.g. "/dev/i2c-1".
         * @param u8Address  7-bit slave address (e.g. 0x48).
         */
        explicit I2C(const std::string& strDevice, uint8_t u8Address)
        {
            open(strDevice, u8Address);
        }

        virtual ~I2C()
        {
            close();
        }

        /**
         * @brief Open an I2C bus and bind it to a slave address.
         * @param strDevice  Path to the i2c-dev character device.
         * @param u8Address  7-bit slave address.
         * @return Status::SUCCESS or an error code.
         */
        Status open(const std::string& strDevice, uint8_t u8Address);

        /**
         * @brief Close the I2C bus file descriptor.
         * @return Status::SUCCESS.
         */
        Status close();

        /**
         * @brief Check whether the bus is currently open.
         * @return true if the file descriptor is valid.
         */
        bool is_open() const override;

        /**
         * @brief Unified read interface supporting multiple operation modes.
         *
         * @param u32ReadTimeout  Timeout in milliseconds (0 = use default).
         * @param buffer          Buffer to read data into.
         * @param options         Read operation configuration.
         * @return ReadResult containing status, bytes read, and terminator found flag.
         *
         * @details
         * - ReadMode::Exact:          Reads exactly buffer.size() bytes from the slave.
         * - ReadMode::UntilDelimiter: Reads single bytes until delimiter found; null-terminates.
         * - ReadMode::UntilToken:     Uses KMP algorithm to detect a token sequence; bytes_read = 0.
         */
        ReadResult tout_read(uint32_t u32ReadTimeout,
                             std::span<uint8_t> buffer,
                             const ReadOptions& options) const override;

        /**
         * @brief Unified write interface.
         *
         * @param u32WriteTimeout  Timeout in milliseconds (0 = use default, currently unused).
         * @param buffer           Data to write to the slave.
         * @return WriteResult containing status and bytes written.
         */
        WriteResult tout_write(uint32_t u32WriteTimeout,
                               std::span<const uint8_t> buffer) const override;

    private:

        int                m_iHandle  = -1;   /**< File descriptor for the i2c-dev node.         */
        uint8_t            m_u8Addr   = 0x00; /**< Bound 7-bit slave address.                    */
        mutable std::mutex m_mutex;            /**< Protects concurrent access.                   */

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


#endif // U_I2C_DRIVER_H
