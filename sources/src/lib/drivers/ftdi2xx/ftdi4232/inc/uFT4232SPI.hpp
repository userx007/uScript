#ifndef U_FT4232_SPI_DRIVER_H
#define U_FT4232_SPI_DRIVER_H

#include "FT4232Base.hpp"
#include "ICommDriver.hpp"

#include <cstdint>
#include <span>
#include <vector>

/**
 * @brief FT4232H SPI master driver
 *
 * Inherits the MPSSE foundation from FT4232Base and implements ICommDriver's
 * unified read/write interface over SPI using the MPSSE native serial shift
 * commands (AN_108 §3.3).
 *
 * Unlike the I²C bit-bang driver, SPI can use the MPSSE hardware serialiser
 * which handles entire byte blocks in one USB transfer — no per-bit overhead.
 * Transfers of any length (up to 65536 bytes per command) are supported.
 *
 * ── Pin assignment on the selected MPSSE channel (ADBUS) ────────────────────
 *
 *   ADBUS0 (TCK/SK) — SCK   : clock output, always driven
 *   ADBUS1 (TDI/DO) — MOSI  : master out, slave in
 *   ADBUS2 (TDO/DI) — MISO  : master in,  slave out (always input)
 *   ADBUS3 (TMS/CS) — CS    : chip-select (default, configurable)
 *
 * ADBUS4–7 and all ACBUS pins are available as general-purpose I/O.
 *
 * ── SPI mode support ────────────────────────────────────────────────────────
 *
 *   Mode 0 (CPOL=0, CPHA=0): CLK idle LOW,  sample rising,  shift falling
 *   Mode 1 (CPOL=0, CPHA=1): CLK idle LOW,  sample falling, shift rising
 *   Mode 2 (CPOL=1, CPHA=0): CLK idle HIGH, sample falling, shift rising
 *   Mode 3 (CPOL=1, CPHA=1): CLK idle HIGH, sample rising,  shift falling
 *
 * CPOL is applied by setting the CLK pin level between transfers via
 * SET_BITS_LOW. CPHA determines which MPSSE shift command is used.
 *
 * ── ICommDriver mapping ─────────────────────────────────────────────────────
 *
 *   tout_write → CS assert + SPI write  + CS deassert
 *   tout_read  → CS assert + SPI read   + CS deassert (dummy 0x00 bytes sent)
 *
 * For full-duplex (simultaneous TX+RX), use the additional spi_transfer()
 * method which lies outside the ICommDriver contract.
 *
 * ── Clock formula (no 3-phase clocking) ─────────────────────────────────────
 *
 *   SCK = 60 MHz / ((1 + divisor) × 2)
 *   divisor = (30,000,000 / clockHz) − 1
 *
 *   Examples:  1 MHz → divisor = 29
 *              6 MHz → divisor = 4
 *             30 MHz → divisor = 0  (maximum)
 *
 * @note Only channels A and B of the FT4232H have MPSSE.
 */
class FT4232SPI : public FT4232Base, public ICommDriver
{
    public:

        // Resolve ambiguity: both FT4232Base and ICommDriver introduce 'Status'
        using Status = ICommDriver::Status;

        // ── SPI configuration ────────────────────────────────────────────────

        /** Standard SPI clock/phase mode */
        enum class SpiMode : uint8_t { Mode0 = 0, Mode1 = 1, Mode2 = 2, Mode3 = 3 };

        /** Bit transmission order */
        enum class BitOrder : uint8_t { MsbFirst = 0, LsbFirst = 1 };

        /** Chip-select active polarity */
        enum class CsPolarity : uint8_t { ActiveLow = 0, ActiveHigh = 1 };

        /**
         * @brief Complete SPI bus configuration
         *
         * Pass this to open() to set all bus parameters in one call.
         * Defaults produce a safe, widely-compatible 1 MHz Mode-0 bus.
         */
        struct SpiConfig {
            uint32_t   clockHz    = 1000000u;           ///< SCK frequency in Hz
            SpiMode    mode       = SpiMode::Mode0;     ///< Clock polarity/phase
            BitOrder   bitOrder   = BitOrder::MsbFirst; ///< Transmission order
            uint8_t    csPin      = 0x08u;              ///< CS pin mask on ADBUS (default ADBUS3)
            CsPolarity csPolarity = CsPolarity::ActiveLow; ///< CS assert level
            Channel    channel    = Channel::A;         ///< MPSSE channel
        };

        // ── Result type for spi_transfer ─────────────────────────────────────

        struct TransferResult {
            Status status       = Status::RETVAL_NOT_SET;
            size_t bytes_xfered = 0; ///< Bytes successfully exchanged
        };

        FT4232SPI() = default;

        /**
         * @brief Construct and immediately open the device
         * @param config        Full SPI bus configuration
         * @param u8DeviceIndex Zero-based index when multiple FT4232H chips are connected
         */
        explicit FT4232SPI(const SpiConfig& config, uint8_t u8DeviceIndex = 0u)
        {
            this->open(config, u8DeviceIndex);
        }

        ~FT4232SPI() override { close(); }

        /**
         * @brief Open the FT4232H and configure MPSSE for SPI
         *
         * @param config        SPI bus parameters
         * @param u8DeviceIndex Physical device index
         */
        Status open(const SpiConfig& config, uint8_t u8DeviceIndex = 0u);

        /** @copydoc FT4232Base::close — deasserts CS before closing */
        Status close() override;

        bool is_open() const override { return FT4232Base::is_open(); }

        /**
         * @brief SPI write-only transaction
         *
         * CS assert → shift out buffer.size() bytes → CS deassert.
         * MISO is ignored. No payload size limit.
         *
         * @param u32WriteTimeout ms (0 = FT4232_WRITE_DEFAULT_TIMEOUT)
         */
        WriteResult tout_write(uint32_t u32WriteTimeout,
                               std::span<const uint8_t> buffer) const override;

        /**
         * @brief SPI read-only transaction
         *
         * CS assert → shift in buffer.size() bytes (dummy 0x00 clocked out on MOSI) → CS deassert.
         *
         * Modes:
         *   Exact          → read exactly buffer.size() bytes
         *   UntilDelimiter → read byte-by-byte until delimiter found
         *   UntilToken     → read byte-by-byte with KMP token matching
         *
         * @param u32ReadTimeout ms (0 = FT4232_READ_DEFAULT_TIMEOUT)
         */
        ReadResult  tout_read(uint32_t u32ReadTimeout,
                              std::span<uint8_t> buffer,
                              const ReadOptions& options) const override;

        /**
         * @brief Full-duplex SPI transaction
         *
         * CS assert → simultaneously shift txBuf out on MOSI and capture rxBuf
         * from MISO → CS deassert. Both buffers must be the same size.
         *
         * @param txBuf          Data to transmit
         * @param rxBuf          Buffer to receive into (same size as txBuf)
         * @param u32TimeoutMs   Timeout in ms (0 = FT4232_READ_DEFAULT_TIMEOUT)
         * @return TransferResult with status and bytes exchanged
         */
        TransferResult spi_transfer(std::span<const uint8_t> txBuf,
                                    std::span<uint8_t>       rxBuf,
                                    uint32_t u32TimeoutMs = 0u) const;

    private:

        // ── Stored configuration ─────────────────────────────────────────────
        SpiConfig m_config; ///< Full bus config saved for CS pin / polarity / mode

        // ── Derived MPSSE command bytes (resolved from m_config at open()) ───
        uint8_t m_cmdWrite = 0x11u; ///< MPSSE write command for current SPI mode
        uint8_t m_cmdRead  = 0x20u; ///< MPSSE read  command for current SPI mode
        uint8_t m_cmdXfer  = 0x31u; ///< MPSSE full-duplex command for current SPI mode

        // ── ADBUS pin state tracking ─────────────────────────────────────────
        uint8_t m_pinValue = 0x00u; ///< Current ADBUS output value (CLK idle + CS idle)
        uint8_t m_pinDir   = 0x0Bu; ///< ADBUS direction: SCK+MOSI+CS = outputs, MISO = input

        // ── Configuration and helpers (uFT4232SPICommon.cpp) ─────────────────

        /** Push MPSSE init sequence and resolve command bytes from config */
        Status configure_mpsse_spi(const SpiConfig& config);

        /** Assert CS (drive to active level) */
        Status cs_assert() const;

        /** Deassert CS (drive to idle level) */
        Status cs_deassert() const;

        /**
         * @brief Apply a SET_BITS_LOW command with current pin state
         * @param csActive  true = CS at active level, false = CS at idle level
         */
        Status apply_pin_state(bool csActive) const;

        /**
         * @brief Core write: build and send MPSSE shift-out command
         *
         * Does NOT manage CS — caller is responsible.
         * @param data         Bytes to transmit
         * @param bytesWritten Accumulates successfully written bytes
         */
        Status spi_write_raw(std::span<const uint8_t> data,
                             size_t& bytesWritten) const;

        /**
         * @brief Core read: send MPSSE shift-in command and fetch response
         *
         * Does NOT manage CS — caller is responsible.
         * @param data      Buffer to fill
         * @param bytesRead Bytes actually received
         * @param timeoutMs Timeout for mpsse_read
         */
        Status spi_read_raw(std::span<uint8_t> data,
                            size_t& bytesRead,
                            uint32_t timeoutMs) const;

        /**
         * @brief Core full-duplex: simultaneous TX+RX
         *
         * Does NOT manage CS — caller is responsible.
         * @param txBuf      Data to transmit (size must equal rxBuf.size())
         * @param rxBuf      Buffer to fill
         * @param bytesXferd Bytes successfully exchanged
         * @param timeoutMs  Timeout for mpsse_read
         */
        Status spi_xfer_raw(std::span<const uint8_t> txBuf,
                            std::span<uint8_t>       rxBuf,
                            size_t& bytesXferd,
                            uint32_t timeoutMs) const;
};

#endif // U_FT4232_SPI_DRIVER_H
