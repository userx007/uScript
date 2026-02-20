#ifndef U_CH347_GPIO_DRIVER_H
#define U_CH347_GPIO_DRIVER_H

/**
 * @file uCH347Gpio.hpp
 * @brief CH347 GPIO driver – wraps CH347GPIO_* C API behind the ICommDriver
 *        interface and adds IRQ support.
 *
 * The CH347 exposes up to 8 GPIO pins (GPIO0–GPIO7), each independently
 * configurable as input or output.
 *
 * Buffer layout convention
 * ========================
 * Because GPIO is inherently register-like rather than a byte stream, the
 * ICommDriver buffer is given a well-defined 3-byte layout:
 *
 *   Byte 0 – enable mask   : bit N = 1 → pin N is managed by this call
 *   Byte 1 – direction mask: bit N = 1 → pin N is an output
 *   Byte 2 – data          : bit N = level for output pins; on reads this
 *                            byte is filled with the current input levels
 *
 * tout_write  : Applies enable/direction/data from buffer[0..2].
 *               Returns WriteResult { status, 3 }.
 *
 * tout_read   : Snapshots current pin directions and levels.
 *               buffer[0] filled with iDir  bitmask (output = 1).
 *               buffer[1] filled with iData bitmask (high = 1).
 *               buffer must be at least 2 bytes.
 *               ReadMode::UntilDelimiter / UntilToken → Status::NotSupported.
 *
 * For fine-grained single-pin control use the non-virtual helper API below.
 */

#include "ICommDriver.hpp"
#include "ch347_lib.h"

#include <string>
#include <span>
#include <cstdint>
#include <functional>

// ---------------------------------------------------------------------------
// GPIO pin identifiers
// ---------------------------------------------------------------------------

/** Bitmask constants for individual GPIO pins. */
enum GpioPin : uint8_t {
    GPIO_PIN_0 = (1u << 0),
    GPIO_PIN_1 = (1u << 1),
    GPIO_PIN_2 = (1u << 2),
    GPIO_PIN_3 = (1u << 3),
    GPIO_PIN_4 = (1u << 4),
    GPIO_PIN_5 = (1u << 5),
    GPIO_PIN_6 = (1u << 6),
    GPIO_PIN_7 = (1u << 7),
    GPIO_ALL   = 0xFF,
};

/** Edge type for interrupt configuration. */
enum class GpioIrqEdge : uint8_t {
    None    = IRQ_TYPE_NONE,         /**< No interrupt (disable) */
    Rising  = IRQ_TYPE_EDGE_RISING,
    Falling = IRQ_TYPE_EDGE_FALLING,
    Both    = IRQ_TYPE_EDGE_BOTH,
};

/** Callback type invoked from the CH347 ISR thread. */
using GpioIrqHandler = std::function<void(uint8_t pinIndex)>;

// ---------------------------------------------------------------------------

class CH347GPIO : public ICommDriver
{
public:
    // -----------------------------------------------------------------------
    // Constants
    // -----------------------------------------------------------------------

    static constexpr size_t  GPIO_BUFFER_SIZE           = 3;    /**< Bytes in write buffer */
    static constexpr size_t  GPIO_READ_BUFFER_SIZE      = 2;    /**< Bytes in read  buffer */
    static constexpr uint32_t GPIO_READ_DEFAULT_TIMEOUT = 1000; /**< ms */
    static constexpr uint32_t GPIO_WRITE_DEFAULT_TIMEOUT= 1000; /**< ms */

    static constexpr uint8_t BUF_IDX_ENABLE = 0; /**< Enable mask index  */
    static constexpr uint8_t BUF_IDX_DIR    = 1; /**< Direction mask index */
    static constexpr uint8_t BUF_IDX_DATA   = 2; /**< Data mask index     */

    // -----------------------------------------------------------------------
    // Construction / destruction
    // -----------------------------------------------------------------------

    CH347GPIO() = default;

    /**
     * @brief Construct and immediately open the GPIO interface.
     *
     * @param strDevice  Device path, e.g. "/dev/ch34xpis0"
     */
    explicit CH347GPIO(const std::string& strDevice) : m_iHandle(-1)
    {
        open(strDevice);
    }

    virtual ~CH347GPIO() { close(); }

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    Status open(const std::string& strDevice);
    Status close();
    bool   is_open() const override;

    // -----------------------------------------------------------------------
    // ICommDriver interface
    // -----------------------------------------------------------------------

    /**
     * @brief Read current GPIO directions and levels.
     *
     * @param u32ReadTimeout  Unused (GPIO reads are synchronous USB commands).
     * @param buffer          Must be ≥ 2 bytes.
     *                          buffer[0] ← direction bitmask (1 = output)
     *                          buffer[1] ← data bitmask      (1 = high)
     * @param options         ReadMode::Exact only.
     * @return ReadResult { status, 2, false }
     */
    ReadResult tout_read(uint32_t u32ReadTimeout,
                         std::span<uint8_t>  buffer,
                         const ReadOptions&  options) const override;

    /**
     * @brief Set GPIO pin directions and output levels.
     *
     * @param u32WriteTimeout Unused.
     * @param buffer          Must be exactly 3 bytes:
     *                          buffer[0] = enable mask  (GpioPin bitmask)
     *                          buffer[1] = direction    (1 = output)
     *                          buffer[2] = data         (1 = high)
     * @return WriteResult { status, 3 }
     */
    WriteResult tout_write(uint32_t u32WriteTimeout,
                           std::span<const uint8_t> buffer) const override;

    // -----------------------------------------------------------------------
    // Single-pin helpers (non-virtual, preferred for application code)
    // -----------------------------------------------------------------------

    /**
     * @brief Configure one pin as output and drive it to a level.
     *
     * @param pin    GpioPin bitmask (single bit)
     * @param level  true = high, false = low
     */
    Status pin_write(uint8_t pin, bool level) const;

    /**
     * @brief Read the current level of one or more input pins.
     *
     * @param pinMask  GpioPin bitmask of pins to read
     * @param level    Receives the raw data bitmask (masked by pinMask)
     */
    Status pin_read(uint8_t pinMask, uint8_t& level) const;

    /**
     * @brief Set the direction of one or more pins without changing levels.
     *
     * @param pinMask   Pins to configure
     * @param isOutput  true = output, false = input
     */
    Status pin_set_direction(uint8_t pinMask, bool isOutput) const;

    /**
     * @brief Drive multiple output pins simultaneously.
     *
     * @param pinMask   Pins to update (must already be configured as outputs)
     * @param levelMask Desired levels for each selected pin
     */
    Status pins_write(uint8_t pinMask, uint8_t levelMask) const;

    // -----------------------------------------------------------------------
    // Interrupt / IRQ helpers
    // -----------------------------------------------------------------------

    /**
     * @brief Configure and enable a GPIO interrupt.
     *
     * Valid GPIO indices for IRQ: 0, 2, 3, 4, 5, 6, 7 (index 1 is reserved).
     *
     * @param pinIndex   0-based pin number
     * @param edge       Trigger edge(s)
     * @param handler    C-style function pointer (required by CH347 API).
     *                   For C++ callbacks wrap the call-site in a static lambda
     *                   or free function and use the companion irq_set_cpp()
     *                   helper below.
     * @return Status
     */
    Status irq_set(uint8_t pinIndex, GpioIrqEdge edge, void* handler) const;

    /**
     * @brief Disable a previously configured GPIO interrupt.
     * @param pinIndex  0-based pin number
     */
    Status irq_disable(uint8_t pinIndex) const;

private:
    int m_iHandle = -1;
};

#endif // U_CH347_GPIO_DRIVER_H
