#ifndef U_CH347_SPI_DRIVER_H
#define U_CH347_SPI_DRIVER_H

/**
 * @file uCH347Spi.hpp
 * @brief CH347 SPI driver – wraps CH347SPI_* C API behind the ICommDriver interface.
 *
 * Buffer layout convention
 * ========================
 * tout_write  : raw payload bytes (MOSI).  CS is asserted/de-asserted automatically
 *               unless the driver was opened with auto-CS disabled.
 * tout_read   : ReadMode::Exact performs a full-duplex WriteRead; the caller must
 *               pre-fill buffer[0..n-1] with MOSI data.  On return the same buffer
 *               holds the MISO data.
 *               ReadMode::UntilDelimiter and ReadMode::UntilToken are not meaningful
 *               for SPI and will return Status::NotSupported.
 *
 * Chip-select selection
 * =====================
 * Pass the desired CS in SpiReadOptions (derived from ReadOptions) via the
 * overloaded tout_read / tout_write that accept SPIXferOptions, or configure
 * a default CS at open() time.
 */

#include "ICommDriver.hpp"
#include "ch347_lib.h"

#include <string>
#include <span>

// ---------------------------------------------------------------------------
// SPI-specific transfer options (extend base ReadOptions with CS info)
// ---------------------------------------------------------------------------

/** Which hardware chip-select line to assert for this transfer. */
enum class SpiCS : uint8_t {
    CS1      = 0x80, /**< CS1 line  (BIT7 set in iChipSelect) */
    CS2      = 0x84, /**< CS2 line  (BIT7+BIT2 set) – chip-dependent */
    IgnoreCS = 0x00, /**< Do not touch CS (caller manages it manually) */
};

/**
 * @brief Per-transfer SPI options.
 *
 * Embed inside the generic ReadOptions::token field (reinterpreted as a
 * single-byte span) OR use the extended tout_xfer() helper directly.
 */
struct SpiXferOptions {
    SpiCS    chipSelect  = SpiCS::CS1; /**< CS line to use */
    bool     ignoreCS    = false;      /**< Pass true to skip CS toggling */
    int      writeStep   = 512;        /**< Bytes per USB packet for writes */
};

// ---------------------------------------------------------------------------

class CH347SPI : public ICommDriver
{
public:
    // -----------------------------------------------------------------------
    // Constants
    // -----------------------------------------------------------------------
    static constexpr uint32_t SPI_READ_DEFAULT_TIMEOUT  = 5000; /**< ms */
    static constexpr uint32_t SPI_WRITE_DEFAULT_TIMEOUT = 5000; /**< ms */

    // -----------------------------------------------------------------------
    // Construction / destruction
    // -----------------------------------------------------------------------

    CH347SPI() = default;

    /**
     * @brief Construct and immediately open a CH347 SPI device.
     *
     * @param strDevice  Device path, e.g. "/dev/ch34xpis0"
     * @param cfg        SPI bus configuration (mode, clock, byte-order …)
     * @param xferOpts   Default per-transfer chip-select options
     */
    explicit CH347SPI(const std::string& strDevice,
                      const mSpiCfgS&    cfg,
                      const SpiXferOptions& xferOpts = {})
        : m_iHandle(-1), m_xferOpts(xferOpts)
    {
        open(strDevice, cfg);
    }

    virtual ~CH347SPI() { close(); }

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    Status open(const std::string& strDevice, const mSpiCfgS& cfg);
    Status close();
    bool   is_open() const override;

    // -----------------------------------------------------------------------
    // Configuration helpers (callable after open)
    // -----------------------------------------------------------------------

    /** Change SPI clock frequency (Hz).  Valid range: 218 750 – 60 000 000. */
    Status set_frequency(uint32_t iHz);

    /** Switch between 8-bit (0) and 16-bit (1) data frames. */
    Status set_data_bits(uint8_t iDataBits);

    /** Enable or disable automatic CS management on WriteRead calls. */
    Status set_auto_cs(bool disable);

    /** Manually assert (iStatus=1) or de-assert (iStatus=0) the CS line. */
    Status change_cs(uint8_t iStatus);

    /** Read back the current hardware SPI configuration. */
    Status get_config(mSpiCfgS& cfg) const;

    // -----------------------------------------------------------------------
    // ICommDriver interface
    // -----------------------------------------------------------------------

    /**
     * @brief Full-duplex SPI transfer (WriteRead).
     *
     * @param u32ReadTimeout  Ignored for SPI (USB bulk transactions are
     *                        synchronous); kept for interface parity.
     * @param buffer          In:  MOSI bytes to clock out  (buffer.size() bytes)
     *                        Out: MISO bytes clocked in    (same buffer)
     * @param options         ReadMode::Exact required.
     *                        options.token (if non-empty and size()==1) is
     *                        reinterpreted as the CS selector byte:
     *                          bit7  = 1 → use CS line; value = iChipSelect arg.
     *                        Leave token empty to use the default CS configured
     *                        at open() time.
     * @return ReadResult  { status, bytesRead == buffer.size(), false }
     *
     * @note ReadMode::UntilDelimiter and ReadMode::UntilToken return
     *       { Status::NotSupported, 0, false }.
     */
    ReadResult tout_read(uint32_t u32ReadTimeout,
                         std::span<uint8_t>    buffer,
                         const ReadOptions&    options) const override;

    /**
     * @brief Write-only SPI transfer (MOSI only, MISO discarded).
     *
     * @param u32WriteTimeout Ignored for SPI; kept for interface parity.
     * @param buffer          Bytes to clock out on MOSI.
     * @return WriteResult { status, bytesWritten }
     */
    WriteResult tout_write(uint32_t u32WriteTimeout,
                           std::span<const uint8_t> buffer) const override;

    // -----------------------------------------------------------------------
    // Extended helpers (SPI-specific, not part of ICommDriver)
    // -----------------------------------------------------------------------

    /**
     * @brief Full-duplex transfer with explicit per-call options.
     *
     * @param buffer    In: MOSI data / Out: MISO data (same buffer, same length)
     * @param opts      Per-transfer chip-select and packet-size options
     * @return ReadResult { status, bytesXfered, false }
     */
    ReadResult tout_xfer(std::span<uint8_t> buffer,
                         const SpiXferOptions& opts) const;

    /**
     * @brief Write-only transfer with explicit per-call options.
     */
    WriteResult tout_write_ex(std::span<const uint8_t> buffer,
                              const SpiXferOptions& opts) const;

private:
    int           m_iHandle  = -1;
    SpiXferOptions m_xferOpts{};   /**< Default transfer options */

    /** Resolve effective CS value for CH347SPI_* calls. */
    std::pair<bool, uint8_t> resolve_cs(const SpiXferOptions& opts) const;
};

#endif // U_CH347_SPI_DRIVER_H
