#ifndef U_FT232H_GPIO_DRIVER_H
#define U_FT232H_GPIO_DRIVER_H

#include "FT232HBase.hpp"

#include <cstdint>

/**
 * @brief FT232H GPIO driver
 *
 * Inherits the MPSSE foundation from FT232HBase and provides direct control
 * of all GPIO pins on the FT232H's single MPSSE channel.
 *
 * The FT232H exposes two independent 8-bit GPIO banks:
 *
 *   Bank::Low  — ADBUS[7:0]  controlled by MPSSE SET_BITS_LOW / GET_BITS_LOW
 *   Bank::High — ACBUS[7:0]  controlled by MPSSE SET_BITS_HIGH / GET_BITS_HIGH
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
 */
class FT232HGPIO : public FT232HBase
{
    public:

        using Status = ICommDriver::Status;

        /** GPIO bank selector */
        enum class Bank : uint8_t {
            Low  = 0, ///< ADBUS[7:0] — SET/GET_BITS_LOW
            High = 1  ///< ACBUS[7:0] — SET/GET_BITS_HIGH
        };

        /**
         * @brief Full GPIO configuration
         *
         * Specifies initial direction and output value for both banks.
         * Direction bits: 1 = output, 0 = input.
         * Defaults: all inputs (safe — no accidental drive on open).
         *
         * No channel field — FT232H has one MPSSE interface.
         */
        struct GpioConfig {
            uint8_t lowDirMask  = 0x00u; ///< ADBUS direction  (1=output, 0=input)
            uint8_t lowValue    = 0x00u; ///< ADBUS initial output levels
            uint8_t highDirMask = 0x00u; ///< ACBUS direction
            uint8_t highValue   = 0x00u; ///< ACBUS initial output levels
        };

        FT232HGPIO() = default;

        explicit FT232HGPIO(const GpioConfig& config, uint8_t u8DeviceIndex = 0u)
        {
            this->open(config, u8DeviceIndex);
        }

        ~FT232HGPIO() override { close(); }

        /**
         * @brief Open the FT232H and configure MPSSE for GPIO
         *
         * @param config        Pin configuration (directions, initial values)
         * @param u8DeviceIndex Physical device index
         */
        Status open(const GpioConfig& config, uint8_t u8DeviceIndex = 0u);

        Status close() override;
        bool is_open() const { return FT232HBase::is_open(); }

        // ── Direction control ────────────────────────────────────────────────
        Status set_direction(Bank bank, uint8_t dirMask, uint8_t initialValue = 0x00u);

        // ── Output control ───────────────────────────────────────────────────
        Status write(Bank bank, uint8_t value);
        Status set_pins  (Bank bank, uint8_t pinMask);
        Status clear_pins(Bank bank, uint8_t pinMask);
        Status toggle_pins(Bank bank, uint8_t pinMask);

        // ── Input reading ────────────────────────────────────────────────────
        Status read(Bank bank, uint8_t& value);
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

#endif // U_FT232H_GPIO_DRIVER_H
