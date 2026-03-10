#ifndef U_FT232H_UART_DRIVER_H
#define U_FT232H_UART_DRIVER_H

#include "FT232HBase.hpp"
#include "ICommDriver.hpp"

#include <cstdint>
#include <span>

/**
 * @brief FT232H async UART driver
 *
 * Implements ICommDriver's unified read/write interface for the FT232H
 * operating in async serial (VCP) mode rather than MPSSE mode.
 *
 * ── Hardware notes ───────────────────────────────────────────────────────────
 *
 *   The FT232H has a single USB interface.  That interface can operate as
 *   either an MPSSE engine (for SPI / I²C / GPIO via FT232HSPI, FT232HI2C,
 *   FT232HGPIO) or as an async serial UART.  The two modes are mutually
 *   exclusive on a single chip — do not use this driver and an MPSSE driver
 *   on the same physical device simultaneously.
 *
 *   On a system with multiple FT232H chips, use device=N to route each
 *   module to a different chip.
 *
 * ── ICommDriver mapping ──────────────────────────────────────────────────────
 *
 *   tout_write → blocking write up to u32WriteTimeout ms
 *   tout_read  → blocking read  up to u32ReadTimeout  ms
 *                ReadMode::Exact / UntilDelimiter / UntilToken all supported
 *
 * @note  Does NOT inherit from FT232HBase — async serial mode does not use
 *        the MPSSE engine.  The device is opened directly through the D2XX /
 *        libftdi serial interface.
 */
class FT232HUART : public ICommDriver
{
public:

    using Status = ICommDriver::Status;

    // ── Timeouts ─────────────────────────────────────────────────────────
    static constexpr uint32_t FT232H_UART_READ_DEFAULT_TIMEOUT  = 1000u; ///< ms
    static constexpr uint32_t FT232H_UART_WRITE_DEFAULT_TIMEOUT = 1000u; ///< ms

    // ── UART bus configuration ────────────────────────────────────────────
    /**
     * @brief Complete UART configuration for the FT232H
     *
     * No channel or variant field — the FT232H is a single-interface chip.
     *
     * stopBits encoding (mirrors D2XX FT_SetDataCharacteristics):
     *   0 = 1 stop bit  |  1 = 1.5 stop bits  |  2 = 2 stop bits
     *
     * parity encoding:
     *   0 = none  |  1 = odd  |  2 = even  |  3 = mark  |  4 = space
     */
    struct UartConfig {
        uint32_t baudRate   {115200u};  ///< Baud rate in bps
        uint8_t  dataBits   {8u};       ///< Data bits (7 or 8)
        uint8_t  stopBits   {0u};       ///< 0=1bit 1=1.5bits 2=2bits
        uint8_t  parity     {0u};       ///< 0=none 1=odd 2=even 3=mark 4=space
        bool     hwFlowCtrl {false};    ///< true = RTS/CTS hardware flow control
    };

    FT232HUART() = default;

    /**
     * @brief Construct and immediately open the device
     * @param config        Full UART configuration
     * @param u8DeviceIndex Zero-based index when multiple FT232H chips are connected
     */
    explicit FT232HUART(const UartConfig& config, uint8_t u8DeviceIndex = 0u)
    {
        this->open(config, u8DeviceIndex);
    }

    ~FT232HUART() override { close(); }

    // Non-copyable
    FT232HUART(const FT232HUART&)            = delete;
    FT232HUART& operator=(const FT232HUART&) = delete;

    /**
     * @brief Open the FT232H and configure for async UART
     *
     * @param config        UART parameters
     * @param u8DeviceIndex Physical device index (0 = first FT232H found)
     */
    Status open(const UartConfig& config, uint8_t u8DeviceIndex = 0u);

    /**
     * @brief Close the device handle (safe to call more than once)
     */
    Status close();

    bool is_open() const override;

    /**
     * @brief Reconfigure an already-open device without closing it
     */
    Status configure(const UartConfig& config);

    /**
     * @brief Change baud rate on an already-open device
     */
    Status set_baud(uint32_t baudRate);

    /**
     * @brief Blocking write  (implements ICommDriver)
     * @param u32WriteTimeout ms (0 = FT232H_UART_WRITE_DEFAULT_TIMEOUT)
     */
    WriteResult tout_write(uint32_t u32WriteTimeout,
                           std::span<const uint8_t> buffer) const override;

    /**
     * @brief Blocking read  (implements ICommDriver)
     *
     * Supports ReadMode::Exact, UntilDelimiter, and UntilToken.
     *
     * @param u32ReadTimeout ms (0 = FT232H_UART_READ_DEFAULT_TIMEOUT)
     */
    ReadResult tout_read(uint32_t u32ReadTimeout,
                         std::span<uint8_t> buffer,
                         const ReadOptions& options) const override;

private:

    // Platform handle — void* keeps D2XX / libftdi headers out of this header.
    //   Linux   : struct ftdi_context*
    //   Windows : FT_HANDLE
    // nullptr = device not open.
    void*      m_hDevice = nullptr;

    UartConfig m_config;

    // Platform helpers (uFT232HUARTCommon.cpp + platform .cpp files)
    Status open_device(uint8_t u8DeviceIndex);
    Status apply_config(const UartConfig& config) const;
};

#endif // U_FT232H_UART_DRIVER_H
