#ifndef FT232H_BASE_HPP
#define FT232H_BASE_HPP

#include "ICommDriver.hpp"

#include <cstdint>
#include <cstddef>
#include <vector>

/**
 * @brief Shared MPSSE foundation for all FT232H driver classes
 *
 * The FT232H is a single-channel, Hi-Speed USB MPSSE device (PID 0x6014).
 * Unlike the FT2232H or FT4232H, it has only one MPSSE interface — no
 * channel selector is needed.  The 60 MHz base clock is always active
 * (MPSSE_DIS_DIV5 is sent unconditionally during init).
 *
 * Platform-specific implementations live in uFT232HLinux.cpp / uFT232HWindows.cpp.
 * The handle is stored as void* to keep SDK headers out of this header.
 *
 * Not intended to be instantiated directly — use FT232HSPI, FT232HI2C, or
 * FT232HGPIO.
 *
 * ── Hardware summary ─────────────────────────────────────────────────────────
 *
 *   VID 0x0403  PID 0x6014
 *   USB Hi-Speed (480 Mbps)
 *   60 MHz MPSSE base clock (after MPSSE_DIS_DIV5)
 *   Max SCK:  30 MHz (SPI divisor = 0)
 *   ADBUS[7:0]  — low  GPIO / SPI / I²C signals
 *   ACBUS[7:0]  — high GPIO
 *
 * ── Clock divisor formula ────────────────────────────────────────────────────
 *
 *   SCK = 60 MHz / ((1 + divisor) × 2)
 *   divisor = (30,000,000 / clockHz) − 1
 *
 *   Examples: 1 MHz → 29,  6 MHz → 4,  30 MHz → 0
 *
 * Reference: FTDI AN_108 — Command Processor for MPSSE and MCU Host Bus
 *            FTDI AN_255 — USB to I2C Example using the FT232H and FT201X
 */
class FT232HBase
{
    public:

        using Status = ICommDriver::Status;

        // ── Device identity ──────────────────────────────────────────────────
        static constexpr uint16_t FT232H_VID = 0x0403u; ///< FTDI VID (all variants)
        static constexpr uint16_t FT232H_PID = 0x6014u; ///< FT232H PID

        // ── Timeouts ─────────────────────────────────────────────────────────
        static constexpr uint32_t FT232H_READ_DEFAULT_TIMEOUT  = 5000u; ///< ms
        static constexpr uint32_t FT232H_WRITE_DEFAULT_TIMEOUT = 5000u; ///< ms

        FT232HBase() = default;
        virtual ~FT232HBase();

        // Non-copyable
        FT232HBase(const FT232HBase&)            = delete;
        FT232HBase& operator=(const FT232HBase&) = delete;

        /** True if the device handle is open and ready */
        bool is_open() const;

        /**
         * @brief Close the device handle
         *
         * Safe to call more than once. Subclasses that need pre-close cleanup
         * should override this, perform their cleanup, then call
         * FT232HBase::close().
         */
        virtual Status close();

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
        static constexpr uint8_t MPSSE_DIS_DIV5       = 0x8Au; ///< Select 60 MHz base clock
        static constexpr uint8_t MPSSE_EN_DIV5        = 0x8Bu; ///< Select 12 MHz base clock (unused)
        static constexpr uint8_t MPSSE_EN_3PHASE      = 0x8Cu; ///< Enable  3-phase clocking (I²C)
        static constexpr uint8_t MPSSE_DIS_3PHASE     = 0x8Du; ///< Disable 3-phase clocking
        static constexpr uint8_t MPSSE_DIS_ADAPTIVE   = 0x97u; ///< Disable adaptive clocking

        // ── MPSSE SPI serial shift commands (AN_108 §3.3) ───────────────────
        static constexpr uint8_t MPSSE_SPI_WRITE_NRE = 0x11u; ///< Write, -ve edge out (Modes 0/3)
        static constexpr uint8_t MPSSE_SPI_WRITE_PRE = 0x10u; ///< Write, +ve edge out (Modes 1/2)
        static constexpr uint8_t MPSSE_SPI_READ_PRE  = 0x20u; ///< Read,  +ve edge in  (Modes 0/3)
        static constexpr uint8_t MPSSE_SPI_READ_NRE  = 0x24u; ///< Read,  -ve edge in  (Modes 1/2)
        static constexpr uint8_t MPSSE_SPI_XFER_NRE  = 0x31u; ///< Full-duplex (Modes 0/3)
        static constexpr uint8_t MPSSE_SPI_XFER_PRE  = 0x34u; ///< Full-duplex (Modes 1/2)

        // ── FT232H clock base ─────────────────────────────────────────────────
        static constexpr uint32_t CLOCK_BASE_HZ = 60000000u; ///< Always 60 MHz after DIS_DIV5

        // ── Platform device handle ────────────────────────────────────────────
        //
        // Stored as void* to avoid leaking libftdi1 / FTD2XX headers.
        //   Linux   : struct ftdi_context*
        //   Windows : FT_HANDLE  (itself a void*)
        //
        // nullptr means device is not open.
        //
        void* m_hDevice = nullptr;

        // ── Device open ───────────────────────────────────────────────────────

        /**
         * @brief Enumerate FT232H devices and open the MPSSE handle
         *
         * @param u8DeviceIndex Zero-based index among connected FT232H chips
         */
        Status open_device(uint8_t u8DeviceIndex);

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

#endif // FT232H_BASE_HPP
