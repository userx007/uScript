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
#include "FT4232Base.hpp"
#include "uFT4232UART.hpp"

#include <stdint.h>

///////////////////////////////////////////////////////////////////
//                    open / close                               //
///////////////////////////////////////////////////////////////////

FT4232UART::Status FT4232UART::open(const UartConfig &sConfig, uint8_t u8DeviceIndex)
{
    // Reject MPSSE-only channels A and B
    if (sConfig.channel != FT4232Base::Channel::C &&
        sConfig.channel != FT4232Base::Channel::D) {
        return Status::INVALID_PARAM;
    }

    if (m_hDevice) {
        close();
    }

    Status s = open_device(sConfig.channel, u8DeviceIndex);
    if (s != Status::SUCCESS) {
        return s;
    }

    s = apply_config(sConfig);
    if (s != Status::SUCCESS) {
        close();
        return s;
    }

    m_config = sConfig;
    return Status::SUCCESS;
}

bool FT4232UART::is_open() const
{
    return m_hDevice != nullptr;
}

///////////////////////////////////////////////////////////////////
//               configure / set_baud                           //
///////////////////////////////////////////////////////////////////

FT4232UART::Status FT4232UART::configure(const UartConfig &sConfig)
{
    if (!m_hDevice) {
        return Status::PORT_ACCESS;
    }

    Status s = apply_config(sConfig);
    if (s == Status::SUCCESS) {
        // Preserve the channel — only framing/flow parameters change
        m_config.baudRate   = sConfig.baudRate;
        m_config.dataBits   = sConfig.dataBits;
        m_config.stopBits   = sConfig.stopBits;
        m_config.parity     = sConfig.parity;
        m_config.hwFlowCtrl = sConfig.hwFlowCtrl;
    }
    return s;
}

FT4232UART::Status FT4232UART::set_baud(uint32_t u32BaudRate)
{
    UartConfig updated = m_config;
    updated.u32BaudRate   = u32BaudRate;
    return configure(updated);
}
