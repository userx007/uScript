#ifndef U_CP2112_GPIO_DRIVER_H
#define U_CP2112_GPIO_DRIVER_H

#include "CP2112Base.hpp"
#include "IGpioDriver.hpp"

#include <cstdint>

/**
 * @brief CP2112 GPIO driver
 *
 * Inherits the HID foundation from CP2112Base and implements IGpioDriver's
 * configure / read / write interface over the CP2112's 8-pin GPIO port.
 *
 * Can be used independently from CP2112 (I²C) or alongside it — each
 * instance opens its own handle to the same physical device.  Both the I²C
 * and GPIO HID reports are independent, so concurrent use is safe.
 *
 * Pin mapping (AN495 §5.2):
 *   GPIO.0 — general purpose or TX LED (specialFuncMask bit 0)
 *   GPIO.1 — general purpose or interrupt output (specialFuncMask bit 1)
 *   GPIO.2 – GPIO.5 — fully general purpose
 *   GPIO.6 — general purpose or clock output (specialFuncMask bit 6)
 *   GPIO.7 — general purpose or RX LED (specialFuncMask bit 7)
 *
 * All pins are 3.3 V logic; NOT 5 V tolerant.
 */
class CP2112Gpio : public CP2112Base, public IGpioDriver
{
    public:

        // Both CP2112Base and IGpioDriver introduce a 'Status' name.
        // Explicitly pull in the one canonical definition to remove ambiguity.
        using Status = ICommDriver::Status;

        CP2112Gpio() = default;

        /**
         * @brief Construct and immediately open the device
         * @param u8DeviceIndex Zero-based index when multiple CP2112s are connected
         */
        explicit CP2112Gpio(uint8_t u8DeviceIndex)
        {
            this->open(u8DeviceIndex);
        }

        ~CP2112Gpio() override { close(); }

        /**
         * @brief Open the CP2112 HID device for GPIO use
         * @param u8DeviceIndex Which CP2112 to open (0-based)
         */
        Status open(uint8_t u8DeviceIndex = 0u);

        bool is_open() const override { return CP2112Base::is_open(); }

        /**
         * @brief Configure pin directions and drive modes
         *
         * Must be called once after open() before any gpio_read() / gpio_write().
         *
         * @param config  directionMask, pushPullMask, specialFuncMask, clockDivider
         *                See IGpioDriver::GpioConfig for full field documentation.
         */
        Status gpio_configure(const GpioConfig& config) const override;

        /**
         * @brief Drive logic levels on output pins
         *
         * @param valueMask  Desired levels — 1 = high, 0 = low
         * @param applyMask  Which pins to update — 1 = apply, 0 = leave unchanged
         *
         * Example — pulse PIN_4 low without touching other pins:
         * @code
         *   gpio.gpio_write(0,       PIN_4);   // drive low
         *   gpio.gpio_write(PIN_4,   PIN_4);   // drive high
         * @endcode
         */
        Status gpio_write(uint8_t valueMask, uint8_t applyMask) const override;

        /**
         * @brief Read current logic levels of all 8 pins
         * @param valueMask  Output bitmask — bit = 1 → high, 0 → low
         */
        Status gpio_read(uint8_t& valueMask) const override;
};

#endif // U_CP2112_GPIO_DRIVER_H
