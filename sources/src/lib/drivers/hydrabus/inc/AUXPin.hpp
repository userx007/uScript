#ifndef HYDRABUS_AUXPIN_HPP
#define HYDRABUS_AUXPIN_HPP


#include <cstdint>
#include <memory>

namespace HydraHAL {

class Hydrabus;   // forward declaration

/**
 * @brief Auxiliary GPIO pin controller.
 *
 * Up to 4 AUX pins (PC4–PC7 on the HydraBus hardware) can be configured
 * as input or output, with optional pull-ups.  Each Protocol instance
 * exposes an array of 4 AUXPin objects:
 *
 * @code
 * spi.aux(0).set_direction(AUXPin::Direction::Output);
 * spi.aux(0).set_value(1);
 * int v = spi.aux(1).get_value();
 * @endcode
 */
class AUXPin {
public:
    enum class Direction : uint8_t {
        Output = 0,
        Input  = 1
    };

    /**
     * @param number   Pin index 0–3.
     * @param hydrabus Shared Hydrabus instance (must outlive this object).
     */
    AUXPin(int number, std::shared_ptr<Hydrabus> hydrabus);

    // -------------------------------------------------------------------------
    // Value
    // -------------------------------------------------------------------------

    /**
     * @brief Read the current logical level of the pin.
     * @return 0 or 1; -1 on communication error.
     */
    int  get_value() const;

    /**
     * @brief Drive the pin to a logical level (pin must be configured as Output).
     * @param value 0 or 1.
     * @return true on success.
     */
    bool set_value(int value);

    /** @brief Toggle the current output level. */
    bool toggle();

    // -------------------------------------------------------------------------
    // Direction
    // -------------------------------------------------------------------------

    /**
     * @brief Read the pin's configured direction.
     */
    Direction get_direction() const;

    /**
     * @brief Set the pin direction.
     * @return true on success.
     */
    bool set_direction(Direction dir);

    // -------------------------------------------------------------------------
    // Pull-up
    // -------------------------------------------------------------------------

    /**
     * @brief Query whether the internal pull-up resistor is enabled.
     * @return 1 = enabled, 0 = disabled, -1 on error.
     */
    int  get_pullup() const;

    /**
     * @brief Enable or disable the internal pull-up.
     * @param enable 1 = enable, 0 = disable.
     * @return true on success.
     */
    bool set_pullup(int enable);

private:
    /** @brief Read raw AUX config byte from device (pullup | direction). */
    uint8_t _get_config() const;

    /** @brief Read raw AUX value byte from device. */
    uint8_t _get_values() const;

    int                        _number;
    std::shared_ptr<Hydrabus>  _hydrabus;
};

} // namespace HydraHAL

#endif // HYDRABUS_AUXPIN_HPP
