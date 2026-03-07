#ifndef I_GPIO_DRIVER_HPP
#define I_GPIO_DRIVER_HPP

#include "ICommDriver.hpp"
#include <cstdint>

/**
 * @brief Pure interface for GPIO pin control
 *
 * Uses ICommDriver::Status so both driver families share one status vocabulary
 * without introducing a third enum.
 */
class IGpioDriver
{
    public:

        using Status = ICommDriver::Status;

        // ── Convenience pin-mask constants ──────────────────────────────────
        static constexpr uint8_t PIN_0   = (1u << 0);
        static constexpr uint8_t PIN_1   = (1u << 1);
        static constexpr uint8_t PIN_2   = (1u << 2);
        static constexpr uint8_t PIN_3   = (1u << 3);
        static constexpr uint8_t PIN_4   = (1u << 4);
        static constexpr uint8_t PIN_5   = (1u << 5);
        static constexpr uint8_t PIN_6   = (1u << 6);
        static constexpr uint8_t PIN_7   = (1u << 7);
        static constexpr uint8_t PIN_ALL = 0xFFu;

        /**
         * @brief Per-pin configuration passed to gpio_configure()
         *
         *   directionMask   — bit = 1 → output,     0 → input
         *   pushPullMask    — bit = 1 → push-pull,   0 → open-drain
         *   specialFuncMask — bit = 1 → alternate function active
         *
         * CP2112 special-function bits (AN495 §5.2):
         *   bit 0 → GPIO.0 = TX LED
         *   bit 1 → GPIO.1 = interrupt (active-low)
         *   bit 6 → GPIO.6 = clock output  (frequency set via clockDivider)
         *   bit 7 → GPIO.7 = RX LED
         */
        struct GpioConfig {
            uint8_t directionMask   = 0x00; ///< Default: all inputs
            uint8_t pushPullMask    = 0x00; ///< Default: open-drain
            uint8_t specialFuncMask = 0x00; ///< Default: all GPIO
            uint8_t clockDivider    = 0x00; ///< Only used when GPIO.6 = clock out
        };

        virtual ~IGpioDriver() = default;

        /** @brief True if the underlying device handle is open */
        virtual bool is_open() const = 0;

        /**
         * @brief Configure pin directions and modes
         * @param config  See GpioConfig above
         */
        virtual Status gpio_configure(const GpioConfig& config) const = 0;

        /**
         * @brief Set logic levels on output pins
         *
         * @param valueMask  Desired logic levels — 1 = high, 0 = low
         * @param applyMask  Which pins to actually drive — 1 = apply, 0 = leave unchanged
         *
         * Example — set PIN_2 high and PIN_3 low without touching others:
         * @code
         *   gpio_write(PIN_2, PIN_2 | PIN_3);
         * @endcode
         */
        virtual Status gpio_write(uint8_t valueMask, uint8_t applyMask) const = 0;

        /**
         * @brief Read the current logic level of all 8 pins
         * @param valueMask  Output — bit = 1 → high, 0 → low
         */
        virtual Status gpio_read(uint8_t& valueMask) const = 0;
};

#endif // I_GPIO_DRIVER_HPP
