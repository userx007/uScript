#ifndef U_FT4232_GPIO_DRIVER_H
#define U_FT4232_GPIO_DRIVER_H

#include "FT4232Base.hpp"

#include <cstdint>

/**
 * @brief FT4232H GPIO driver
 *
 * Inherits the MPSSE foundation from FT4232Base and provides direct control
 * of all GPIO pins available on the selected MPSSE channel.
 *
 * Each MPSSE channel (A or B) exposes two independent 8-bit GPIO banks:
 *
 *   Bank::Low  — ADBUS[7:0]  controlled by MPSSE SET_BITS_LOW / GET_BITS_LOW
 *   Bank::High — ACBUS[7:0]  controlled by MPSSE SET_BITS_HIGH / GET_BITS_HIGH
 *
 * Pin direction and initial output values are configured at open() time via
 * a GpioConfig struct, and can be changed at runtime with set_direction().
 *
 * ── Pin assignment reference ─────────────────────────────────────────────────
 *
 *   Bank::Low  (ADBUS)       Bank::High (ACBUS)
 *   ──────────────────       ──────────────────
 *   bit 0  ADBUS0 (TCK)      bit 0  ACBUS0
 *   bit 1  ADBUS1 (TDI)      bit 1  ACBUS1
 *   bit 2  ADBUS2 (TDO)      bit 2  ACBUS2
 *   bit 3  ADBUS3 (TMS)      bit 3  ACBUS3
 *   bit 4  ADBUS4 (GPIOL0)   bit 4  ACBUS4
 *   bit 5  ADBUS5 (GPIOL1)   bit 5  ACBUS5
 *   bit 6  ADBUS6 (GPIOL2)   bit 6  ACBUS6
 *   bit 7  ADBUS7 (GPIOL3)   bit 7  ACBUS7
 *
 * @note  Only channels A and B of the FT4232H have the MPSSE engine.
 *
 * @note  When using FT4232GPIO alongside FT4232I2C or FT4232SPI on the same
 *        physical chip, open each driver on a different channel (A vs B).
 *        The two MPSSE channels are fully independent.
 *
 * @note  If you need GPIO pins alongside an I²C or SPI bus on the SAME channel,
 *        do not use this driver.  Instead use the ADBUS4–7 / ACBUS pins directly
 *        via the protocol driver's channel — those pins are not consumed by the
 *        I²C (ADBUS0–2) or SPI (ADBUS0–3) logic and can be driven with extra
 *        SET_BITS_LOW commands interleaved with protocol traffic.
 */
class FT4232GPIO : public FT4232Base
{
    public:

        using Status = ICommDriver::Status;

        // ── GPIO bank selector ───────────────────────────────────────────────
        enum class Bank : uint8_t {
            Low  = 0, ///< ADBUS[7:0] — SET/GET_BITS_LOW
            High = 1  ///< ACBUS[7:0] — SET/GET_BITS_HIGH
        };

        /**
         * @brief Full GPIO channel configuration
         *
         * Specifies the initial direction and output value for both banks.
         * Direction bits: 1 = output, 0 = input.
         * Value bits apply only to pins configured as outputs; input pins
         * ignore the value field (their level is determined by the circuit).
         *
         * Defaults: all pins as inputs (safe, no accidental drive on open).
         */
        struct GpioConfig {
            uint8_t lowDirMask   = 0x00u; ///< ADBUS direction  (1=output, 0=input)
            uint8_t lowValue     = 0x00u; ///< ADBUS initial output levels
            uint8_t highDirMask  = 0x00u; ///< ACBUS direction  (1=output, 0=input)
            uint8_t highValue    = 0x00u; ///< ACBUS initial output levels
            Channel channel      = Channel::A; ///< MPSSE channel to open
        };

        FT4232GPIO() = default;

        /**
         * @brief Construct and immediately open the device
         * @param config        GPIO pin configuration
         * @param u8DeviceIndex Zero-based index when multiple FT4232H chips are connected
         */
        explicit FT4232GPIO(const GpioConfig& config, uint8_t u8DeviceIndex = 0u)
        {
            this->open(config, u8DeviceIndex);
        }

        ~FT4232GPIO() override { close(); }

        /**
         * @brief Open the FT4232H and configure MPSSE for GPIO
         *
         * Enables MPSSE mode on the requested channel, applies the initial
         * pin direction and output values from config, then leaves all pins
         * in that state until further calls.
         *
         * @param config        GPIO configuration (directions, initial values, channel)
         * @param u8DeviceIndex Physical device index
         */
        Status open(const GpioConfig& config, uint8_t u8DeviceIndex = 0u);

        /** @copydoc FT4232Base::close — drives all output pins low before closing */
        Status close() override;

        bool is_open() const { return FT4232Base::is_open(); }

        // ── Direction control ────────────────────────────────────────────────

        /**
         * @brief Set pin directions for one bank
         *
         * Immediately applies a SET_BITS_LOW/HIGH command with the new direction
         * mask.  Output values for already-output pins are preserved; pins
         * switching from input to output are driven to initialValue.
         *
         * @param bank          Bank::Low (ADBUS) or Bank::High (ACBUS)
         * @param dirMask       Bitmask: 1 = output, 0 = input
         * @param initialValue  Output level for pins becoming outputs (default 0)
         */
        Status set_direction(Bank bank, uint8_t dirMask, uint8_t initialValue = 0x00u);

        // ── Output control ───────────────────────────────────────────────────

        /**
         * @brief Write output levels for all pins in a bank
         *
         * Bits corresponding to input pins are ignored by the hardware but are
         * stored internally so they are correctly restored if direction changes.
         *
         * @param bank  Bank::Low or Bank::High
         * @param value Desired output levels (full 8-bit mask)
         */
        Status write(Bank bank, uint8_t value);

        /**
         * @brief Set (drive HIGH) one or more output pins
         *
         * @param bank    Bank::Low or Bank::High
         * @param pinMask Bitmask of pins to drive high (non-output pins ignored)
         */
        Status set_pins(Bank bank, uint8_t pinMask);

        /**
         * @brief Clear (drive LOW) one or more output pins
         *
         * @param bank    Bank::Low or Bank::High
         * @param pinMask Bitmask of pins to drive low (non-output pins ignored)
         */
        Status clear_pins(Bank bank, uint8_t pinMask);

        /**
         * @brief Toggle one or more output pins
         *
         * @param bank    Bank::Low or Bank::High
         * @param pinMask Bitmask of pins to toggle (non-output pins ignored)
         */
        Status toggle_pins(Bank bank, uint8_t pinMask);

        // ── Input reading ────────────────────────────────────────────────────

        /**
         * @brief Read the current level of all pins in a bank
         *
         * Returns the instantaneous logic level of every pin — both inputs and
         * outputs — as seen on the ADBUS/ACBUS lines.  For output pins this
         * reflects the driven value; for input pins it reflects the external signal.
         *
         * @param bank   Bank::Low or Bank::High
         * @param value  Receives the 8-bit pin state
         */
        Status read(Bank bank, uint8_t& value);

        /**
         * @brief Read the level of specific pins and return their masked state
         *
         * Convenience wrapper: reads the full bank then ANDs with pinMask.
         *
         * @param bank    Bank::Low or Bank::High
         * @param pinMask Bitmask of pins to query
         * @param value   Receives (rawBankValue & pinMask)
         */
        Status read_pins(Bank bank, uint8_t pinMask, uint8_t& value);

    private:

        // ── Cached pin state ─────────────────────────────────────────────────
        // Updated every time write() / set_direction() is called so that
        // read-modify-write operations (set_pins, clear_pins, toggle_pins)
        // do not need a round-trip GET_BITS command.
        uint8_t m_lowValue  = 0x00u; ///< Last written ADBUS output value
        uint8_t m_lowDir    = 0x00u; ///< Current ADBUS direction mask
        uint8_t m_highValue = 0x00u; ///< Last written ACBUS output value
        uint8_t m_highDir   = 0x00u; ///< Current ACBUS direction mask

        // ── Internal helpers (implemented in uFT4232GPIOCommon.cpp) ─────────

        /** Push MPSSE init sequence and apply initial pin config */
        Status configure_mpsse_gpio(const GpioConfig& config);

        /**
         * @brief Apply a SET_BITS_LOW command (ADBUS bank)
         * @param value  Output level byte
         * @param dir    Direction byte (1=output)
         */
        Status apply_low(uint8_t value, uint8_t dir) const;

        /**
         * @brief Apply a SET_BITS_HIGH command (ACBUS bank)
         * @param value  Output level byte
         * @param dir    Direction byte (1=output)
         */
        Status apply_high(uint8_t value, uint8_t dir) const;
};

#endif // U_FT4232_GPIO_DRIVER_H
