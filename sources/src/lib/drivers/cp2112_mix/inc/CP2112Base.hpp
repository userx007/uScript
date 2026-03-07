#ifndef CP2112_BASE_HPP
#define CP2112_BASE_HPP

#include "ICommDriver.hpp"

#include <cstdint>
#include <cstddef>

#ifdef _WIN32
  #include <windows.h>
#endif

/**
 * @brief Shared HID foundation for all CP2112 driver classes
 *
 * Owns the OS-level device handle and provides the four low-level HID
 * primitives that both the I²C driver (CP2112) and the GPIO driver
 * (CP2112Gpio) build on top of.
 *
 * Platform-specific implementations live in uCP2112Linux.cpp /
 * uCP2112Windows.cpp alongside the rest of the platform code.
 *
 * Not intended to be instantiated directly — use CP2112 or CP2112Gpio.
 */
class CP2112Base
{
    public:

        using Status = ICommDriver::Status;

        // ── Device identity ─────────────────────────────────────────────────
        static constexpr uint16_t CP2112_VID = 0x10C4u; ///< Silicon Labs VID
        static constexpr uint16_t CP2112_PID = 0xEA90u; ///< CP2112 PID

        // ── HID transport constants ──────────────────────────────────────────
        static constexpr size_t   HID_REPORT_SIZE            = 64u;
        static constexpr uint32_t CP2112_READ_DEFAULT_TIMEOUT  = 5000u; ///< ms
        static constexpr uint32_t CP2112_WRITE_DEFAULT_TIMEOUT = 5000u; ///< ms
        static constexpr uint32_t STATUS_POLL_INTERVAL_MS      = 2u;    ///< ms

        CP2112Base() = default;
        virtual ~CP2112Base();

        // Non-copyable
        CP2112Base(const CP2112Base&)            = delete;
        CP2112Base& operator=(const CP2112Base&) = delete;

        /**
         * @brief True if the device handle is open and ready
         */
        bool is_open() const;

        /**
         * @brief Close the device handle
         *
         * Safe to call more than once. Subclasses that need pre-close cleanup
         * (e.g. cancelling an in-flight I²C transfer) should override this,
         * perform their cleanup, then call CP2112Base::close().
         */
        virtual Status close();

    protected:

        // ── HID Report IDs shared by I²C and GPIO sub-layers ────────────────
        // GPIO reports
        static constexpr uint8_t RPT_GPIO_CONFIG          = 0x02u;
        static constexpr uint8_t RPT_GPIO_GET             = 0x03u;
        static constexpr uint8_t RPT_GPIO_SET             = 0x04u;
        // I²C / SMBus reports
        static constexpr uint8_t RPT_SMBUS_CONFIG         = 0x06u;
        static constexpr uint8_t RPT_DATA_READ_REQUEST    = 0x09u;
        static constexpr uint8_t RPT_DATA_WRITE_READ_REQ  = 0x0Au;
        static constexpr uint8_t RPT_DATA_READ_FORCE_SEND = 0x0Bu;
        static constexpr uint8_t RPT_DATA_READ_RESPONSE   = 0x0Cu;
        static constexpr uint8_t RPT_DATA_WRITE           = 0x0Du;
        static constexpr uint8_t RPT_TRANSFER_STATUS_REQ  = 0x0Eu;
        static constexpr uint8_t RPT_TRANSFER_STATUS_RESP = 0x0Fu;
        static constexpr uint8_t RPT_CANCEL_TRANSFER      = 0x11u;

        // ── Transfer status codes ────────────────────────────────────────────
        static constexpr uint8_t XFER_IDLE     = 0x00u;
        static constexpr uint8_t XFER_BUSY     = 0x01u;
        static constexpr uint8_t XFER_COMPLETE = 0x02u;
        static constexpr uint8_t XFER_ERROR    = 0x03u;

        // ── Platform device handle ───────────────────────────────────────────
#ifdef _WIN32
        HANDLE  m_hDevice = INVALID_HANDLE_VALUE;
#else
        int     m_hDevice = -1;
#endif

        /**
         * @brief Enumerate CP2112 devices and open handle for the requested index
         *
         * Shared by CP2112::open() and CP2112Gpio::open(); both call this
         * then layer their own configuration on top.
         *
         * @param u8DeviceIndex  Zero-based index among connected CP2112 devices
         */
        Status open_device(uint8_t u8DeviceIndex);

        // ── HID primitives — implemented in the platform .cpp files ─────────

        /** Send a 64-byte Feature report (buf[0] = report ID) */
        Status hid_set_feature    (const uint8_t* buf, size_t len) const;

        /** Receive a 64-byte Feature report (buf[0] = desired report ID on entry) */
        Status hid_get_feature    (uint8_t* buf, size_t len) const;

        /** Write a 64-byte Interrupt OUT report (buf[0] = report ID) */
        Status hid_interrupt_write(const uint8_t* buf, size_t len) const;

        /**
         * Read one Interrupt IN report with timeout
         * @param timeoutMs   ms to wait before returning READ_TIMEOUT
         * @param bytesRead   filled with the number of bytes returned by the OS
         */
        Status hid_interrupt_read (uint8_t* buf, size_t len,
                                   uint32_t timeoutMs, size_t& bytesRead) const;
};

#endif // CP2112_BASE_HPP
