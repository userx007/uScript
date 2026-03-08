#ifndef U_FT4232_I2C_DRIVER_H
#define U_FT4232_I2C_DRIVER_H

#include "FT4232Base.hpp"
#include "ICommDriver.hpp"

#include <cstdint>
#include <span>
#include <vector>

/**
 * @brief FT4232H I²C master driver
 *
 * Inherits the MPSSE foundation from FT4232Base and implements ICommDriver's
 * unified read/write interface over a bit-banged I²C bus.
 *
 * I²C is implemented via MPSSE SET_BITS_LOW / GET_BITS_LOW commands so the
 * bus protocol (START, STOP, ACK, NAK) is fully under software control.
 * This gives correct open-drain behaviour on all three signal lines.
 *
 * Pin assignment on the selected MPSSE channel (ADBUS low byte):
 *   ADBUS0 (TCK) — SCL   : always output
 *   ADBUS1 (TDI) — SDA   : output when driving low; input when releasing
 *   ADBUS2 (TDO) — SDA_IN: always input; used to sample SDA state
 *
 * Connect ADBUS1 and ADBUS2 together on the PCB and add a pull-up to VCC.
 * The driver emulates open-drain by switching ADBUS1 between output-low
 * and high-impedance (input) rather than actively driving high.
 *
 * @note  Only channels A and B of the FT4232H have MPSSE.
 * @note  No payload size limit — transfers of any length are supported.
 *        The underlying USB bulk transfer handles segmentation transparently.
 *
 * Clock calculation (bit-bang, no 3-phase):
 *   TCK = 60 MHz / ((1 + divisor) * 2)
 *   divisor = (30,000,000 / clockHz) - 1
 *   Example: 100 kHz → divisor = 299, 400 kHz → divisor = 74
 */
class FT4232I2C : public FT4232Base, public ICommDriver
{
    public:

        // Resolve ambiguity: both FT4232Base and ICommDriver introduce 'Status'
        using Status = ICommDriver::Status;

        FT4232I2C() = default;

        /**
         * @brief Construct and immediately open the device
         * @param u8I2CAddress  7-bit I²C slave address
         * @param u32ClockHz    I²C clock in Hz  (default 100 kHz)
         * @param channel       MPSSE channel to use (default A)
         * @param u8DeviceIndex Zero-based index when multiple FT4232H chips are connected
         */
        explicit FT4232I2C(uint8_t  u8I2CAddress,
                           uint32_t u32ClockHz    = 100000u,
                           Channel  channel       = Channel::A,
                           uint8_t  u8DeviceIndex = 0u)
        {
            this->open(u8I2CAddress, u32ClockHz, channel, u8DeviceIndex);
        }

        ~FT4232I2C() override { close(); }

        /**
         * @brief Open the FT4232H and configure MPSSE for I²C
         *
         * @param u8I2CAddress  7-bit I²C slave address
         * @param u32ClockHz    I²C SCL frequency in Hz
         * @param channel       MPSSE channel (A or B)
         * @param u8DeviceIndex Physical device index
         */
        Status open(uint8_t  u8I2CAddress,
                    uint32_t u32ClockHz    = 100000u,
                    Channel  channel       = Channel::A,
                    uint8_t  u8DeviceIndex = 0u);

        /** @copydoc FT4232Base::close — also sends I²C STOP before closing */
        Status close() override;

        bool is_open() const override { return FT4232Base::is_open(); }

        /**
         * @brief Unified read interface
         *
         * Sends a repeated START with the slave read address, then reads bytes.
         *
         * Modes:
         *   Exact          → read exactly buffer.size() bytes
         *   UntilDelimiter → read byte-by-byte until delimiter found
         *   UntilToken     → read byte-by-byte with KMP token matching
         *
         * @param u32ReadTimeout ms (0 = FT4232_READ_DEFAULT_TIMEOUT)
         */
        ReadResult  tout_read(uint32_t u32ReadTimeout,
                              std::span<uint8_t> buffer,
                              const ReadOptions& options) const override;

        /**
         * @brief Unified write interface
         *
         * Sends START, slave write address, data bytes, STOP.
         * No payload size limit.
         *
         * @param u32WriteTimeout ms (0 = FT4232_WRITE_DEFAULT_TIMEOUT)
         */
        WriteResult tout_write(uint32_t u32WriteTimeout,
                               std::span<const uint8_t> buffer) const override;

    private:

        // ── I²C pin masks (ADBUS low byte) ───────────────────────────────────
        static constexpr uint8_t I2C_SCL   = 0x01u; ///< ADBUS0: SCL (always output)
        static constexpr uint8_t I2C_SDA_O = 0x02u; ///< ADBUS1: SDA drive (output when low)
        static constexpr uint8_t I2C_SDA_I = 0x04u; ///< ADBUS2: SDA read (always input)

        // ── Pin direction masks ───────────────────────────────────────────────
        /// SCL=output, SDA_O=output (actively driving SDA)
        static constexpr uint8_t DIR_SCL_SDA_OUT = I2C_SCL | I2C_SDA_O; // 0x03
        /// SCL=output, SDA_O=input  (releasing SDA — open-drain high)
        static constexpr uint8_t DIR_SCL_ONLY    = I2C_SCL;              // 0x01

        uint8_t  m_u8I2CAddress = 0x00u; ///< 7-bit I²C slave address

        // ── I²C protocol helpers (implemented in uFT4232I2CCommon.cpp) ───────

        /** Push MPSSE clock configuration commands */
        Status configure_mpsse_i2c(uint32_t u32ClockHz) const;

        /** Append a SET_BITS_LOW command to a command buffer */
        static void push_pin_state(std::vector<uint8_t>& buf,
                                   bool scl, bool drive_sda_low);

        /** Append GET_BITS_LOW + SEND_IMMEDIATE to a command buffer */
        static void push_read_sda(std::vector<uint8_t>& buf);

        /**
         * @brief Build and send I²C START condition
         *
         * Sequence: idle (SCL=H, SDA=H) → SDA falls while SCL high → SCL falls
         */
        Status i2c_start() const;

        /**
         * @brief Build and send I²C Repeated START condition
         *
         * Used before a read following a write in the same transaction.
         * Sequence: SCL=H → SDA released → SCL=H, SDA=L → SCL=L
         */
        Status i2c_repeated_start() const;

        /**
         * @brief Build and send I²C STOP condition
         *
         * Sequence: SCL=L, SDA=L → SCL=H, SDA=L → SCL=H, SDA=H (released)
         */
        Status i2c_stop() const;

        /**
         * @brief Write one byte and read the ACK/NAK bit from the slave
         *
         * @param byte    Byte to transmit (MSB first)
         * @param ack     Set to true if slave acknowledged (SDA low during ACK clock)
         */
        Status i2c_write_byte(uint8_t byte, bool& ack) const;

        /**
         * @brief Read one byte from the slave and send ACK or NAK
         *
         * @param byte      Received byte (MSB first)
         * @param sendAck   true → drive ACK (SDA low), false → drive NAK (release SDA)
         */
        Status i2c_read_byte(uint8_t& byte, bool sendAck) const;

        /**
         * @brief Full I²C write transaction
         *
         * START → addr+W → data bytes → STOP
         *
         * @param data         Payload to transmit
         * @param timeoutMs    Per-byte timeout
         * @param bytesWritten Accumulates bytes successfully written
         */
        Status i2c_write(std::span<const uint8_t> data,
                         uint32_t timeoutMs,
                         size_t& bytesWritten) const;

        /**
         * @brief Full I²C read transaction
         *
         * Repeated-START → addr+R → data bytes (ACK each) → NAK → STOP
         *
         * @param data      Buffer to fill
         * @param bytesRead Bytes actually received
         * @param timeoutMs Per-byte timeout
         */
        Status i2c_read(std::span<uint8_t> data,
                        size_t& bytesRead,
                        uint32_t timeoutMs) const;
};

#endif // U_FT4232_I2C_DRIVER_H
