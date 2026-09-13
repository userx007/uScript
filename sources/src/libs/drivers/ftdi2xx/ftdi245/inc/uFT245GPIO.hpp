#ifndef U_FT245_GPIO_DRIVER_H
#define U_FT245_GPIO_DRIVER_H

#include "FT245Base.hpp"

#include <cstdint>

/**
 * @brief FT245 GPIO bit-bang driver
 *
 * Provides direct control of all 8 data pins (D0–D7) of the FT245 via
 * asynchronous bit-bang mode (BITMODE_BITBANG = 0x01).
 *
 * In bit-bang mode the FT245 data bus is driven directly as a byte-wide
 * GPIO port.  The FIFO interface is not available while bit-bang is active.
 *
 * ── Pin assignment ────────────────────────────────────────────────────────────
 *
 *   D0–D7  — 8 GPIO pins.  Direction is set per-pin via the direction mask:
 *             1 = output, 0 = input.
 *
 * ── Variant notes ────────────────────────────────────────────────────────────
 *
 *   FT245BM: bit-bang mode available.
 *   FT245R:  bit-bang mode available and additionally supports CBUS bit-bang
 *            (not covered by this driver; available via FT_SetBitMode 0x20).
 *
 * ── Coexistence with FIFO ─────────────────────────────────────────────────────
 *
 *   Bit-bang and FIFO modes are mutually exclusive on a single FT245 device.
 *   To switch back to FIFO operation, close this driver and open an FT245Sync
 *   instance on the same device index.
 */
class FT245GPIO : public FT245Base
{
    public:

        using Status = ICommDriver::Status;

        /**
         * @brief Full GPIO configuration
         *
         * Direction mask: 1 = output, 0 = input.
         * Defaults: all pins as inputs (safe on open).
         */
        struct GpioConfig {
            uint8_t dirMask      = 0x00u; ///< D0–D7 direction (1=out, 0=in)
            uint8_t initialValue = 0x00u; ///< Initial output levels
            Variant variant      = Variant::FT245BM;
        };

        FT245GPIO() = default;

        explicit FT245GPIO(const GpioConfig& config, uint8_t u8DeviceIndex = 0u)
        {
            this->open(config, u8DeviceIndex);
        }

        ~FT245GPIO() override { close(); }

        /**
         * @brief Open the FT245 in bit-bang GPIO mode
         *
         * @param config        Direction and initial value for D0–D7
         * @param u8DeviceIndex Physical device index (0 = first chip found)
         */
        Status open(const GpioConfig& config, uint8_t u8DeviceIndex = 0u);

        /** @copydoc FT245Base::close — drives all output pins low before closing */
        Status close() override;

        bool is_open() const { return FT245Base::is_open(); }

        // ── Direction control ─────────────────────────────────────────────────
        /**
         * @brief Set the direction of all 8 GPIO pins
         *
         * @param dirMask      1 = output, 0 = input (per-pin)
         * @param initialValue Output level for pins newly becoming outputs
         */
        Status set_direction(uint8_t dirMask, uint8_t initialValue = 0x00u);

        // ── Output control ────────────────────────────────────────────────────
        /** Write a full byte to the output pins (masked by direction) */
        Status write     (uint8_t value);
        /** Assert (set high) selected output pins */
        Status set_pins  (uint8_t pinMask);
        /** Deassert (set low) selected output pins */
        Status clear_pins(uint8_t pinMask);
        /** Toggle selected output pins */
        Status toggle_pins(uint8_t pinMask);

        // ── Input reading ─────────────────────────────────────────────────────
        /**
         * @brief Read the instantaneous level of all 8 pins
         *
         * Sampled via FT_GetBitMode / ftdi_read_pins.
         * Input pins reflect the external signal; output pins reflect the
         * last written value.
         */
        Status read     (uint8_t& value);
        /** Read (rawValue & pinMask) into value */
        Status read_pins(uint8_t pinMask, uint8_t& value);

    private:

        uint8_t m_value  = 0x00u; ///< Last written output byte
        uint8_t m_dirMask = 0x00u; ///< Current direction mask

        Status apply(uint8_t value, uint8_t dir) const;
};

#endif // U_FT245_GPIO_DRIVER_H
