#ifndef U_CP2112_DRIVER_H
#define U_CP2112_DRIVER_H

#include "CP2112Base.hpp"
#include "ICommDriver.hpp"

#include <cstdint>
#include <span>
#include <vector>

/**
 * @brief CP2112 I²C driver
 *
 * Inherits the HID foundation from CP2112Base and implements ICommDriver's
 * unified read/write interface over the CP2112's I²C/SMBus port.
 *
 * Maximum single I²C write payload per HID report: 61 bytes.
 * tout_write() chunks automatically — no caller-side splitting required.
 *
 * Maximum single I²C read request: 512 bytes.
 */
class CP2112 : public CP2112Base, public ICommDriver
{
    public:

        // Both CP2112Base and ICommDriver introduce a 'Status' name.
        // Explicitly pull in the one canonical definition to remove ambiguity.
        using Status = ICommDriver::Status;

        static constexpr size_t MAX_I2C_WRITE_PAYLOAD = 61u;  ///< Bytes per HID write report
        static constexpr size_t MAX_I2C_READ_LEN      = 512u; ///< Max bytes per read request

        CP2112() = default;

        /**
         * @brief Construct and immediately open the device
         * @param u8I2CAddress  7-bit I²C slave address
         * @param u32ClockHz    I²C clock in Hz (default 400 kHz)
         * @param u8DeviceIndex Zero-based index when multiple CP2112s are connected
         */
        explicit CP2112(uint8_t u8I2CAddress,
                        uint32_t u32ClockHz    = 400000u,
                        uint8_t  u8DeviceIndex = 0u)
        {
            this->open(u8I2CAddress, u32ClockHz, u8DeviceIndex);
        }

        ~CP2112() override { close(); }

        /**
         * @brief Open and configure the CP2112 for I²C use
         * @param u8I2CAddress  7-bit I²C slave address
         * @param u32ClockHz    I²C clock in Hz
         * @param u8DeviceIndex Which CP2112 to open if multiple are present
         */
        Status open(uint8_t u8I2CAddress,
                    uint32_t u32ClockHz    = 400000u,
                    uint8_t  u8DeviceIndex = 0u);

        /** @copydoc CP2112Base::close — also cancels any in-flight I²C transfer */
        Status close() override;

        bool is_open() const override { return CP2112Base::is_open(); }

        /**
         * @brief Unified read  (Exact / UntilDelimiter / UntilToken)
         * @param u32ReadTimeout ms (0 = CP2112_READ_DEFAULT_TIMEOUT)
         */
        ReadResult tout_read(uint32_t u32ReadTimeout,
                             std::span<uint8_t> buffer,
                             const ReadOptions& options) const override;

        /**
         * @brief Unified write — automatically chunks payloads > 61 bytes
         * @param u32WriteTimeout ms (0 = CP2112_WRITE_DEFAULT_TIMEOUT)
         */
        WriteResult tout_write(uint32_t u32WriteTimeout,
                               std::span<const uint8_t> buffer) const override;

    private:

        uint8_t  m_u8I2CAddress = 0x00u; ///< 7-bit I²C slave address

        // ── I²C protocol helpers (implemented in uCP2112Common.cpp) ─────────

        Status configure_smbus   (uint32_t u32ClockHz) const;

        /**
         * @brief Send data over I²C, chunking automatically at 61-byte boundaries
         * @param bytesWritten  Accumulates successfully written bytes across all chunks
         */
        Status i2c_write         (std::span<const uint8_t> data,
                                  uint32_t timeoutMs,
                                  size_t& bytesWritten) const;

        /** Send a single ≤61-byte chunk as one HID Data Write report */
        Status i2c_write_chunk   (std::span<const uint8_t> chunk,
                                  uint32_t timeoutMs) const;

        Status i2c_read          (std::span<uint8_t> data,
                                  size_t& bytesRead,
                                  uint32_t timeoutMs) const;

        Status poll_transfer_done(uint32_t timeoutMs) const;
        Status cancel_transfer   () const;
};

#endif // U_CP2112_DRIVER_H
