#ifndef U_FT2232_SPI_DRIVER_H
#define U_FT2232_SPI_DRIVER_H

#include "FT2232Base.hpp"
#include "ICommDriver.hpp"

#include <cstdint>
#include <span>
#include <vector>

/**
 * @brief FT2232 SPI master driver
 *
 * Inherits the MPSSE foundation from FT2232Base and implements ICommDriver's
 * unified read/write interface over SPI using the MPSSE native serial shift
 * commands (AN_108 §3.3).
 *
 * ── Pin assignment (ADBUS) ───────────────────────────────────────────────────
 *
 *   ADBUS0 (TCK/SK) — SCK  : clock  output
 *   ADBUS1 (TDI/DO) — MOSI : master out
 *   ADBUS2 (TDO/DI) — MISO : master in  (always input)
 *   ADBUS3 (TMS/CS) — CS   : chip-select (default; configurable via SpiConfig)
 *
 * ── SPI modes ────────────────────────────────────────────────────────────────
 *
 *   Mode 0 (CPOL=0, CPHA=0): idle LOW,  sample rising,  shift falling
 *   Mode 1 (CPOL=0, CPHA=1): idle LOW,  sample falling, shift rising
 *   Mode 2 (CPOL=1, CPHA=0): idle HIGH, sample falling, shift rising
 *   Mode 3 (CPOL=1, CPHA=1): idle HIGH, sample rising,  shift falling
 *
 * ── Clock limits ─────────────────────────────────────────────────────────────
 *
 *   Variant::FT2232H → base 60 MHz → max SCK 30 MHz  (divisor 0)
 *   Variant::FT2232D → base  6 MHz → max SCK  3 MHz  (divisor 0)
 *
 * ── ICommDriver mapping ──────────────────────────────────────────────────────
 *
 *   tout_write  → CS assert + write-only  + CS deassert
 *   tout_read   → CS assert + read-only   + CS deassert
 *   spi_transfer→ CS assert + full-duplex + CS deassert  (extra method)
 */
class FT2232SPI : public FT2232Base, public ICommDriver
{
    public:

        using Status = ICommDriver::Status;

        enum class SpiMode    : uint8_t { Mode0 = 0, Mode1 = 1, Mode2 = 2, Mode3 = 3 };
        enum class BitOrder   : uint8_t { MsbFirst = 0, LsbFirst = 1 };
        enum class CsPolarity : uint8_t { ActiveLow = 0, ActiveHigh = 1 };

        struct SpiConfig {
            uint32_t   clockHz    = 1000000u;
            SpiMode    mode       = SpiMode::Mode0;
            BitOrder   bitOrder   = BitOrder::MsbFirst;
            uint8_t    csPin      = 0x08u;                  ///< ADBUS3 by default
            CsPolarity csPolarity = CsPolarity::ActiveLow;
            Variant    variant    = Variant::FT2232H;
            Channel    channel    = Channel::A;
        };

        struct TransferResult {
            Status status       = Status::RETVAL_NOT_SET;
            size_t bytes_xfered = 0;
        };

        FT2232SPI() = default;

        explicit FT2232SPI(const SpiConfig& config, uint8_t u8DeviceIndex = 0u)
        {
            this->open(config, u8DeviceIndex);
        }

        ~FT2232SPI() override { close(); }

        Status open(const SpiConfig& config, uint8_t u8DeviceIndex = 0u);

        /** @copydoc FT2232Base::close — deasserts CS before closing */
        Status close() override;

        bool is_open() const override { return FT2232Base::is_open(); }

        WriteResult tout_write(uint32_t u32WriteTimeout,
                               std::span<const uint8_t> buffer) const override;

        ReadResult  tout_read(uint32_t u32ReadTimeout,
                              std::span<uint8_t> buffer,
                              const ReadOptions& options) const override;

        /**
         * @brief Full-duplex SPI: simultaneous TX and RX
         * @param txBuf / rxBuf must be the same size
         * @param u32TimeoutMs  0 = FT2232_READ_DEFAULT_TIMEOUT
         */
        TransferResult spi_transfer(std::span<const uint8_t> txBuf,
                                    std::span<uint8_t>       rxBuf,
                                    uint32_t u32TimeoutMs = 0u) const;

    private:

        SpiConfig m_config;
        uint8_t   m_cmdWrite = 0x11u;
        uint8_t   m_cmdRead  = 0x20u;
        uint8_t   m_cmdXfer  = 0x31u;
        uint8_t   m_pinValue = 0x00u;
        uint8_t   m_pinDir   = 0x0Bu;

        Status configure_mpsse_spi(const SpiConfig& config);
        Status cs_assert()                          const;
        Status cs_deassert()                        const;
        Status apply_pin_state(bool csActive)       const;

        Status spi_write_raw(std::span<const uint8_t> data,
                             size_t& bytesWritten)            const;

        Status spi_read_raw (std::span<uint8_t> data,
                             size_t& bytesRead, uint32_t timeoutMs) const;

        Status spi_xfer_raw (std::span<const uint8_t> txBuf,
                             std::span<uint8_t> rxBuf,
                             size_t& bytesXferd, uint32_t timeoutMs) const;
};

#endif // U_FT2232_SPI_DRIVER_H
