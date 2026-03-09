#ifndef U_FT2232_I2C_DRIVER_H
#define U_FT2232_I2C_DRIVER_H

#include "FT2232Base.hpp"
#include "ICommDriver.hpp"

#include <cstdint>
#include <span>
#include <vector>

/**
 * @brief FT2232 I²C master driver
 *
 * Inherits the MPSSE foundation from FT2232Base and implements ICommDriver's
 * unified read/write interface over a bit-banged I²C bus.
 *
 * I²C is implemented via MPSSE SET_BITS_LOW / GET_BITS_LOW commands so the
 * bus protocol (START, STOP, ACK, NAK) is fully under software control,
 * giving correct open-drain behaviour on all three signal lines.
 *
 * ── Pin assignment (ADBUS low byte) ─────────────────────────────────────────
 *
 *   ADBUS0 (TCK) — SCL   : always output
 *   ADBUS1 (TDI) — SDA_O : output-low when driving; input (hi-Z) when releasing
 *   ADBUS2 (TDO) — SDA_I : always input; used to sample SDA state
 *
 * Connect ADBUS1 and ADBUS2 together on the PCB with a pull-up to VCC.
 * The driver emulates open-drain by switching ADBUS1 between output-low
 * and high-impedance (input) rather than actively driving high.
 *
 * ── Supported variants and clock limits ─────────────────────────────────────
 *
 *   Variant::FT2232H  → base clock 60 MHz → max 30 MHz TCK
 *     Typical I²C clocks: 100 kHz (divisor 299), 400 kHz (divisor 74)
 *
 *   Variant::FT2232D  → base clock  6 MHz → max  3 MHz TCK
 *     Typical I²C clocks: 100 kHz (divisor 29),  400 kHz (divisor 6)
 *
 * @note Only channels A and B of the FT2232H have MPSSE.
 *       For the FT2232D, only channel A supports MPSSE.
 */
class FT2232I2C : public FT2232Base, public ICommDriver
{
    public:

        using Status = ICommDriver::Status;

        FT2232I2C() = default;

        /**
         * @brief Construct and immediately open the device
         * @param u8I2CAddress  7-bit I²C slave address
         * @param u32ClockHz    SCL frequency in Hz  (default 100 kHz)
         * @param variant       FT2232H or FT2232D   (default FT2232H)
         * @param channel       MPSSE channel        (default A)
         * @param u8DeviceIndex Zero-based device index
         */
        explicit FT2232I2C(uint8_t  u8I2CAddress,
                           uint32_t u32ClockHz    = 100000u,
                           Variant  variant       = Variant::FT2232H,
                           Channel  channel       = Channel::A,
                           uint8_t  u8DeviceIndex = 0u)
        {
            this->open(u8I2CAddress, u32ClockHz, variant, channel, u8DeviceIndex);
        }

        ~FT2232I2C() override { close(); }

        /**
         * @brief Open the FT2232 and configure MPSSE for I²C
         *
         * @param u8I2CAddress  7-bit I²C slave address
         * @param u32ClockHz    SCL frequency in Hz
         * @param variant       FT2232H or FT2232D
         * @param channel       MPSSE channel (A or B; FT2232D only supports A)
         * @param u8DeviceIndex Physical device index
         */
        Status open(uint8_t  u8I2CAddress,
                    uint32_t u32ClockHz    = 100000u,
                    Variant  variant       = Variant::FT2232H,
                    Channel  channel       = Channel::A,
                    uint8_t  u8DeviceIndex = 0u);

        /** @copydoc FT2232Base::close — sends I²C STOP before closing */
        Status close() override;

        bool is_open() const override { return FT2232Base::is_open(); }

        /**
         * @brief Unified read interface (ICommDriver)
         *
         * Modes: Exact, UntilDelimiter, UntilToken.
         * @param u32ReadTimeout ms (0 = FT2232_READ_DEFAULT_TIMEOUT)
         */
        ReadResult  tout_read(uint32_t u32ReadTimeout,
                              std::span<uint8_t> buffer,
                              const ReadOptions& options) const override;

        /**
         * @brief Unified write interface (ICommDriver)
         *
         * START → addr+W → bytes → STOP. No payload size limit.
         * @param u32WriteTimeout ms (0 = FT2232_WRITE_DEFAULT_TIMEOUT)
         */
        WriteResult tout_write(uint32_t u32WriteTimeout,
                               std::span<const uint8_t> buffer) const override;

    private:

        // ── I²C pin masks (ADBUS low byte) ───────────────────────────────────
        static constexpr uint8_t I2C_SCL   = 0x01u; ///< ADBUS0: SCL output
        static constexpr uint8_t I2C_SDA_O = 0x02u; ///< ADBUS1: SDA drive
        static constexpr uint8_t I2C_SDA_I = 0x04u; ///< ADBUS2: SDA read

        static constexpr uint8_t DIR_SCL_SDA_OUT = I2C_SCL | I2C_SDA_O; ///< 0x03
        static constexpr uint8_t DIR_SCL_ONLY    = I2C_SCL;              ///< 0x01

        uint8_t m_u8I2CAddress = 0x00u;

        Status configure_mpsse_i2c(uint32_t u32ClockHz) const;

        static void push_pin_state(std::vector<uint8_t>& buf, bool scl, bool drive_sda_low);
        static void push_read_sda (std::vector<uint8_t>& buf);

        Status i2c_start()          const;
        Status i2c_repeated_start() const;
        Status i2c_stop()           const;

        Status i2c_write_byte(uint8_t byte, bool& ack)          const;
        Status i2c_read_byte (uint8_t& byte, bool sendAck)      const;

        Status i2c_write(std::span<const uint8_t> data,
                         uint32_t timeoutMs, size_t& bytesWritten) const;

        Status i2c_read (std::span<uint8_t> data,
                         size_t& bytesRead, uint32_t timeoutMs)    const;
};

#endif // U_FT2232_I2C_DRIVER_H
