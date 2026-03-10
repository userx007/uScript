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

