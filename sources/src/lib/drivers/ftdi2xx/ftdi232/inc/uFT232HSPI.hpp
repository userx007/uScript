#ifndef U_FT232H_SPI_DRIVER_H
#define U_FT232H_SPI_DRIVER_H

#include "FT232HBase.hpp"
#include "ICommDriver.hpp"

#include <cstdint>
#include <span>
#include <vector>

/**
 * @brief FT232H SPI master driver
 *
 * Inherits the MPSSE foundation from FT232HBase and implements ICommDriver's
 * unified read/write interface over SPI using the MPSSE hardware serialiser.
 *
 * The FT232H has a single MPSSE channel — no channel selector is needed.
 *
 * ── Pin assignment (ADBUS) ───────────────────────────────────────────────────
 *
 *   ADBUS0 (TCK/SK) — SCK   : clock output
 *   ADBUS1 (TDI/DO) — MOSI  : master out, slave in
 *   ADBUS2 (TDO/DI) — MISO  : master in,  slave out (input)
 *   ADBUS3 (TMS/CS) — CS    : chip-select (default, configurable via csPin)
 *
 * ADBUS4–7 and ACBUS[7:0] are available for GPIO.
 *
 * ── SPI modes ────────────────────────────────────────────────────────────────
 *
 *   Mode 0 (CPOL=0, CPHA=0): CLK idle LOW,  sample rising,  shift falling
 *   Mode 1 (CPOL=0, CPHA=1): CLK idle LOW,  sample falling, shift rising
 *   Mode 2 (CPOL=1, CPHA=0): CLK idle HIGH, sample falling, shift rising
 *   Mode 3 (CPOL=1, CPHA=1): CLK idle HIGH, sample rising,  shift falling
 *
 * ── Clock formula ────────────────────────────────────────────────────────────
 *
 *   SCK = 60 MHz / ((1 + divisor) × 2)
 *   divisor = (30,000,000 / clockHz) − 1
 *
 *   Examples: 1 MHz → 29,   6 MHz → 4,   30 MHz → 0
 */
class FT232HSPI : public FT232HBase, public ICommDriver
{
    public:

        using Status = ICommDriver::Status;

        // ── SPI configuration ────────────────────────────────────────────────

        /** Standard SPI clock/phase mode */
        enum class SpiMode    : uint8_t { Mode0 = 0, Mode1 = 1, Mode2 = 2, Mode3 = 3 };

        /** Bit transmission order */
        enum class BitOrder   : uint8_t { MsbFirst = 0, LsbFirst = 1 };

        /** Chip-select active polarity */
        enum class CsPolarity : uint8_t { ActiveLow = 0, ActiveHigh = 1 };

        /**
         * @brief Complete SPI bus configuration
         *
         * Defaults produce a safe, widely-compatible 1 MHz Mode-0 bus.
         * No channel field — the FT232H has only one MPSSE interface.
         */
        struct SpiConfig {
            uint32_t   clockHz    = 1000000u;              ///< SCK frequency in Hz (max 30 MHz)
            SpiMode    mode       = SpiMode::Mode0;        ///< Clock polarity/phase
            BitOrder   bitOrder   = BitOrder::MsbFirst;    ///< Transmission order
            uint8_t    csPin      = 0x08u;                 ///< CS pin mask on ADBUS (default ADBUS3)
            CsPolarity csPolarity = CsPolarity::ActiveLow; ///< CS assert level
        };

        // ── Full-duplex result ────────────────────────────────────────────────

        struct TransferResult {
            Status status       = Status::RETVAL_NOT_SET;
            size_t bytes_xfered = 0;
        };

        FT232HSPI() = default;

        explicit FT232HSPI(const SpiConfig& config, uint8_t u8DeviceIndex = 0u)
        {
            this->open(config, u8DeviceIndex);
        }

        ~FT232HSPI() override { close(); }

        /**
         * @brief Open the FT232H and configure MPSSE for SPI
         *
         * @param config        SPI bus parameters
         * @param u8DeviceIndex Physical device index (0 if only one chip)
         */
        Status open(const SpiConfig& config, uint8_t u8DeviceIndex = 0u);

        Status close() override;
        bool is_open() const override { return FT232HBase::is_open(); }

        /**
         * @brief SPI write-only transaction (CS asserted for full transfer)
         */
        WriteResult tout_write(uint32_t u32WriteTimeout,
                               std::span<const uint8_t> buffer) const override;

        /**
         * @brief SPI read-only transaction (dummy 0x00 clocked on MOSI)
         */
        ReadResult  tout_read(uint32_t u32ReadTimeout,
                              std::span<uint8_t> buffer,
                              const ReadOptions& options) const override;

        /**
         * @brief Full-duplex SPI transaction (simultaneous TX+RX)
         *
         * Both buffers must be the same size.
         */
        TransferResult spi_transfer(std::span<const uint8_t> txBuf,
                                    std::span<uint8_t>       rxBuf,
                                    uint32_t u32TimeoutMs = 0u) const;

    private:

        SpiConfig m_config;

        // Resolved MPSSE command bytes (set at open() time from m_config)
        uint8_t m_cmdWrite = 0x11u;
        uint8_t m_cmdRead  = 0x20u;
        uint8_t m_cmdXfer  = 0x31u;

        // ADBUS pin state tracking
        uint8_t m_pinValue = 0x00u; ///< Current ADBUS output value
        uint8_t m_pinDir   = 0x0Bu; ///< ADBUS direction: SCK+MOSI+CS = outputs, MISO = input

        Status configure_mpsse_spi(const SpiConfig& config);
        Status cs_assert()   const;
        Status cs_deassert() const;
        Status apply_pin_state(bool csActive) const;

        Status spi_write_raw(std::span<const uint8_t> data,
                             size_t& bytesWritten) const;
        Status spi_read_raw(std::span<uint8_t> data,
                            size_t& bytesRead,
                            uint32_t timeoutMs) const;
        Status spi_xfer_raw(std::span<const uint8_t> txBuf,
                            std::span<uint8_t>       rxBuf,
                            size_t& bytesXferd,
                            uint32_t timeoutMs) const;
};

#endif // U_FT232H_SPI_DRIVER_H
