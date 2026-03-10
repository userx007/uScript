#ifndef U_FT2232_UART_DRIVER_H
#define U_FT2232_UART_DRIVER_H

#include "FT2232Base.hpp"
#include "ICommDriver.hpp"

#include <cstdint>
#include <span>

/**
 * @brief FT2232 async UART driver
 *
 * Implements ICommDriver's unified read/write interface for the async
 * serial channel of the FT2232 family.
 *
 * ── Hardware support ─────────────────────────────────────────────────────────
 *
 *   FT2232D (Variant::FT2232D, PID 0x6001):
 *     Channel A → MPSSE (I²C, SPI, GPIO)
 *     Channel B → async serial UART   ← THIS DRIVER
 *
 *   FT2232H (Variant::FT2232H, PID 0x6010):
 *     Both channels are MPSSE — no dedicated async UART channel.
 *     Passing Variant::FT2232H to open() returns INVALID_PARAM.
 *
 * ── ICommDriver mapping ──────────────────────────────────────────────────────
 *
 *   tout_write → blocking write up to u32WriteTimeout ms
 *   tout_read  → blocking read  up to u32ReadTimeout  ms
 *                ReadMode::Exact / UntilDelimiter / UntilToken all supported
 *
 * @note  Does NOT inherit from FT2232Base — channel B on FT2232D is accessed
 *        through the D2XX / libftdi serial interface, not the MPSSE engine.
 */
class FT2232UART : public ICommDriver
{
public:

    using Status = ICommDriver::Status;

    // ── Timeouts ─────────────────────────────────────────────────────────
    static constexpr uint32_t FT2232_UART_READ_DEFAULT_TIMEOUT  = 1000u; ///< ms
    static constexpr uint32_t FT2232_UART_WRITE_DEFAULT_TIMEOUT = 1000u; ///< ms

    // ── UART bus configuration ────────────────────────────────────────────
    /**
     * @brief Complete UART channel configuration
     *
     * Only FT2232D is a valid variant; FT2232H has no async UART channel.
     *
     * stopBits encoding (mirrors D2XX FT_SetDataCharacteristics):
     *   0 = 1 stop bit  |  1 = 1.5 stop bits  |  2 = 2 stop bits
     *
     * parity encoding:
     *   0 = none  |  1 = odd  |  2 = even  |  3 = mark  |  4 = space
     */
    struct UartConfig {
        uint32_t            baudRate   {115200u};              ///< Baud rate in bps
        uint8_t             dataBits   {8u};                   ///< Data bits (7 or 8)
        uint8_t             stopBits   {0u};                   ///< 0=1bit 1=1.5bits 2=2bits
        uint8_t             parity     {0u};                   ///< 0=none 1=odd 2=even 3=mark 4=space
        bool                hwFlowCtrl {false};                ///< true = RTS/CTS hardware flow
        FT2232Base::Variant variant    {FT2232Base::Variant::FT2232D}; ///< Must be FT2232D
    };

    FT2232UART() = default;

    /**
     * @brief Construct and immediately open the device
     * @param config        Full UART channel configuration (variant must be FT2232D)
     * @param u8DeviceIndex Zero-based index when multiple chips are connected
     */
    explicit FT2232UART(const UartConfig& config, uint8_t u8DeviceIndex = 0u)
    {
        this->open(config, u8DeviceIndex);
    }

    ~FT2232UART() override { close(); }

    // Non-copyable
    FT2232UART(const FT2232UART&)            = delete;
    FT2232UART& operator=(const FT2232UART&) = delete;

    /**
     * @brief Open the FT2232D channel B and configure for async UART
     *
     * Returns INVALID_PARAM if config.variant is FT2232H (no async UART
     * channel exists on that variant).
     *
     * @param config        UART bus parameters
     * @param u8DeviceIndex Physical device index (0 = first chip found)
     */
    Status open(const UartConfig& config, uint8_t u8DeviceIndex = 0u);

    /**
     * @brief Close the channel handle (safe to call more than once)
     */
    Status close();

    bool is_open() const override;

    /**
     * @brief Reconfigure an already-open channel without closing it
     *
     * The variant field is ignored (use close() + open() to change chips).
     */
    Status configure(const UartConfig& config);

    /**
     * @brief Change baud rate on an already-open channel
     */
    Status set_baud(uint32_t baudRate);

    /**
     * @brief Blocking write  (implements ICommDriver)
     * @param u32WriteTimeout ms (0 = FT2232_UART_WRITE_DEFAULT_TIMEOUT)
     */
    WriteResult tout_write(uint32_t u32WriteTimeout,
                           std::span<const uint8_t> buffer) const override;

    /**
     * @brief Blocking read  (implements ICommDriver)
     *
     * Supports ReadMode::Exact, UntilDelimiter, and UntilToken.
     *
     * @param u32ReadTimeout ms (0 = FT2232_UART_READ_DEFAULT_TIMEOUT)
     */
    ReadResult tout_read(uint32_t u32ReadTimeout,
                         std::span<uint8_t> buffer,
                         const ReadOptions& options) const override;

private:

    // Platform handle — void* keeps D2XX / libftdi headers out of this header.
    //   Linux   : struct ftdi_context*
    //   Windows : FT_HANDLE
    // nullptr = channel not open.
    void*      m_hDevice = nullptr;

    UartConfig m_config;

    // Platform helpers (uFT2232UARTCommon.cpp + platform .cpp files)
    Status open_device(FT2232Base::Variant variant, uint8_t u8DeviceIndex);
    Status apply_config(const UartConfig& config) const;
};

#endif // U_FT2232_UART_DRIVER_H
