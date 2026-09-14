#ifndef HYDRABUS_UTILS_HPP
#define HYDRABUS_UTILS_HPP

#include "Hydrabus.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <stop_token>
#include <utility>

namespace HydraHAL {
class Hydrabus;

/**
 * @brief HydraBus utility peripherals (ADC, frequency counter).
 *
 * Unlike the Protocol subclasses, Utils operates directly at BBIO level —
 * it does not enter a dedicated binary mode.  It talks to the Hydrabus
 * while still in the BBIO main mode.
 *
 * @example
 * @code
 * auto hb = std::make_shared<HydraHAL::Hydrabus>(driver);
 * hb->enter_bbio();
 *
 * HydraHAL::Utils utils(hb);
 * uint16_t adc = utils.read_adc();       // single ADC sample
 *
 * // Continuous ADC – calls your callback until it returns false
 * utils.continuous_adc([](uint16_t v) {
 *     std::cout << v << '\n';
 *     return true;  // keep going; return false to stop
 * });
 *
 * auto [freq, duty] = utils.read_frequency();
 * @endcode
 */
class Utils
{

public:
    /**
     * @param hydrabus Open, BBIO-enabled Hydrabus instance.
     */
    explicit Utils(std::shared_ptr<Hydrabus> hydrabus);

    Utils(const Utils &)            = delete;

    Utils &operator=(const Utils &) = delete;

    Utils(Utils &&)                 = default;

    Utils &operator=(Utils &&)      = default;

    ~Utils()                        = default;

    // -------------------------------------------------------------------------
    // ADC
    // -------------------------------------------------------------------------

    /**
     * @brief Read one 10-bit ADC sample.
     *
     * Returns the raw ADC value (0–1023).  The ADC is sampled on the
     * dedicated HydraBus ADC pin (PA3).
     *
     * @return ADC value, or 0 on error.
     */
    uint16_t read_adc();

    /**
     * @brief Continuously sample the ADC, invoking a callback for each value.
     *
     * The loop runs until the callback returns false, or stop_tok is
     * requested (checked between samples — if the caller never returns
     * false and stop_tok is never provided, this loop blocks on
     * Hydrabus::read() forever with no way out, so callers driving this
     * from a cancellable context should always pass a real stop_tok).
     *
     * @param callback  Called with each 10-bit ADC sample.
     *                  Return true to continue, false to stop.
     * @param stop_tok  Allows cancelling the loop between samples.
     */
    void continuous_adc(std::function<bool(uint16_t)> callback, std::stop_token stop_tok = {});

    // -------------------------------------------------------------------------
    // Frequency counter
    // -------------------------------------------------------------------------

    /**
     * @brief Read the frequency and duty-cycle of the signal on PA0.
     *
     * @return {frequency_hz, duty_cycle_raw} — both as 32-bit little-endian
     *         values as returned by the firmware.
     */
    std::pair<uint32_t, uint32_t> read_frequency();

    // -------------------------------------------------------------------------
    // Session
    // -------------------------------------------------------------------------

    /**
     * @brief Exit BBIO mode and close the serial port.
     *
     * After calling this, the Hydrabus instance should not be reused.
     */
    void close();

private:
    std::shared_ptr<Hydrabus> _hydrabus;
};

} // namespace HydraHAL

#endif // HYDRABUS_UTILS_HPP