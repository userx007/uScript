#ifndef FT4232_BASE_HPP
#define FT4232_BASE_HPP

#include "ICommDriver.hpp"

#include <cstdint>
#include <cstddef>
#include <vector>

/**
 * @brief Shared MPSSE foundation for all FT4232H driver classes
 *
 * Owns the OS-level device handle and provides the three low-level MPSSE
 * transport primitives that all protocol layers (I²C, SPI, GPIO) build on top of.
 *
 * Platform-specific implementations live in uFT4232Linux.cpp / uFT4232Windows.cpp.
 * The handle is stored as void* to keep SDK headers (libftdi1 / FTD2XX) out of
 * this header — each platform .cpp casts it to the appropriate type.
 *
 * Not intended to be instantiated directly — use FT4232I2C or future sub-classes.
 *
 * @note  Only channels A and B of the FT4232H have the MPSSE engine.
 *        Channels C and D are UART-only and cannot be used with this driver.
 */
class FT4232Base
{
    public:

        using Status = ICommDriver::Status;

        // ── Device identity ─────────────────────────────────────────────────
        static constexpr uint16_t FT4232H_VID = 0x0403u; ///< FTDI VID
        static constexpr uint16_t FT4232H_PID = 0x6011u; ///< FT4232H PID

        // ── Timeouts ────────────────────────────────────────────────────────
        static constexpr uint32_t FT4232_READ_DEFAULT_TIMEOUT  = 5000u; ///< ms
        static constexpr uint32_t FT4232_WRITE_DEFAULT_TIMEOUT = 5000u; ///< ms

        /**
         * @brief MPSSE-capable channel selector
         *
         * Only A and B have MPSSE. Passing C or D to open_device() will
         * return INVALID_PARAM.
         */
        enum class Channel : uint8_t { A = 0, B = 1 };

        FT4232Base() = default;
        virtual ~FT4232Base();

        // Non-copyable
        FT4232Base(const FT4232Base&)            = delete;
        FT4232Base& operator=(const FT4232Base&) = delete;

        /**
         * @brief True if the device handle is open and ready
         */
        bool is_open() const;

        /**
         * @brief Close the device handle
         *
         * Safe to call more than once. Subclasses that need pre-close cleanup
         * should override this, do their cleanup, then call FT4232Base::close().
         */
        virtual Status close();

    protected:

        // ── MPSSE command bytes ──────────────────────────────────────────────
        //
        // These are the MPSSE opcodes used to build command sequences that
        // are passed to mpsse_write(). They are shared by all protocol layers
        // that inherit from this base.
        //
        // Reference: FTDI AN_108 — Command Processor for MPSSE and MCU Host Bus
        //            FTDI AN_255 — USB to I2C Example using the FT232H and FT201X
        //
        static constexpr uint8_t MPSSE_SET_BITS_LOW  = 0x80u; ///< Set ADBUS[7:0] value + direction
        static constexpr uint8_t MPSSE_GET_BITS_LOW  = 0x81u; ///< Read ADBUS[7:0] → 1 response byte
        static constexpr uint8_t MPSSE_SET_BITS_HIGH = 0x82u; ///< Set ACBUS[7:0] value + direction
        static constexpr uint8_t MPSSE_GET_BITS_HIGH = 0x83u; ///< Read ACBUS[7:0] → 1 response byte
        static constexpr uint8_t MPSSE_LOOPBACK_OFF  = 0x85u; ///< Disable internal loopback
        static constexpr uint8_t MPSSE_SET_CLK_DIV   = 0x86u; ///< Set TCK divisor (2 bytes follow)
        static constexpr uint8_t MPSSE_SEND_IMMEDIATE = 0x87u; ///< Flush MPSSE TX buffer to USB
        static constexpr uint8_t MPSSE_DIS_DIV5      = 0x8Au; ///< Use 60 MHz base clock (FT232H/4232H)
        static constexpr uint8_t MPSSE_EN_DIV5       = 0x8Bu; ///< Use 12 MHz base clock (FT2232)
        static constexpr uint8_t MPSSE_EN_3PHASE     = 0x8Cu; ///< Enable 3-phase clocking (for I²C)
        static constexpr uint8_t MPSSE_DIS_3PHASE    = 0x8Du; ///< Disable 3-phase clocking
        static constexpr uint8_t MPSSE_DIS_ADAPTIVE  = 0x97u; ///< Disable adaptive clocking

        // ── Platform device handle ───────────────────────────────────────────
        //
        // Stored as void* to avoid leaking libftdi1 / FTD2XX headers into this
        // header. Each platform .cpp casts to:
        //   Linux   : struct ftdi_context*
        //   Windows : FT_HANDLE  (which is itself a void*)
        //
        // nullptr means device is not open.
        //
        void* m_hDevice = nullptr;

        /**
         * @brief Enumerate FT4232H devices and open the MPSSE handle
         *
         * Selects the requested channel and physical device index.
         * Called by sub-class open() methods before any protocol configuration.
         *
         * @param channel       MPSSE channel to open (A or B)
         * @param u8DeviceIndex Zero-based index among connected FT4232H chips
         */
        Status open_device(Channel channel, uint8_t u8DeviceIndex);

        // ── MPSSE transport primitives ───────────────────────────────────────
        //   Implemented in uFT4232Linux.cpp / uFT4232Windows.cpp

        /**
         * @brief Write raw MPSSE command bytes to the device
         *
         * All I²C, SPI, and GPIO operations are built by constructing a
         * std::vector<uint8_t> of MPSSE opcodes and handing it to this method.
         *
         * @param buf  Pointer to command buffer
         * @param len  Number of bytes to write
         */
        Status mpsse_write(const uint8_t* buf, size_t len) const;

        /**
         * @brief Read response bytes produced by GET_BITS / read commands
         *
         * Call after any MPSSE command sequence that queues response bytes
         * (GET_BITS_LOW, serial read commands, etc.).
         *
         * @param buf        Receive buffer
         * @param len        Exact number of bytes expected
         * @param timeoutMs  Give up after this many milliseconds
         * @param bytesRead  Actual bytes received
         */
        Status mpsse_read(uint8_t* buf, size_t len,
                          uint32_t timeoutMs, size_t& bytesRead) const;

        /**
         * @brief Discard any pending bytes in the device's RX/TX FIFOs
         */
        Status mpsse_purge() const;
};

#endif // FT4232_BASE_HPP
