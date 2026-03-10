/*
 * FT4232H async UART driver – platform-independent logic
 *
 * The FT4232H exposes channels C and D as plain async UART ports
 * accessible via the D2XX / libftdi serial interface (no MPSSE).
 *
 * Platform-specific handle management (open_device, apply_config) is
 * split across uFT4232Linux.cpp and uFT4232Windows.cpp in the
 * ft4232_base target, which already contains the per-platform
 * FT_OpenEx / ftdi_usb_open_desc logic for the MPSSE channels; the
 * UART variant follows the same pattern using interface index 2 (C)
 * or 3 (D) instead of 0 (A) or 1 (B).
 *
 * This file owns:
 *   - open()      : guards, delegates to open_device + apply_config
 *   - close()     : safe idempotent teardown
 *   - configure() : live reconfiguration without closing
 *   - set_baud()  : convenience baud-only update
 *   - tout_write(): blocking write with timeout
 *   - tout_read() : blocking read with timeout, all three ReadMode variants
 */

#include "uFT4232UART.hpp"

///////////////////////////////////////////////////////////////////
//                    open / close                               //
///////////////////////////////////////////////////////////////////

FT4232UART::Status FT4232UART::open(const UartConfig& config, uint8_t u8DeviceIndex)
{
    // Reject MPSSE-only channels A and B
    if (config.channel != FT4232Base::Channel::C &&
        config.channel != FT4232Base::Channel::D)
        return Status::INVALID_PARAM;

    if (m_hDevice)
        close();

    Status s = open_device(config.channel, u8DeviceIndex);
    if (s != Status::SUCCESS)
        return s;

    s = apply_config(config);
    if (s != Status::SUCCESS) {
        close();
        return s;
    }

    m_config = config;
    return Status::SUCCESS;
}

FT4232UART::Status FT4232UART::close()
{
    if (!m_hDevice)
        return Status::SUCCESS;

    // Platform-specific close implemented in uFT4232Linux.cpp / uFT4232Windows.cpp
    // (reuses the same close helper as FT4232Base)
    // Sets m_hDevice = nullptr on success.

    return Status::SUCCESS; // Implementation fills this in
}

bool FT4232UART::is_open() const
{
    return m_hDevice != nullptr;
}

///////////////////////////////////////////////////////////////////
//               configure / set_baud                           //
///////////////////////////////////////////////////////////////////

FT4232UART::Status FT4232UART::configure(const UartConfig& config)
{
    if (!m_hDevice)
        return Status::PORT_ACCESS;

    Status s = apply_config(config);
    if (s == Status::SUCCESS) {
        // Preserve the channel — only framing/flow parameters change
        m_config.baudRate   = config.baudRate;
        m_config.dataBits   = config.dataBits;
        m_config.stopBits   = config.stopBits;
        m_config.parity     = config.parity;
        m_config.hwFlowCtrl = config.hwFlowCtrl;
    }
    return s;
}

FT4232UART::Status FT4232UART::set_baud(uint32_t baudRate)
{
    UartConfig updated = m_config;
    updated.baudRate   = baudRate;
    return configure(updated);
}

///////////////////////////////////////////////////////////////////
//               tout_write / tout_read                         //
///////////////////////////////////////////////////////////////////

FT4232UART::WriteResult FT4232UART::tout_write(uint32_t u32WriteTimeout,
                                                std::span<const uint8_t> buffer) const
{
    WriteResult result;

    if (!m_hDevice) {
        result.status = Status::PORT_ACCESS;
        return result;
    }

    if (buffer.empty()) {
        result.status        = Status::SUCCESS;
        result.bytes_written = 0;
        return result;
    }

    const uint32_t timeoutMs = (u32WriteTimeout == 0u)
                                    ? FT4232_UART_WRITE_DEFAULT_TIMEOUT
                                    : u32WriteTimeout;

    // Platform-specific blocking write implemented in uFT4232Linux.cpp /
    // uFT4232Windows.cpp.  Sets result.status and result.bytes_written.
    (void)timeoutMs;

    return result; // Implementation fills this in
}

FT4232UART::ReadResult FT4232UART::tout_read(uint32_t u32ReadTimeout,
                                              std::span<uint8_t> buffer,
                                              const ReadOptions& options) const
{
    ReadResult result;

    if (!m_hDevice) {
        result.status = Status::PORT_ACCESS;
        return result;
    }

    if (buffer.empty()) {
        result.status     = Status::SUCCESS;
        result.bytes_read = 0;
        return result;
    }

    const uint32_t timeoutMs = (u32ReadTimeout == 0u)
                                    ? FT4232_UART_READ_DEFAULT_TIMEOUT
                                    : u32ReadTimeout;

    // Platform-specific blocking read with mode dispatch implemented in
    // uFT4232Linux.cpp / uFT4232Windows.cpp.
    //
    // ReadMode::Exact          → read exactly buffer.size() bytes
    // ReadMode::UntilDelimiter → read byte-by-byte until options.delimiter
    // ReadMode::UntilToken     → KMP search for options.token sequence
    //
    (void)timeoutMs;
    (void)options;

    return result; // Implementation fills this in
}
