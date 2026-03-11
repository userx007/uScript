#ifndef U_FT245_SYNC_DRIVER_H
#define U_FT245_SYNC_DRIVER_H

#include "FT245Base.hpp"
#include "ICommDriver.hpp"

#include <cstdint>
#include <span>

/**
 * @brief FT245 bulk FIFO driver (async and sync modes)
 *
 * Inherits the FIFO foundation from FT245Base and implements ICommDriver's
 * unified read/write interface over the FT245 parallel FIFO.
 *
 * This is the primary data-path driver for the FT245 family.  It provides
 * high-throughput byte-stream transfers to/from the chip's internal FIFO
 * via USB bulk transfers, with no serial protocol overhead.
 *
 * ── Transfer modes ───────────────────────────────────────────────────────────
 *
 *   FifoMode::Async (BITMODE_RESET — FT245BM and FT245R):
 *     The device FIFO is driven asynchronously.  Suitable for general-purpose
 *     byte streaming.  Open with fifoMode = FifoMode::Async.
 *
 *   FifoMode::Sync (BITMODE_SYNC_FIFO — FT245BM only):
 *     All transfers are clocked by the USB frame clock, giving deterministic
 *     latency and the highest possible throughput (up to 1 MB/s).
 *     Open with fifoMode = FifoMode::Sync.
 *     Passing FifoMode::Sync for a FT245R returns INVALID_PARAM.
 *
 * ── ICommDriver mapping ───────────────────────────────────────────────────────
 *
 *   tout_write → blocking write into TX FIFO, up to u32WriteTimeout ms
 *   tout_read  → blocking read  from RX FIFO, up to u32ReadTimeout  ms
 *                ReadMode::Exact / UntilDelimiter / UntilToken all supported
 */
class FT245Sync : public FT245Base, public ICommDriver
{
    public:

        using Status = ICommDriver::Status;

        // ── Device configuration ──────────────────────────────────────────────
        struct SyncConfig {
            Variant  variant    = Variant::FT245BM;  ///< FT245BM or FT245R
            FifoMode fifoMode   = FifoMode::Async;   ///< Async (both) or Sync (BM only)
        };

        FT245Sync() = default;

        /**
         * @brief Construct and immediately open the device
         * @param config        FIFO configuration
         * @param u8DeviceIndex Zero-based index when multiple chips are connected
         */
        explicit FT245Sync(const SyncConfig& config, uint8_t u8DeviceIndex = 0u)
        {
            this->open(config, u8DeviceIndex);
        }

        ~FT245Sync() override { close(); }

        /**
         * @brief Open the FT245 and configure the FIFO mode
         *
         * @param config        FIFO variant and mode
         * @param u8DeviceIndex Physical device index (0 = first chip found)
         */
        Status open(const SyncConfig& config, uint8_t u8DeviceIndex = 0u);

        /** @copydoc FT245Base::close — purges FIFO before closing */
        Status close() override;

        bool is_open() const override { return FT245Base::is_open(); }

        /**
         * @brief Blocking write into TX FIFO (implements ICommDriver)
         *
         * Writes all bytes in buffer into the device TX FIFO within
         * u32WriteTimeout milliseconds.
         *
         * @param u32WriteTimeout ms (0 = FT245_WRITE_DEFAULT_TIMEOUT)
         */
        WriteResult tout_write(uint32_t u32WriteTimeout,
                               std::span<const uint8_t> buffer) const override;

        /**
         * @brief Blocking read from RX FIFO (implements ICommDriver)
         *
         * Supports ReadMode::Exact, UntilDelimiter, and UntilToken.
         *
         * @param u32ReadTimeout ms (0 = FT245_READ_DEFAULT_TIMEOUT)
         */
        ReadResult tout_read(uint32_t u32ReadTimeout,
                             std::span<uint8_t> buffer,
                             const ReadOptions& options) const override;

        /**
         * @brief Purge RX and TX FIFOs without closing the device
         *
         * Discards all pending bytes in both directions.  Safe to call
         * at any time while the device is open.
         */
        Status flush() const { return fifo_purge(); }
};

#endif // U_FT245_SYNC_DRIVER_H
