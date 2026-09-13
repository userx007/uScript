#ifndef U_FT2232_GPIO_DRIVER_H
#define U_FT2232_GPIO_DRIVER_H

#include "FT2232Base.hpp"

#include <cstdint>

/**
 * @brief FT2232 GPIO driver
 *
 * Provides direct control of the GPIO pins available on the selected MPSSE
 * channel via the MPSSE SET/GET_BITS_LOW and SET/GET_BITS_HIGH commands.
 *
 * Each MPSSE channel exposes two independent 8-bit banks:
 *
 *   Bank::Low  — ADBUS[7:0]  (SET/GET_BITS_LOW,  opcodes 0x80/0x81)
 *   Bank::High — ACBUS[7:0]  (SET/GET_BITS_HIGH, opcodes 0x82/0x83)
 *
 * ── Variant notes ────────────────────────────────────────────────────────────
 *
 *   FT2232H: both channels A and B support MPSSE and therefore GPIO.
 *   FT2232D: only channel A supports MPSSE; channel B is async serial only.
 *
 * ── Coexistence with I²C / SPI ───────────────────────────────────────────────
 *
 *   For GPIO on a different channel than the protocol driver:
 *     Open FT2232GPIO on channel B, FT2232I2C/SPI on channel A. Each opens
 *     its own handle; the two MPSSE engines are fully independent.
 *
 *   For GPIO pins on the SAME channel as a protocol driver:
 *     Do not use this class. Instead drive the spare ADBUS4–7 / ACBUS pins
 *     directly with SET_BITS_LOW commands interleaved in the protocol traffic.
 */
class FT2232GPIO : public FT2232Base
{
    public:

        using Status = ICommDriver::Status;

        enum class Bank : uint8_t { Low = 0, High = 1 };

        /**
         * @brief Full GPIO channel configuration
         *
         * Direction bits: 1 = output, 0 = input.
         * Defaults: all pins as inputs (safe on open).
         */
        struct GpioConfig {
            uint8_t lowDirMask   = 0x00u; ///< ADBUS direction
            uint8_t lowValue     = 0x00u; ///< ADBUS initial output levels
            uint8_t highDirMask  = 0x00u; ///< ACBUS direction
            uint8_t highValue    = 0x00u; ///< ACBUS initial output levels
            Variant variant      = Variant::FT2232H;
            Channel channel      = Channel::A;
        };

        FT2232GPIO() = default;

        explicit FT2232GPIO(const GpioConfig& config, uint8_t u8DeviceIndex = 0u)
        {
            this->open(config, u8DeviceIndex);
        }

        ~FT2232GPIO() override { close(); }

        Status open(const GpioConfig& config, uint8_t u8DeviceIndex = 0u);

        /** @copydoc FT2232Base::close — drives all output pins low before closing */
        Status close() override;

        bool is_open() const { return FT2232Base::is_open(); }

        // ── Direction control ────────────────────────────────────────────────
        /**
         * @brief Set pin directions for one bank
         * @param dirMask       1 = output, 0 = input
         * @param initialValue  Output level for pins newly becoming outputs
         */
        Status set_direction(Bank bank, uint8_t dirMask, uint8_t initialValue = 0x00u);

        // ── Output control ───────────────────────────────────────────────────
        Status write     (Bank bank, uint8_t value);
        Status set_pins  (Bank bank, uint8_t pinMask);
        Status clear_pins(Bank bank, uint8_t pinMask);
        Status toggle_pins(Bank bank, uint8_t pinMask);

        // ── Input reading ────────────────────────────────────────────────────
        /** Read the instantaneous level of all pins in a bank */
        Status read     (Bank bank, uint8_t& value);
        /** Read (rawValue & pinMask) into value */
        Status read_pins(Bank bank, uint8_t pinMask, uint8_t& value);

    private:

        uint8_t m_lowValue  = 0x00u;
        uint8_t m_lowDir    = 0x00u;
        uint8_t m_highValue = 0x00u;
        uint8_t m_highDir   = 0x00u;

        Status configure_mpsse_gpio(const GpioConfig& config);
        Status apply_low (uint8_t value, uint8_t dir) const;
        Status apply_high(uint8_t value, uint8_t dir) const;
};

#endif // U_FT2232_GPIO_DRIVER_H
