/*
 * FT232H async UART driver — platform-independent logic
 *
 * The FT232H single interface can be opened in async serial mode
 * (rather than MPSSE mode) via the D2XX / libftdi serial API.
 * This file owns all logic that is independent of OS and USB SDK.
 *
 * Platform-specific handle management is implemented in
 * uFT232HLinux.cpp / uFT232HWindows.cpp using the same device
 * enumeration path as the MPSSE drivers (PID 0x6014).
 */

#include "uFT232HUART.hpp"

///////////////////////////////////////////////////////////////////
//                      open / close                             //
///////////////////////////////////////////////////////////////////

FT232HUART::Status FT232HUART::open(const UartConfig& config, uint8_t u8DeviceIndex)
{
    if (m_hDevice)
        close();

    Status s = open_device(u8DeviceIndex);
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

FT232HUART::Status FT232HUART::close()
{
    if (!m_hDevice)
        return Status::SUCCESS;

    // Platform-specific close implemented in uFT232HLinux.cpp / uFT232HWindows.cpp
    // Sets m_hDevice = nullptr on success.

    return Status::SUCCESS; // Implementation fills this in
}

bool FT232HUART::is_open() const
{
    return m_hDevice != nullptr;
}

///////////////////////////////////////////////////////////////////
//                  configure / set_baud                         //
///////////////////////////////////////////////////////////////////

FT232HUART::Status FT232HUART::configure(const UartConfig& config)
{
    if (!m_hDevice)
        return Status::PORT_ACCESS;

    Status s = apply_config(config);
    if (s == Status::SUCCESS)
        m_config = config;

    return s;
}

FT232HUART::Status FT232HUART::set_baud(uint32_t baudRate)
{
    UartConfig updated = m_config;
    updated.baudRate   = baudRate;
    return configure(updated);
}

///////////////////////////////////////////////////////////////////
//                  tout_write / tout_read                       //
///////////////////////////////////////////////////////////////////

FT232HUART::WriteResult FT232HUART::tout_write(uint32_t u32WriteTimeout,
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
                                    ? FT232H_UART_WRITE_DEFAULT_TIMEOUT
                                    : u32WriteTimeout;

    // Platform-specific blocking write — implemented in
    // uFT232HLinux.cpp / uFT232HWindows.cpp.
    (void)timeoutMs;

    return result; // Implementation fills this in
}

FT232HUART::ReadResult FT232HUART::tout_read(uint32_t u32ReadTimeout,
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
                                    ? FT232H_UART_READ_DEFAULT_TIMEOUT
                                    : u32ReadTimeout;

    // Platform-specific blocking read with ReadMode dispatch —
    // implemented in uFT232HLinux.cpp / uFT232HWindows.cpp.
    //
    //   ReadMode::Exact          → read exactly buffer.size() bytes
    //   ReadMode::UntilDelimiter → byte-by-byte until options.delimiter
    //   ReadMode::UntilToken     → KMP search for options.token sequence
    //
    (void)timeoutMs;
    (void)options;

    return result; // Implementation fills this in
}
