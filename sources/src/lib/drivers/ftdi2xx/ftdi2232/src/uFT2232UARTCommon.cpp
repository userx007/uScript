/*
 * FT2232 async UART driver — platform-independent logic
 *
 * The FT2232D exposes channel B as a plain async serial port accessible
 * via the D2XX / libftdi serial interface.  This file owns all the
 * logic that is independent of the OS and USB SDK.
 *
 * Platform-specific handle management (open_device, apply_config) is
 * implemented in uFT2232Linux.cpp / uFT2232Windows.cpp using interface
 * index 1 (channel B = second interface of the FT2232D).
 */

#include "uFT2232UART.hpp"

///////////////////////////////////////////////////////////////////
//                      open / close                             //
///////////////////////////////////////////////////////////////////

FT2232UART::Status FT2232UART::open(const UartConfig& config, uint8_t u8DeviceIndex)
{
    // FT2232H has no async UART channel — both A and B are MPSSE
    if (config.variant != FT2232Base::Variant::FT2232D)
        return Status::INVALID_PARAM;

    if (m_hDevice)
        close();

    Status s = open_device(config.variant, u8DeviceIndex);
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

FT2232UART::Status FT2232UART::close()
{
    if (!m_hDevice)
        return Status::SUCCESS;

    // Platform-specific close implemented in uFT2232Linux.cpp / uFT2232Windows.cpp
    // Sets m_hDevice = nullptr on success.

    return Status::SUCCESS; // Implementation fills this in
}

bool FT2232UART::is_open() const
{
    return m_hDevice != nullptr;
}

///////////////////////////////////////////////////////////////////
//                  configure / set_baud                         //
///////////////////////////////////////////////////////////////////

FT2232UART::Status FT2232UART::configure(const UartConfig& config)
{
    if (!m_hDevice)
        return Status::PORT_ACCESS;

    Status s = apply_config(config);
    if (s == Status::SUCCESS) {
        m_config.baudRate   = config.baudRate;
        m_config.dataBits   = config.dataBits;
        m_config.stopBits   = config.stopBits;
        m_config.parity     = config.parity;
        m_config.hwFlowCtrl = config.hwFlowCtrl;
        // variant is fixed at open time — not updated here
    }
    return s;
}

FT2232UART::Status FT2232UART::set_baud(uint32_t baudRate)
{
    UartConfig updated = m_config;
    updated.baudRate   = baudRate;
    return configure(updated);
}

///////////////////////////////////////////////////////////////////
//                  tout_write / tout_read                       //
///////////////////////////////////////////////////////////////////

FT2232UART::WriteResult FT2232UART::tout_write(uint32_t u32WriteTimeout,
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
                                    ? FT2232_UART_WRITE_DEFAULT_TIMEOUT
                                    : u32WriteTimeout;

    // Platform-specific blocking write — implemented in
    // uFT2232Linux.cpp / uFT2232Windows.cpp.
    (void)timeoutMs;

    return result; // Implementation fills this in
}

FT2232UART::ReadResult FT2232UART::tout_read(uint32_t u32ReadTimeout,
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
                                    ? FT2232_UART_READ_DEFAULT_TIMEOUT
                                    : u32ReadTimeout;

    // Platform-specific blocking read with ReadMode dispatch —
    // implemented in uFT2232Linux.cpp / uFT2232Windows.cpp.
    //
    //   ReadMode::Exact          → read exactly buffer.size() bytes
    //   ReadMode::UntilDelimiter → byte-by-byte until options.delimiter
    //   ReadMode::UntilToken     → KMP search for options.token sequence
    //
    (void)timeoutMs;
    (void)options;

    return result; // Implementation fills this in
}
