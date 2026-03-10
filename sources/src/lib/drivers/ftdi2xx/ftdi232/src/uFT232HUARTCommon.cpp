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

