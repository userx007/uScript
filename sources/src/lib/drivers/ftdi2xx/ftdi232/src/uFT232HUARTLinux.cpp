/*
 * FT232H async UART driver — Linux platform implementation
 *
 * The FT232H has a single USB interface (INTERFACE_A).
 * Opened with libftdi1 in async serial mode — ftdi_set_bitmode is NOT
 * called, leaving the channel in BITMODE_RESET (async serial).
 */

#include "uFT232HUART.hpp"
#include "FT232HBase.hpp"
#include "uLogger.hpp"

#include <ftdi.h>

#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "FT232H_UART |"
#define LOG_HDR    LOG_STRING(LT_HDR)

#define CTX (static_cast<struct ftdi_context*>(m_hDevice))


// ============================================================================
// open_device
// ============================================================================

FT232HUART::Status FT232HUART::open_device(uint8_t u8DeviceIndex)
{
    struct ftdi_context* ctx = ftdi_new();
    if (!ctx) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("ftdi_new() failed"));
        return Status::OUT_OF_MEMORY;
    }

    // FT232H has a single interface — always INTERFACE_A
    if (ftdi_set_interface(ctx, INTERFACE_A) < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ftdi_set_interface() failed: ");
                  LOG_STRING(ftdi_get_error_string(ctx)));
        ftdi_free(ctx);
        return Status::PORT_ACCESS;
    }

    if (ftdi_usb_open_desc_index(ctx,
                                  static_cast<int>(FT232HBase::FT232H_VID),
                                  static_cast<int>(FT232HBase::FT232H_PID),
                                  nullptr, nullptr,
                                  static_cast<unsigned int>(u8DeviceIndex)) < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ftdi_usb_open_desc_index() failed: ");
                  LOG_STRING(ftdi_get_error_string(ctx)));
        ftdi_free(ctx);
        return Status::PORT_ACCESS;
    }

    // Do NOT call ftdi_set_bitmode(MPSSE) — leave in async serial mode
    ftdi_usb_reset(ctx);
    ftdi_set_latency_timer(ctx, 1);
    ftdi_read_data_set_chunksize(ctx, 65536);
    ftdi_write_data_set_chunksize(ctx, 65536);
    ftdi_tcioflush(ctx);

    m_hDevice = static_cast<void*>(ctx);

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("FT232H UART opened, deviceIndex="); LOG_UINT32(u8DeviceIndex));

    return Status::SUCCESS;
}


// ============================================================================
// apply_config
// ============================================================================

FT232HUART::Status FT232HUART::apply_config(const UartConfig& config) const
{
    if (ftdi_set_baudrate(CTX, static_cast<int>(config.baudRate)) < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ftdi_set_baudrate() failed: "); LOG_STRING(ftdi_get_error_string(CTX)));
        return Status::PORT_ACCESS;
    }

    enum ftdi_bits_type bits = (config.dataBits == 7) ? BITS_7 : BITS_8;

    enum ftdi_stopbits_type stop;
    switch (config.stopBits) {
        case 1:  stop = STOP_BIT_15; break;
        case 2:  stop = STOP_BIT_2;  break;
        default: stop = STOP_BIT_1;  break;
    }

    enum ftdi_parity_type parity;
    switch (config.parity) {
        case 1:  parity = ODD;   break;
        case 2:  parity = EVEN;  break;
        case 3:  parity = MARK;  break;
        case 4:  parity = SPACE; break;
        default: parity = NONE;  break;
    }

    if (ftdi_set_line_property(CTX, bits, stop, parity) < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ftdi_set_line_property() failed: "); LOG_STRING(ftdi_get_error_string(CTX)));
        return Status::PORT_ACCESS;
    }

    const int flow = config.hwFlowCtrl ? SIO_RTS_CTS_HS : SIO_DISABLE_FLOW_CTRL;
    if (ftdi_setflowctrl(CTX, flow) < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ftdi_setflowctrl() failed: "); LOG_STRING(ftdi_get_error_string(CTX)));
        return Status::PORT_ACCESS;
    }

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("UART cfg: baud=");  LOG_UINT32(config.baudRate);
              LOG_STRING(" data=");  LOG_UINT32(config.dataBits);
              LOG_STRING(" stop=");  LOG_UINT32(config.stopBits);
              LOG_STRING(" par=");   LOG_UINT32(config.parity);
              LOG_STRING(" flow=");  LOG_UINT32(config.hwFlowCtrl ? 1u : 0u));

    return Status::SUCCESS;
}


// ============================================================================
// close
// ============================================================================

FT232HUART::Status FT232HUART::close()
{
    if (!m_hDevice)
        return Status::SUCCESS;

    ftdi_usb_close(CTX);
    ftdi_free(CTX);
    m_hDevice = nullptr;

    LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("FT232H UART closed"));
    return Status::SUCCESS;
}


// ============================================================================
// tout_write
// ============================================================================

FT232HUART::WriteResult FT232HUART::tout_write(uint32_t                 u32WriteTimeout,
                                                std::span<const uint8_t> buffer) const
{
    WriteResult result;
    if (!m_hDevice) { result.status = Status::PORT_ACCESS; return result; }
    if (buffer.empty()) { result.status = Status::SUCCESS; result.bytes_written = 0; return result; }

    const uint32_t timeoutMs = u32WriteTimeout ? u32WriteTimeout : FT232H_UART_WRITE_DEFAULT_TIMEOUT;
    const auto deadline = std::chrono::steady_clock::now()
                          + std::chrono::milliseconds(timeoutMs);

    const uint8_t* ptr       = buffer.data();
    size_t         remaining = buffer.size();

    while (remaining > 0) {
        int ret = ftdi_write_data(CTX, const_cast<uint8_t*>(ptr), static_cast<int>(remaining));
        if (ret < 0) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("ftdi_write_data() failed: "); LOG_STRING(ftdi_get_error_string(CTX)));
            result.status = Status::WRITE_ERROR; return result;
        }
        ptr                  += ret;
        result.bytes_written += ret;
        remaining            -= ret;

        if (remaining > 0) {
            if (std::chrono::steady_clock::now() >= deadline) {
                result.status = Status::WRITE_ERROR; return result;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    result.status = Status::SUCCESS;
    return result;
}


// ============================================================================
// tout_read
// ============================================================================

FT232HUART::ReadResult FT232HUART::tout_read(uint32_t           u32ReadTimeout,
                                              std::span<uint8_t> buffer,
                                              const ReadOptions& options) const
{
    ReadResult result;
    if (!m_hDevice) { result.status = Status::PORT_ACCESS; return result; }
    if (buffer.empty()) { result.status = Status::SUCCESS; result.bytes_read = 0; return result; }

    const uint32_t timeoutMs = u32ReadTimeout ? u32ReadTimeout : FT232H_UART_READ_DEFAULT_TIMEOUT;
    const auto deadline = std::chrono::steady_clock::now()
                          + std::chrono::milliseconds(timeoutMs);

    auto read_one = [&](uint8_t& byte) -> bool {
        while (true) {
            int ret = ftdi_read_data(CTX, &byte, 1);
            if (ret < 0) { result.status = Status::READ_ERROR; return false; }
            if (ret == 1) return true;
            if (std::chrono::steady_clock::now() >= deadline) {
                result.status = Status::READ_TIMEOUT; return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };

    switch (options.mode) {

    case ReadMode::Exact:
    default: {
        while (result.bytes_read < buffer.size()) {
            int ret = ftdi_read_data(CTX,
                                     buffer.data() + result.bytes_read,
                                     static_cast<int>(buffer.size() - result.bytes_read));
            if (ret < 0) { result.status = Status::READ_ERROR; return result; }
            result.bytes_read += ret;
            if (result.bytes_read < buffer.size()) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    result.status = Status::READ_TIMEOUT; return result;
                }
                if (ret == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        result.status = Status::SUCCESS;
        break;
    }

    case ReadMode::UntilDelimiter: {
        while (result.bytes_read < buffer.size()) {
            uint8_t byte = 0;
            if (!read_one(byte)) return result;
            buffer[result.bytes_read++] = byte;
            if (byte == options.delimiter) { result.status = Status::SUCCESS; return result; }
        }
        result.status = Status::READ_ERROR;
        break;
    }

    case ReadMode::UntilToken: {
        const auto& token = options.token;
        if (token.empty()) { result.status = Status::INVALID_PARAM; return result; }

        std::vector<size_t> fail(token.size(), 0u);
        for (size_t i = 1; i < token.size(); ++i) {
            size_t j = fail[i - 1];
            while (j > 0 && token[i] != token[j]) j = fail[j - 1];
            if (token[i] == token[j]) ++j;
            fail[i] = j;
        }

        size_t matched = 0;
        while (result.bytes_read < buffer.size()) {
            uint8_t byte = 0;
            if (!read_one(byte)) return result;
            buffer[result.bytes_read++] = byte;
            while (matched > 0 && byte != token[matched]) matched = fail[matched - 1];
            if (byte == token[matched]) ++matched;
            if (matched == token.size()) { result.status = Status::SUCCESS; return result; }
        }
        result.status = Status::READ_ERROR;
        break;
    }

    }
    return result;
}
