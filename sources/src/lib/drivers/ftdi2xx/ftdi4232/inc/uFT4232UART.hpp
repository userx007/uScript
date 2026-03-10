#ifndef U_FT4232_UART_DRIVER_H
#define U_FT4232_UART_DRIVER_H

#include "FT4232Base.hpp"
#include "ICommDriver.hpp"

#include <cstdint>
#include <span>

/**
 * @brief FT4232H async UART driver (channels C and D)
 *
 * Implements ICommDriver's unified read/write interface for the two
 * async UART-only channels of the FT4232H (channels C and D).
 *
 * Unlike FT4232SPI / FT4232I2C / FT4232GPIO, this driver does NOT
 * inherit from FT4232Base because channels C and D have no MPSSE
 * engine and are accessed via the D2XX / libftdi serial interface
 * rather than the MPSSE command processor.
 *
 * ── Channel mapping ──────────────────────────────────────────────────────────
 *
 *   Channel::C  →  interface index 2  (zero-based among all 4 FT4232H interfaces)
 *   Channel::D  →  interface index 3
 *
 * ── UART configuration ───────────────────────────────────────────────────────
 *
 *   Baud rate  : any value supported by the FT4232H (e.g. 9600 – 3 000 000)
 *   Data bits  : 7 or 8
 *   Stop bits  : 1, 1.5 (encoded as 0/1/2 in the D2XX API), or 2
 *   Parity     : none / odd / even / mark / space  (0–4)
 *   Flow ctrl  : none or RTS/CTS hardware flow control
 *
 * ── ICommDriver mapping ──────────────────────────────────────────────────────
 *
 *   tout_write → FT_Write (blocking up to u32WriteTimeout ms)
 *   tout_read  → FT_Read  (blocking up to u32ReadTimeout ms)
 *                ReadMode::Exact          : fills buffer exactly
 *                ReadMode::UntilDelimiter : reads until delimiter byte found
 *                ReadMode::UntilToken     : reads until byte sequence found (KMP)
 *
 * @note  Only channels C and D support async UART.
 *        Passing Channel::A or Channel::B to open() will fail with
 *        Status::INVALID_PARAM.  For MPSSE-based protocols on A/B use
 *        FT4232SPI, FT4232I2C, or FT4232GPIO.
 */
class FT4232UART : public ICommDriver
{
public:

    using Status = ICommDriver::Status;

    // ── Timeouts ─────────────────────────────────────────────────────────
    static constexpr uint32_t FT4232_UART_READ_DEFAULT_TIMEOUT  = 1000u; ///< ms
    static constexpr uint32_t FT4232_UART_WRITE_DEFAULT_TIMEOUT = 1000u; ///< ms

    // ── UART bus configuration ────────────────────────────────────────────
    /**
     * @brief Complete UART channel configuration
     *
     * Pass this to open() to set all parameters in one call.
     * Defaults produce a safe, widely-compatible 115200-8N1 configuration.
     *
     * stopBits encoding (mirrors the D2XX FT_SetDataCharacteristics values):
     *   0 = 1 stop bit
     *   1 = 1.5 stop bits
     *   2 = 2 stop bits
     *
     * parity encoding (mirrors D2XX FT_SetDataCharacteristics):
     *   0 = none
     *   1 = odd
     *   2 = even
     *   3 = mark
     *   4 = space
     */
    struct UartConfig {
        uint32_t baudRate   {115200u};   ///< Baud rate in bps
        uint8_t  dataBits   {8u};        ///< Data bits per frame (7 or 8)
        uint8_t  stopBits   {0u};        ///< 0=1bit, 1=1.5bits, 2=2bits
        uint8_t  parity     {0u};        ///< 0=none 1=odd 2=even 3=mark 4=space
        bool                hwFlowCtrl {false};      ///< true = enable RTS/CTS hardware flow control
        FT4232Base::Channel channel    {FT4232Base::Channel::C}; ///< Async UART channel (C or D)
    };

    FT4232UART() = default;

    /**
     * @brief Construct and immediately open the device
     * @param config        Full UART channel configuration
     * @param u8DeviceIndex Zero-based index when multiple FT4232H chips are connected
     */
    explicit FT4232UART(const UartConfig& config, uint8_t u8DeviceIndex = 0u)
    {
        this->open(config, u8DeviceIndex);
    }

    ~FT4232UART() override { close(); }

    // Non-copyable
    FT4232UART(const FT4232UART&)            = delete;
    FT4232UART& operator=(const FT4232UART&) = delete;

    /**
     * @brief Open the FT4232H channel and configure for async UART
     *
     * Rejects Channel::A and Channel::B with Status::INVALID_PARAM.
     *
     * @param config        UART bus parameters
     * @param u8DeviceIndex Physical device index (0 = first FT4232H found)
     * @return Status::SUCCESS on success, or an error code
     */
    Status open(const UartConfig& config, uint8_t u8DeviceIndex = 0u);

    /**
     * @brief Close the channel handle
     *
     * Safe to call more than once.
     */
    Status close();

    /**
     * @brief True if the channel handle is open and ready
     */
    bool is_open() const override;

    /**
     * @brief Reconfigure an already-open channel
     *
     * Updates baud rate, framing, and flow control without closing and
     * reopening the handle.  The channel field inside config is ignored
     * (use close() + open() to switch channels).
     *
     * @param config  New UART parameters (channel field ignored)
     * @return Status::SUCCESS on success, or an error code
     */
    Status configure(const UartConfig& config);

    /**
     * @brief Change baud rate on an already-open channel
     *
     * Convenience wrapper around configure() when only the baud rate
     * needs to be updated.
     *
     * @param baudRate  New baud rate in bps
     * @return Status::SUCCESS on success, or an error code
     */
    Status set_baud(uint32_t baudRate);

    /**
     * @brief Unified write interface  (implements ICommDriver)
     *
     * Writes buffer.size() bytes, blocking until complete or timeout.
     *
     * @param u32WriteTimeout  ms (0 = FT4232_UART_WRITE_DEFAULT_TIMEOUT)
     * @param buffer           Data to send
     * @return WriteResult with status and bytes written
     */
    WriteResult tout_write(uint32_t u32WriteTimeout,
                           std::span<const uint8_t> buffer) const override;

    /**
     * @brief Unified read interface  (implements ICommDriver)
     *
     * Supports all three ReadMode values:
     *   - Exact          : fills buffer exactly (up to buffer.size() bytes)
     *   - UntilDelimiter : reads until options.delimiter found
     *   - UntilToken     : KMP search for options.token sequence
     *
     * @param u32ReadTimeout  ms (0 = FT4232_UART_READ_DEFAULT_TIMEOUT)
     * @param buffer          Receive buffer
     * @param options         Read configuration (mode, delimiter, token)
     * @return ReadResult with status, bytes read, and terminator flag
     */
    ReadResult tout_read(uint32_t u32ReadTimeout,
                         std::span<uint8_t> buffer,
                         const ReadOptions& options) const override;

private:

    // ── Platform device handle ────────────────────────────────────────────
    //
    // Stored as void* to keep D2XX / libftdi headers out of this header.
    // Each platform .cpp casts to:
    //   Linux   : struct ftdi_context*
    //   Windows : FT_HANDLE  (which is itself a void*)
    //
    // nullptr means channel is not open.
    //
    void*      m_hDevice = nullptr;

    // ── Stored configuration ──────────────────────────────────────────────
    UartConfig m_config;

    // ── Platform-specific helpers (uFT4232UARTCommon.cpp) ─────────────────

    /**
     * @brief Open the D2XX / libftdi handle for the requested channel
     *
     * @param channel       Channel::C or Channel::D
     * @param u8DeviceIndex Physical device index
     */
    Status open_device(FT4232Base::Channel channel, uint8_t u8DeviceIndex);

    /**
     * @brief Apply baud rate, framing, and flow control to an open handle
     *
     * @param config  Parameters to apply (channel field ignored)
     */
    Status apply_config(const UartConfig& config) const;
};

#endif // U_FT4232_UART_DRIVER_H
