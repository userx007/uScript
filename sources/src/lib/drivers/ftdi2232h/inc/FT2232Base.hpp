#ifndef FT2232_BASE_HPP
#define FT2232_BASE_HPP

#include "ICommDriver.hpp"

#include <cstdint>
#include <cstddef>
#include <vector>

/**
 * @brief Shared MPSSE foundation for all FT2232 driver classes
 *
 * Supports both the FT2232H (high-speed, 60 MHz, dual MPSSE) and the
 * FT2232D/C/L (full-speed, 6 MHz, single MPSSE channel A) variants.
 * Select the correct variant via the Variant enum passed to open_device().
 *
 * Platform-specific implementations live in uFT2232Linux.cpp / uFT2232Windows.cpp.
 * The handle is stored as void* to keep SDK headers (libftdi1 / FTD2XX) out of
 * this header — each platform .cpp casts it to the appropriate type.
 *
 * Not intended to be instantiated directly — use FT2232I2C, FT2232SPI, or
 * FT2232GPIO.
 *
 * ── Chip variant differences ─────────────────────────────────────────────────
 *
 *   Variant::FT2232H  (PID 0x6010)
 *     • Two MPSSE channels: A and B
 *     • 60 MHz base clock (enabled via MPSSE_DIS_DIV5 command)
 *     • Max SCK:  30 MHz (divisor = 0)
 *     • USB Hi-Speed (480 Mbps)
 *
 *   Variant::FT2232D  (PID 0x6001 — covers FT2232D, FT2232C, FT2232L)
 *     • One MPSSE channel: A only (channel B is async serial)
 *     • 6 MHz base clock (fixed; MPSSE_DIS_DIV5 is NOT supported)
 *     • Max SCK:  3 MHz (divisor = 0)
 *     • USB Full-Speed (12 Mbps)
 *
 * ── Clock divisor formula ────────────────────────────────────────────────────
 *
 *   SCK = clock_base_hz() / ((1 + divisor) × 2)
 *   divisor = (clock_base_hz() / 2 / clockHz) − 1
 *
 *   FT2232H examples:  100 kHz → 299,   400 kHz → 74,   1 MHz → 29
 *   FT2232D examples:  100 kHz → 29,    400 kHz → 6,    1 MHz → 2
 *
 * Reference: FTDI AN_108 — Command Processor for MPSSE and MCU Host Bus
 *            FTDI AN_255 — USB to I2C Example using the FT232H and FT201X
 */
class FT2232Base
{
    public:

        using Status = ICommDriver::Status;

        // ── Device identity ──────────────────────────────────────────────────
        static constexpr uint16_t FT2232_VID   = 0x0403u; ///< FTDI VID (all variants)
        static constexpr uint16_t FT2232H_PID  = 0x6010u; ///< FT2232H high-speed PID
        static constexpr uint16_t FT2232D_PID  = 0x6001u; ///< FT2232D/C/L full-speed PID

        // ── Timeouts ─────────────────────────────────────────────────────────
        static constexpr uint32_t FT2232_READ_DEFAULT_TIMEOUT  = 5000u; ///< ms
        static constexpr uint32_t FT2232_WRITE_DEFAULT_TIMEOUT = 5000u; ///< ms

        // ── Chip variant ─────────────────────────────────────────────────────
        /**
         * @brief FT2232 silicon variant
         *
         * Determines which PID is used, which channels have MPSSE, and which
         * clock base frequency is applied in the MPSSE init sequence.
         */
        enum class Variant : uint8_t {
            FT2232H = 0, ///< High-speed (60 MHz, channels A+B, PID 0x6010)
            FT2232D = 1, ///< Full-speed (6 MHz,  channel A only, PID 0x6001)
        };

        // ── MPSSE channel selector ────────────────────────────────────────────
        /**
         * @brief MPSSE channel to open
         *
         * FT2232H: both A and B are MPSSE-capable.
         * FT2232D: only A has MPSSE; passing B returns INVALID_PARAM.
         */
        enum class Channel : uint8_t { A = 0, B = 1 };

        FT2232Base() = default;
        virtual ~FT2232Base();

        // Non-copyable
        FT2232Base(const FT2232Base&)            = delete;
        FT2232Base& operator=(const FT2232Base&) = delete;

        /** True if the device handle is open and ready */
        bool is_open() const;

        /**
         * @brief Close the device handle
         *
         * Safe to call more than once. Subclasses that need pre-close cleanup
         * should override this, perform their cleanup, then call
         * FT2232Base::close().
         */
        virtual Status close();

        /** Return the active variant (valid after a successful open_device()) */
        Variant variant() const { return m_variant; }

    protected:

        // ── MPSSE command opcodes ────────────────────────────────────────────
        //
        // Source: FTDI AN_108 — Command Processor for MPSSE and MCU Host Bus
        //
        static constexpr uint8_t MPSSE_SET_BITS_LOW   = 0x80u; ///< Set ADBUS[7:0] value+direction
        static constexpr uint8_t MPSSE_GET_BITS_LOW   = 0x81u; ///< Read ADBUS[7:0] → 1 byte
        static constexpr uint8_t MPSSE_SET_BITS_HIGH  = 0x82u; ///< Set ACBUS[7:0] value+direction
        static constexpr uint8_t MPSSE_GET_BITS_HIGH  = 0x83u; ///< Read ACBUS[7:0] → 1 byte
        static constexpr uint8_t MPSSE_LOOPBACK_OFF   = 0x85u; ///< Disable internal loopback
        static constexpr uint8_t MPSSE_SET_CLK_DIV    = 0x86u; ///< Set TCK divisor (2 bytes follow)
        static constexpr uint8_t MPSSE_SEND_IMMEDIATE = 0x87u; ///< Flush MPSSE TX buffer to USB
        static constexpr uint8_t MPSSE_DIS_DIV5       = 0x8Au; ///< 60 MHz base clock  (FT2232H only)
        static constexpr uint8_t MPSSE_EN_DIV5        = 0x8Bu; ///< 12 MHz base clock  (not used here)
        static constexpr uint8_t MPSSE_EN_3PHASE      = 0x8Cu; ///< Enable  3-phase clocking (I²C)
        static constexpr uint8_t MPSSE_DIS_3PHASE     = 0x8Du; ///< Disable 3-phase clocking
        static constexpr uint8_t MPSSE_DIS_ADAPTIVE   = 0x97u; ///< Disable adaptive clocking

        // ── MPSSE native SPI serial shift commands (AN_108 §3.3) ────────────
        //
        //   Mode 0/3 (sample on leading edge):
        //     write-only  : MPSSE_SPI_WRITE_NRE  (0x11)
        //     read-only   : MPSSE_SPI_READ_PRE   (0x20)
        //     full-duplex : MPSSE_SPI_XFER_NRE   (0x31)
        //
        //   Mode 1/2 (sample on trailing edge):
        //     write-only  : MPSSE_SPI_WRITE_PRE  (0x10)
        //     read-only   : MPSSE_SPI_READ_NRE   (0x24)
        //     full-duplex : MPSSE_SPI_XFER_PRE   (0x34)
        //
        //   Format: { cmd, lenLow, lenHigh [, txData...] }
        //   len = number_of_bytes - 1
        //
        static constexpr uint8_t MPSSE_SPI_WRITE_NRE = 0x11u; ///< Write, -ve edge (Modes 0/3)
        static constexpr uint8_t MPSSE_SPI_WRITE_PRE = 0x10u; ///< Write, +ve edge (Modes 1/2)
        static constexpr uint8_t MPSSE_SPI_READ_PRE  = 0x20u; ///< Read,  +ve edge (Modes 0/3)
        static constexpr uint8_t MPSSE_SPI_READ_NRE  = 0x24u; ///< Read,  -ve edge (Modes 1/2)
        static constexpr uint8_t MPSSE_SPI_XFER_NRE  = 0x31u; ///< Full-duplex (Modes 0/3)
        static constexpr uint8_t MPSSE_SPI_XFER_PRE  = 0x34u; ///< Full-duplex (Modes 1/2)

        // ── Variant and device handle ─────────────────────────────────────────
        Variant m_variant = Variant::FT2232H; ///< Set by open_device(); used in clock helpers

        /**
         * Stored as void* to avoid leaking libftdi1 / FTD2XX headers into
         * this header. Platform .cpp files cast to the appropriate type:
         *   Linux   : struct ftdi_context*
         *   Windows : FT_HANDLE  (which is itself a void*)
         *
         * nullptr means the device is not open.
         */
        void* m_hDevice = nullptr;

        // ── Clock helpers — variant-aware ────────────────────────────────────

        /**
         * @brief Return the MPSSE clock base frequency for the active variant
         *
         *   FT2232H → 60,000,000 Hz  (after MPSSE_DIS_DIV5)
         *   FT2232D →  6,000,000 Hz  (fixed; DIS_DIV5 not supported)
         */
        uint32_t clock_base_hz() const
        {
            return (m_variant == Variant::FT2232H) ? 60000000u : 6000000u;
        }

        /**
         * @brief Append the clock-mode selection byte(s) to an MPSSE init buffer
         *
         *   FT2232H → appends MPSSE_DIS_DIV5 (selects 60 MHz base)
         *   FT2232D → appends nothing (6 MHz base is fixed; the command
         *             is not supported and must not be sent)
         */
        void push_clock_init(std::vector<uint8_t>& buf) const
        {
            if (m_variant == Variant::FT2232H) {
                buf.push_back(MPSSE_DIS_DIV5);
            }
            // FT2232D: no command needed — 6 MHz is the hardware default
        }

        // ── Device open ───────────────────────────────────────────────────────

        /**
         * @brief Enumerate FT2232 devices and open the MPSSE handle
         *
         * @param variant       FT2232H or FT2232D — selects PID and channel rules
         * @param channel       MPSSE channel (A or B; FT2232D accepts A only)
         * @param u8DeviceIndex Zero-based index among connected chips of this variant
         */
        Status open_device(Variant variant, Channel channel, uint8_t u8DeviceIndex);

        // ── MPSSE transport primitives — implemented in platform .cpp files ──

        /** Write raw MPSSE command bytes to the device */
        Status mpsse_write(const uint8_t* buf, size_t len) const;

        /**
         * Read response bytes queued by GET_BITS / shift-in commands
         * @param timeoutMs  ms before returning READ_TIMEOUT
         * @param bytesRead  actual bytes received
         */
        Status mpsse_read(uint8_t* buf, size_t len,
                          uint32_t timeoutMs, size_t& bytesRead) const;

        /** Discard any pending bytes in the device RX/TX FIFOs */
        Status mpsse_purge() const;
};

#endif // FT2232_BASE_HPP
