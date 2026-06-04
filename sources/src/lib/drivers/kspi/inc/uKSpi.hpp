#ifndef UKSPI_DRIVER_HPP
#define UKSPI_DRIVER_HPP

#include "ICommDriver.hpp"

#include <string>
#include <vector>
#include <span>
#include <mutex>
#include <cstdint>

/**
 * @brief KSPI bus configuration passed to open().
 */
struct SpiConfig
{
    uint8_t  mode         = 0;        /**< KSPI mode: 0–3 (CPOL/CPHA).                          */
    uint8_t  bits_per_word = 8;       /**< Bits per word, typically 8.                          */
    uint32_t speed_hz     = 1000000;  /**< Bus clock frequency in Hz (default: 1 MHz).          */
    bool     lsb_first    = false;    /**< Transmit LSB first when true (MSB first otherwise).  */
};

/**
 * @brief Linux KSPI driver implementing the ICommDriver interface.
 *
 * Wraps /dev/spidevB.C character devices using the Linux spidev kernel interface.
 * The bus parameters (mode, bits-per-word, speed) are configured once at open()
 * time via ioctl(SPI_IOC_WR_*).
 *
 * KSPI is a full-duplex, synchronous bus: every read is accompanied by a
 * simultaneous write (and vice-versa). The driver exposes the ICommDriver
 * half-duplex abstraction on top of that by:
 *   - tout_write() — performs a full-duplex transfer, TX from the caller's
 *                    buffer, RX discarded.
 *   - tout_read()  — performs a full-duplex transfer, TX filled with 0x00,
 *                    RX returned to the caller.
 *
 * There is no hardware "timeout" signal on KSPI; transfers complete as soon
 * as the kernel finishes clocking out bits. The u32*Timeout parameters are
 * therefore used only for an internal poll(2) guard before the ioctl, which
 * provides a ceiling in pathological situations (e.g. driver hang).
 *
 * Read semantics:
 *   - ReadMode::Exact          — KSPI full-duplex transfer of exactly buffer.size()
 *                                bytes; RX returned in buffer.
 *   - ReadMode::UntilDelimiter — repeated single-byte transfers until the
 *                                delimiter byte is received or buffer is full.
 *   - ReadMode::UntilToken     — KMP streaming search over single-byte transfers;
 *                                does not fill the user buffer (bytes_read = 0).
 *
 * Thread safety:
 *   - All public methods are protected by an internal mutex.
 *
 * Configuration:
 *   @see SpiConfig
 */
class KSPI : public ICommDriver
{
    public:

        static constexpr size_t   KSPI_MAX_BUFLENGTH          = 256;  /**< Maximum KSPI buffer length.                  */
        static constexpr uint32_t KSPI_READ_DEFAULT_TIMEOUT   = 5000; /**< Default KSPI read timeout in milliseconds.   */
        static constexpr uint32_t KSPI_WRITE_DEFAULT_TIMEOUT  = 5000; /**< Default KSPI write timeout in milliseconds.  */

        using SpiConfig = ::SpiConfig;

        KSPI() = default;

        /**
         * @brief Construct and immediately open the KSPI device.
         * @param strDevice  Path to the spidev node, e.g. "/dev/spidev0.0".
         * @param config     Bus configuration (mode, speed, bits).
         */
        explicit KSPI(const std::string& strDevice, const SpiConfig& config = SpiConfig{})
        {
            open(strDevice, config);
        }

        virtual ~KSPI()
        {
            close();
        }

        /**
         * @brief Open an KSPI device and apply bus configuration.
         * @param strDevice  Path to the spidev character device.
         * @param config     Bus configuration.
         * @return Status::SUCCESS or an error code.
         */
        Status open(const std::string& strDevice, const SpiConfig& config = SpiConfig{});

        /**
         * @brief Close the KSPI device file descriptor.
         * @return Status::SUCCESS.
         */
        Status close();

        /**
         * @brief Check whether the device is currently open.
         * @return true if the file descriptor is valid.
         */
        bool is_open() const override;

        /**
         * @brief Unified read interface supporting multiple operation modes.
         *
         * @param u32ReadTimeout  Timeout in milliseconds (0 = use default).
         * @param buffer          Buffer to receive data into.
         * @param options         Read operation configuration.
         * @return ReadResult containing status, bytes read, and terminator found flag.
         *
         * @details
         * - ReadMode::Exact:          Full-duplex transfer of buffer.size() bytes; TX = 0x00.
         * - ReadMode::UntilDelimiter: Single-byte transfers until delimiter found; null-terminates.
         * - ReadMode::UntilToken:     KMP token search over single-byte transfers; bytes_read = 0.
         */
        ReadResult tout_read(uint32_t u32ReadTimeout,
                             std::span<uint8_t> buffer,
                             const ReadOptions& options) const override;

        /**
         * @brief Unified write interface.
         *
         * @param u32WriteTimeout  Timeout in milliseconds (0 = use default).
         * @param buffer           Data to transmit; RX during transfer is discarded.
         * @return WriteResult containing status and bytes written.
         */
        WriteResult tout_write(uint32_t u32WriteTimeout,
                               std::span<const uint8_t> buffer) const override;

    private:

        int                m_iHandle = -1;   /**< File descriptor for the spidev node.  */
        SpiConfig          m_config  = {};   /**< Active bus configuration.             */
        mutable std::mutex m_mutex;          /**< Protects concurrent access.           */

        // -----------------------------------------------------------------------
        // Internal transport primitives
        // -----------------------------------------------------------------------

        /**
         * @brief Execute a full-duplex KSPI transfer via SPI_IOC_MESSAGE(1).
         *
         * @param txBuf  Data to transmit (may be nullptr to send 0x00 bytes).
         * @param rxBuf  Buffer for received data (may be nullptr to discard RX).
         * @param length Number of bytes to transfer.
         * @return Status::SUCCESS, Status::READ_ERROR, or Status::WRITE_ERROR.
         */
        Status spi_transfer(const uint8_t* txBuf,
                            uint8_t*       rxBuf,
                            size_t         length) const;

        /**
         * @brief Read exactly buffer.size() bytes (TX = 0x00 padding).
         * Uses poll(2) for the timeout guard before the ioctl.
         */
        Status timeout_read(uint32_t u32ReadTimeout,
                            std::span<uint8_t> buffer,
                            size_t& szBytesRead) const;

        /**
         * @brief Read bytes one at a time until cDelimiter is received or buffer is full.
         * Null-terminates the result on Status::SUCCESS.
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
         * @brief Transmit the full buffer; RX data is discarded.
         */
        Status timeout_write(uint32_t u32WriteTimeout,
                             std::span<const uint8_t> buffer,
                             size_t& szBytesWritten) const;

        // -----------------------------------------------------------------------
        // KMP helpers (identical strategy to UART / I2C drivers)
        // -----------------------------------------------------------------------

        /** @brief Run KMP stream matching over single-byte KSPI reads. */
        Status kmp_stream_match(std::span<const uint8_t> token,
                                const std::vector<int>& viLps,
                                uint32_t u32Timeout,
                                bool bReturnOnTimeout,
                                bool useBuffer) const;

        /** @brief Build the KMP failure-function table for @p pattern. */
        void build_kmp_table(std::span<const uint8_t> pattern,
                             size_t szLength,
                             std::vector<int>& viLps) const;

        /**
         * @brief Apply SpiConfig to the open file descriptor via ioctl.
         * @return Status::SUCCESS or Status::PORT_ACCESS on failure.
         */
        Status setup(const SpiConfig& config) const;
};


#endif // UKSPI_DRIVER_HPP
