#ifndef U_CP2112_DRIVER_H
#define U_CP2112_DRIVER_H

#include "ICommDriver.hpp"

#include <cstdint>
#include <string>
#include <span>

#ifdef _WIN32
  #include <windows.h>
#endif


/**
 * @brief CP2112 USB-to-SMBus/I2C bridge driver
 *
 * Implements ICommDriver over the CP2112 HID interface.
 * Communicates via the OS HID layer — no external library required.
 *
 * Linux  : uses /dev/hidraw* (kernel hidraw module)
 * Windows: uses Windows HID API (hid.lib + setupapi.lib)
 *
 * tout_write → I2C write transaction to the configured slave address
 * tout_read  → I2C read  transaction from the configured slave address
 *
 * @note  The slave address supplied to open() must be the 7-bit address.
 *        The driver shifts it internally to produce 8-bit read/write addresses.
 *
 * @note  Maximum single I2C write payload: 61 bytes (one HID report).
 *        Maximum single I2C read  payload: 512 bytes (multiple IN packets).
 */
class CP2112 : public ICommDriver
{
    public:

        // ---------------------------------------------------------------
        // Device constants
        // ---------------------------------------------------------------
        static constexpr uint16_t CP2112_VID              = 0x10C4; ///< Silicon Labs VID
        static constexpr uint16_t CP2112_PID              = 0xEA90; ///< CP2112 PID
        static constexpr size_t   HID_REPORT_SIZE         = 64;     ///< HID report size (incl. report ID)
        static constexpr size_t   MAX_I2C_WRITE_PAYLOAD   = 61;     ///< Max bytes per I2C write
        static constexpr size_t   MAX_I2C_READ_LEN        = 512;    ///< Max bytes per I2C read request
        static constexpr uint32_t CP2112_READ_DEFAULT_TIMEOUT  = 5000; ///< ms
        static constexpr uint32_t CP2112_WRITE_DEFAULT_TIMEOUT = 5000; ///< ms
        static constexpr uint32_t STATUS_POLL_INTERVAL_MS = 2;      ///< Transfer-status polling interval


        CP2112() = default;

        /**
         * @brief Construct and immediately open the device
         * @param u8I2CAddress  7-bit I2C slave address
         * @param u32ClockHz    I2C clock speed in Hz  (default 400 kHz)
         * @param u8DeviceIndex Zero-based index when multiple CP2112s are connected
         */
        explicit CP2112(uint8_t u8I2CAddress, uint32_t u32ClockHz = 400000, uint8_t u8DeviceIndex = 0)
        {
            open(u8I2CAddress, u32ClockHz, u8DeviceIndex);
        }

        virtual ~CP2112()
        {
            close();
        }

        /**
         * @brief Open and configure the CP2112 HID device
         * @param u8I2CAddress  7-bit I2C slave address
         * @param u32ClockHz    I2C clock in Hz
         * @param u8DeviceIndex Which CP2112 to open if multiple are present (0-based)
         */
        Status open(uint8_t u8I2CAddress, uint32_t u32ClockHz = 400000, uint8_t u8DeviceIndex = 0);

        Status close();
        bool   is_open() const override;

        /**
         * @brief Unified read interface
         *
         * Delegates to ReadMode-specific paths:
         *   Exact          → single I2C read of buffer.size() bytes
         *   UntilDelimiter → byte-by-byte I2C reads until delimiter found
         *   UntilToken     → byte-by-byte I2C reads with KMP token matching
         *
         * @param u32ReadTimeout ms (0 = use CP2112_READ_DEFAULT_TIMEOUT)
         */
        ReadResult  tout_read(uint32_t u32ReadTimeout,
                              std::span<uint8_t> buffer,
                              const ReadOptions& options) const override;

        /**
         * @brief Unified write interface
         *
         * Performs a single I2C write transaction.
         * Maximum payload per call: MAX_I2C_WRITE_PAYLOAD (61) bytes.
         *
         * @param u32WriteTimeout ms (0 = use CP2112_WRITE_DEFAULT_TIMEOUT)
         */
        WriteResult tout_write(uint32_t u32WriteTimeout,
                               std::span<const uint8_t> buffer) const override;


    private:

        // ---------------------------------------------------------------
        // HID Report IDs  (AN495, CP2112 HID USB-to-SMBus/I2C Bridge)
        // ---------------------------------------------------------------
        static constexpr uint8_t RPT_SMBUS_CONFIG         = 0x06; ///< Get/Set SMBus configuration
        static constexpr uint8_t RPT_DATA_READ_REQUEST    = 0x09; ///< Initiate I2C read
        static constexpr uint8_t RPT_DATA_WRITE_READ_REQ  = 0x0A; ///< Combined write-then-read
        static constexpr uint8_t RPT_DATA_READ_FORCE_SEND = 0x0B; ///< Force flush of read buffer
        static constexpr uint8_t RPT_DATA_READ_RESPONSE   = 0x0C; ///< Interrupt IN: read data
        static constexpr uint8_t RPT_DATA_WRITE           = 0x0D; ///< Interrupt OUT: write data
        static constexpr uint8_t RPT_TRANSFER_STATUS_REQ  = 0x0E; ///< Request transfer status
        static constexpr uint8_t RPT_TRANSFER_STATUS_RESP = 0x0F; ///< Transfer status response
        static constexpr uint8_t RPT_CANCEL_TRANSFER      = 0x11; ///< Abort current transfer

        // ---------------------------------------------------------------
        // Transfer status codes (byte 1 of RPT_TRANSFER_STATUS_RESP / RPT_DATA_READ_RESPONSE)
        // ---------------------------------------------------------------
        static constexpr uint8_t XFER_IDLE     = 0x00;
        static constexpr uint8_t XFER_BUSY     = 0x01;
        static constexpr uint8_t XFER_COMPLETE = 0x02;
        static constexpr uint8_t XFER_ERROR    = 0x03;

        // ---------------------------------------------------------------
        // Platform handle
        // ---------------------------------------------------------------
#ifdef _WIN32
        HANDLE   m_hDevice  = INVALID_HANDLE_VALUE;
#else
        int      m_hDevice  = -1;
#endif

        uint8_t  m_u8I2CAddress = 0x00; ///< 7-bit I2C slave address


        // ---------------------------------------------------------------
        // Low-level HID I/O  (platform-implemented)
        // ---------------------------------------------------------------

        /** Send a 64-byte Feature report (buf[0] = report ID) */
        Status hid_set_feature     (const uint8_t* buf, size_t len) const;

        /** Receive a 64-byte Feature report (buf[0] = report ID on entry & exit) */
        Status hid_get_feature     (uint8_t* buf, size_t len) const;

        /** Write a 64-byte Interrupt OUT report (buf[0] = report ID) */
        Status hid_interrupt_write (const uint8_t* buf, size_t len) const;

        /**
         * Read one Interrupt IN report with timeout
         * @param buf        64-byte receive buffer; buf[0] will be the report ID
         * @param timeoutMs  milliseconds to wait before returning READ_TIMEOUT
         * @param bytesRead  actual bytes returned by the OS
         */
        Status hid_interrupt_read  (uint8_t* buf, size_t len,
                                    uint32_t timeoutMs, size_t& bytesRead) const;


        // ---------------------------------------------------------------
        // CP2112 protocol helpers  (implemented in Common)
        // ---------------------------------------------------------------

        /** Push SMBus/I2C clock speed and options to the device */
        Status configure_smbus   (uint32_t u32ClockHz) const;

        /**
         * Execute one or more I2C write transactions, chunking automatically
         * when data exceeds MAX_I2C_WRITE_PAYLOAD (61 bytes).
         * @param bytesWritten  Accumulates successfully written bytes across all chunks.
         */
        Status i2c_write         (std::span<const uint8_t> data, uint32_t timeoutMs,
                                  size_t& bytesWritten) const;

        /** Send a single ≤61-byte chunk as one HID Data Write report */
        Status i2c_write_chunk   (std::span<const uint8_t> chunk, uint32_t timeoutMs) const;

        /** Execute a single I2C read transaction (≤512 bytes) */
        Status i2c_read          (std::span<uint8_t> data, size_t& bytesRead,
                                  uint32_t timeoutMs) const;

        /** Poll Transfer Status until Idle/Complete or timeout */
        Status poll_transfer_done(uint32_t timeoutMs) const;

        /** Send Cancel Transfer report to abort any in-progress transaction */
        Status cancel_transfer   () const;
};


#endif // U_CP2112_DRIVER_H
