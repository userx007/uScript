#ifndef U_FT232H_I2C_DRIVER_H
#define U_FT232H_I2C_DRIVER_H

#include "FT232HBase.hpp"
#include "ICommDriver.hpp"

#include <cstdint>
#include <span>
#include <vector>

/**
 * @brief FT232H I²C master driver
 *
 * Inherits the MPSSE foundation from FT232HBase and implements ICommDriver's
 * unified read/write interface over a bit-banged I²C bus.
 *
 * The FT232H has a single MPSSE channel — no channel selector is needed.
 *
 * ── Pin assignment (ADBUS) ───────────────────────────────────────────────────
 *
 *   ADBUS0 (TCK) — SCL   : always output
 *   ADBUS1 (TDI) — SDA   : output when driving low; input when releasing
 *   ADBUS2 (TDO) — SDA_IN: always input; used to sample SDA
 *
 * Connect ADBUS1 and ADBUS2 together on the PCB and add a pull-up to VCC.
 *
 * ── Clock formula ────────────────────────────────────────────────────────────
 *
 *   SCL = 60 MHz / ((1 + divisor) × 2)
 *   divisor = (30,000,000 / clockHz) − 1
 *
 *   Examples: 100 kHz → 299,   400 kHz → 74,   1 MHz → 29,   3.4 MHz → 7
 */
class FT232HI2C : public FT232HBase, public ICommDriver
{
    public:

        using Status = ICommDriver::Status;

        FT232HI2C() = default;

        /**
         * @brief Construct and immediately open the device
         *
         * @param u8I2CAddress  7-bit I²C slave address
         * @param u32ClockHz    I²C clock in Hz (default 100 kHz)
         * @param u8DeviceIndex Zero-based index when multiple FT232H chips are connected
         */
        explicit FT232HI2C(uint8_t  u8I2CAddress,
                           uint32_t u32ClockHz    = 100000u,
                           uint8_t  u8DeviceIndex = 0u)
        {
            this->open(u8I2CAddress, u32ClockHz, u8DeviceIndex);
        }

        ~FT232HI2C() override { close(); }

        /**
         * @brief Open the FT232H and configure MPSSE for I²C
         *
         * @param u8I2CAddress  7-bit I²C slave address
         * @param u32ClockHz    I²C SCL frequency in Hz
         * @param u8DeviceIndex Physical device index
         */
        Status open(uint8_t  u8I2CAddress,
                    uint32_t u32ClockHz    = 100000u,
                    uint8_t  u8DeviceIndex = 0u);

        Status close() override;
        bool is_open() const override { return FT232HBase::is_open(); }

        /**
         * @brief Unified read interface
         *
         * Sends Repeated-START with slave read address, then reads bytes.
         */
        ReadResult  tout_read(uint32_t u32ReadTimeout,
                              std::span<uint8_t> buffer,
                              const ReadOptions& options) const override;

        /**
         * @brief Unified write interface
         *
         * Sends START, slave write address, data bytes, STOP.
         */
        WriteResult tout_write(uint32_t u32WriteTimeout,
                               std::span<const uint8_t> buffer) const override;

    private:

        // ── I²C pin masks (ADBUS low byte) ───────────────────────────────────
        static constexpr uint8_t I2C_SCL   = 0x01u; ///< ADBUS0: SCL
        static constexpr uint8_t I2C_SDA_O = 0x02u; ///< ADBUS1: SDA drive
        static constexpr uint8_t I2C_SDA_I = 0x04u; ///< ADBUS2: SDA read

        static constexpr uint8_t DIR_SCL_SDA_OUT = I2C_SCL | I2C_SDA_O;
        static constexpr uint8_t DIR_SCL_ONLY    = I2C_SCL;

        uint8_t m_u8I2CAddress = 0x00u;

        Status configure_mpsse_i2c(uint32_t u32ClockHz) const;

        static void push_pin_state(std::vector<uint8_t>& buf,
                                   bool scl, bool drive_sda_low);
        static void push_read_sda(std::vector<uint8_t>& buf);

        Status i2c_start()          const;
        Status i2c_repeated_start() const;
        Status i2c_stop()           const;
        Status i2c_write_byte(uint8_t byte, bool& ack) const;
        Status i2c_read_byte(uint8_t& byte, bool sendAck) const;

        Status i2c_write(std::span<const uint8_t> data,
                         uint32_t timeoutMs,
                         size_t& bytesWritten) const;
        Status i2c_read(std::span<uint8_t> data,
                        size_t& bytesRead,
                        uint32_t timeoutMs) const;
};

#endif // U_FT232H_I2C_DRIVER_H
