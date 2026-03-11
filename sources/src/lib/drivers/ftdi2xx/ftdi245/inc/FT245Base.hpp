#ifndef FT245_BASE_HPP
#define FT245_BASE_HPP

#include "ICommDriver.hpp"

#include <cstdint>
#include <cstddef>
#include <vector>

/**
 * @brief Shared FIFO foundation for all FT245 driver classes
 *
 * Supports the FT245BM/RL (full-speed, async FIFO) and the FT245R
 * (full-speed, async FIFO with integrated oscillator) variants.
 * Select the correct variant via the Variant enum passed to open_device().
 *
 * Unlike the MPSSE-based FT2232 family, the FT245 exposes an 8-bit parallel
 * FIFO interface.  Data is transferred using D2XX / libftdi in either
 * synchronous FIFO mode (FT245BM/RL in sync mode) or asynchronous FIFO mode
 * (FT245R default).  There is no clock-divisor or serial-protocol engine —
 * the host simply reads/writes bulk USB packets into the device FIFO.
 *
 * Platform-specific implementations live in uFT245Linux.cpp / uFT245Windows.cpp.
 * The handle is stored as void* to keep SDK headers (libftdi1 / FTD2XX) out of
 * this header — each platform .cpp casts it to the appropriate type.
 *
 * Not intended to be instantiated directly — use FT245Sync (bulk FIFO) or
 * FT245GPIO (bit-bang GPIO).
 *
 * ── Chip variant differences ─────────────────────────────────────────────────
 *
 *   Variant::FT245BM  (PID 0x6001 — also covers FT245RL)
 *     • Async or sync parallel FIFO interface
 *     • USB Full-Speed (12 Mbps)
 *     • Single channel (no channel selection)
 *     • Up to 1 MB/s transfer rate in sync mode
 *
 *   Variant::FT245R   (PID 0x6001 — integrated oscillator variant)
 *     • Async parallel FIFO, same USB PID as FT245BM
 *     • USB Full-Speed (12 Mbps)
 *     • Built-in 6 MHz oscillator; no external clock required
 *     • Bit-bang mode supported via BITMODE_BITBANG
 *
 * ── Transfer modes ───────────────────────────────────────────────────────────
 *
 *   Async FIFO (default / FT245R):
 *     Data written to the device FIFO is transmitted to USB when the device
 *     asserts TXE# (transmit FIFO not empty).  RXF# indicates received data.
 *     Driven entirely by D2XX/libftdi bulk transfers.
 *
 *   Sync FIFO (FT245BM/RL in BITMODE_SYNC_FIFO):
 *     All reads and writes are clocked by the USB frame clock.  Requires
 *     BITMODE_SYNC_FIFO (0x40) to be set before use.
 *
 * Reference: FTDI DS_FT245BM — FT245BM USB FIFO IC Datasheet
 *            FTDI DS_FT245R  — FT245R USB FIFO IC Datasheet
 *            FTDI AN_232R-01 — Bit Bang Modes for the FT232R and FT245R
 */
class FT245Base
{
    public:

        using Status = ICommDriver::Status;

        // ── Device identity ──────────────────────────────────────────────────
        static constexpr uint16_t FT245_VID    = 0x0403u; ///< FTDI VID (all variants)
        static constexpr uint16_t FT245BM_PID  = 0x6001u; ///< FT245BM/RL PID
        static constexpr uint16_t FT245R_PID   = 0x6001u; ///< FT245R PID (same as BM)

        // ── Timeouts ─────────────────────────────────────────────────────────
        static constexpr uint32_t FT245_READ_DEFAULT_TIMEOUT  = 5000u; ///< ms
        static constexpr uint32_t FT245_WRITE_DEFAULT_TIMEOUT = 5000u; ///< ms

        // ── Chip variant ─────────────────────────────────────────────────────
        /**
         * @brief FT245 silicon variant
         *
         * Both FT245BM and FT245R share the same USB PID (0x6001), but differ
         * in supported bit-modes and internal oscillator presence.
         */
        enum class Variant : uint8_t {
            FT245BM = 0, ///< FT245BM/RL — async/sync FIFO, external oscillator
            FT245R  = 1, ///< FT245R     — async FIFO, integrated 6 MHz oscillator
        };

        // ── Transfer mode ─────────────────────────────────────────────────────
        /**
         * @brief FIFO transfer mode
         *
         * FT245BM supports both async and sync.
         * FT245R supports async only; passing Sync returns INVALID_PARAM.
         */
        enum class FifoMode : uint8_t {
            Async = 0, ///< Asynchronous FIFO (BITMODE_RESET, default)
            Sync  = 1, ///< Synchronous FIFO  (BITMODE_SYNC_FIFO, FT245BM only)
        };

        FT245Base() = default;
        virtual ~FT245Base();

        // Non-copyable
        FT245Base(const FT245Base&)            = delete;
        FT245Base& operator=(const FT245Base&) = delete;

        /** True if the device handle is open and ready */
        bool is_open() const;

        /**
         * @brief Close the device handle
         *
         * Safe to call more than once. Subclasses that need pre-close cleanup
         * should override this, perform their cleanup, then call
         * FT245Base::close().
         */
        virtual Status close();

        /** Return the active variant (valid after a successful open_device()) */
        Variant variant() const { return m_variant; }

        /** Return the active FIFO mode (valid after a successful open_device()) */
        FifoMode fifo_mode() const { return m_fifoMode; }

    protected:

        // ── Bit-mode constants (FTDI AN_232R-01, FTD2XX Programmer's Guide) ──
        //
        // Passed as the ucMode argument to FT_SetBitMode / ftdi_set_bitmode.
        //
        static constexpr uint8_t BITMODE_RESET     = 0x00u; ///< Reset / async FIFO (default)
        static constexpr uint8_t BITMODE_BITBANG   = 0x01u; ///< Async bit-bang (8-bit GPIO)
        static constexpr uint8_t BITMODE_SYNC_FIFO = 0x40u; ///< Sync FIFO (FT245BM only)

        // ── Variant and device handle ─────────────────────────────────────────
        Variant  m_variant  = Variant::FT245BM;   ///< Set by open_device()
        FifoMode m_fifoMode = FifoMode::Async;    ///< Set by open_device()

        /**
         * Stored as void* to avoid leaking libftdi1 / FTD2XX headers into
         * this header. Platform .cpp files cast to the appropriate type:
         *   Linux   : struct ftdi_context*
         *   Windows : FT_HANDLE  (which is itself a void*)
         *
         * nullptr means the device is not open.
         */
        void* m_hDevice = nullptr;

        // ── Device open ───────────────────────────────────────────────────────

        /**
         * @brief Enumerate FT245 devices and open the handle
         *
         * @param variant       FT245BM or FT245R — selects PID and allowed modes
         * @param fifoMode      Async (all variants) or Sync (FT245BM only)
         * @param u8DeviceIndex Zero-based index among connected chips of this variant
         */
        Status open_device(Variant variant, FifoMode fifoMode, uint8_t u8DeviceIndex);

        // ── FIFO transport primitives — implemented in platform .cpp files ──

        /**
         * @brief Write raw bytes into the device TX FIFO
         *
         * Performs a synchronous USB bulk write.
         * @param buf     Pointer to data to transmit
         * @param len     Number of bytes to write
         */
        Status fifo_write(const uint8_t* buf, size_t len) const;

        /**
         * @brief Read bytes from the device RX FIFO with a timeout
         *
         * Polls until the requested number of bytes arrives or the timeout
         * expires.
         *
         * @param buf        Destination buffer
         * @param len        Number of bytes to read
         * @param timeoutMs  ms before returning READ_TIMEOUT
         * @param bytesRead  Actual bytes received
         */
        Status fifo_read(uint8_t* buf, size_t len,
                         uint32_t timeoutMs, size_t& bytesRead) const;

        /** Discard any pending bytes in the device RX/TX FIFOs */
        Status fifo_purge() const;
};

#endif // FT245_BASE_HPP
