/*
 * FT2232 async UART driver — Linux platform implementation
 *
 * Implements all five platform hooks for FT2232UART using libftdi1:
 *
 *   open_device   — opens FT2232D channel B as a libftdi async serial port
 *   apply_config  — pushes baud / data / flow-control settings
 *   close         — releases the ftdi_context
 *   tout_write    — blocking write with timeout
 *   tout_read     — blocking read (Exact / UntilDelimiter / UntilToken)
 *
 * Channel B on FT2232D is a plain async serial port — no MPSSE engine.
 * libftdi1 is opened with INTERFACE_B and ftdi_set_bitmode is NOT called
 * (BITMODE_RESET leaves the channel in async serial mode).
 */

#include "uFT2232UART.hpp"
#include "FT2232Base.hpp"   // FT2232_VID / FT2232D_PID constants
#include "uLogger.hpp"

#include <ftdi.h>

#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

#define LT_HDR  "FT2232_UART|"
#define LOG_HDR LOG_STRING(LT_HDR)

#define CTX (static_cast<struct ftdi_context*>(m_hDevice))


// ============================================================================
// open_device
// ============================================================================

FT2232UART::Status FT2232UART::open_device(FT2232Base::Variant variant, uint8_t u8DeviceIndex)
{
    (void)variant;
    
    struct ftdi_context* ctx = ftdi_new();

    if (!ctx) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ftdi_new() failed (out of memory?)"));
        return Status::OUT_OF_MEMORY;
    }

    // Channel B = INTERFACE_B — the async serial channel on FT2232D
    if (ftdi_set_interface(ctx, INTERFACE_B) < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ftdi_set_interface(B) failed: ");
                  LOG_STRING(ftdi_get_error_string(ctx)));
        ftdi_free(ctx);
        return Status::PORT_ACCESS;
    }

    // Open by VID/PID and device index
    if (ftdi_usb_open_desc_index(ctx,
                                  static_cast<int>(FT2232Base::FT2232_VID),
                                  static_cast<int>(FT2232Base::FT2232D_PID),
                                  nullptr,
                                  nullptr,
                                  static_cast<unsigned int>(u8DeviceIndex)) < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ftdi_usb_open_desc_index() failed: ");
                  LOG_STRING(ftdi_get_error_string(ctx)));
        ftdi_free(ctx);
        return Status::PORT_ACCESS;
    }

    // Reset to clean state — do NOT call ftdi_set_bitmode(MPSSE);
    // leaving the channel in BITMODE_RESET keeps it in async serial mode.
    if (ftdi_usb_reset(ctx) < 0) {
        LOG_PRINT(LOG_WARNING, LOG_HDR;
                  LOG_STRING("ftdi_usb_reset() failed (non-fatal): ");
                  LOG_STRING(ftdi_get_error_string(ctx)));
    }

    if (ftdi_set_latency_timer(ctx, 1) < 0) {
        LOG_PRINT(LOG_WARNING, LOG_HDR;
                  LOG_STRING("ftdi_set_latency_timer() failed (non-fatal)"));
    }

    ftdi_read_data_set_chunksize(ctx, 65536);
    ftdi_write_data_set_chunksize(ctx, 65536);
    ftdi_tcioflush(ctx);

    m_hDevice = static_cast<void*>(ctx);

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("FT2232D UART ch-B opened, deviceIndex=");
              LOG_UINT32(u8DeviceIndex));

    return Status::SUCCESS;
}


// ============================================================================
// apply_config
// ============================================================================

FT2232UART::Status FT2232UART::apply_config(const UartConfig& config) const
{
    // ── Baud rate ─────────────────────────────────────────────────────────
    if (ftdi_set_baudrate(CTX, static_cast<int>(config.baudRate)) < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ftdi_set_baudrate() failed, baud=");
                  LOG_UINT32(config.baudRate);
                  LOG_STRING(": "); LOG_STRING(ftdi_get_error_string(CTX)));
        return Status::PORT_ACCESS;
    }

    // ── Data characteristics ──────────────────────────────────────────────
    // Map UartConfig encoding → libftdi1 enums
    //
    // dataBits: 7 → BITS_7 | 8 → BITS_8
    enum ftdi_bits_type bits;
    switch (config.dataBits) {
        case 7:  bits = BITS_7; break;
        case 8:  default: bits = BITS_8; break;
    }

    // stopBits: 0=1bit → STOP_BIT_1 | 1=1.5bits → STOP_BIT_15 | 2=2bits → STOP_BIT_2
    enum ftdi_stopbits_type stop;
    switch (config.stopBits) {
        case 1:  stop = STOP_BIT_15; break;
        case 2:  stop = STOP_BIT_2;  break;
        default: stop = STOP_BIT_1;  break;
    }

    // parity: 0=none | 1=odd | 2=even | 3=mark | 4=space
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
                  LOG_STRING("ftdi_set_line_property() failed: ");
                  LOG_STRING(ftdi_get_error_string(CTX)));
        return Status::PORT_ACCESS;
    }

    // ── Flow control ──────────────────────────────────────────────────────
    const int flow = config.hwFlowCtrl ? SIO_RTS_CTS_HS : SIO_DISABLE_FLOW_CTRL;
    if (ftdi_setflowctrl(CTX, flow) < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ftdi_setflowctrl() failed: ");
                  LOG_STRING(ftdi_get_error_string(CTX)));
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

FT2232UART::Status FT2232UART::close()
{
    if (!m_hDevice)
        return Status::SUCCESS;

    ftdi_usb_close(CTX);
    ftdi_free(CTX);
    m_hDevice = nullptr;

    LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("FT2232D UART ch-B closed"));
    return Status::SUCCESS;
}


// ============================================================================
// tout_write — blocking write with timeout
//
// libftdi1 ftdi_write_data() performs a synchronous USB bulk write
// internally, so a single call is sufficient.  The timeout is enforced
// by writing in a deadline loop to handle short writes.
// ============================================================================

FT2232UART::WriteResult FT2232UART::tout_write(uint32_t                 u32WriteTimeout,
                                                std::span<const uint8_t> buffer) const
{
    WriteResult result;

    if (!m_hDevice) { result.status = Status::PORT_ACCESS; return result; }
    if (buffer.empty()) { result.status = Status::SUCCESS; result.bytes_written = 0; return result; }

    const uint32_t timeoutMs = (u32WriteTimeout == 0u)
                                   ? FT2232_UART_WRITE_DEFAULT_TIMEOUT
                                   : u32WriteTimeout;

    const auto deadline = std::chrono::steady_clock::now()
                          + std::chrono::milliseconds(timeoutMs);

    const uint8_t* ptr       = buffer.data();
    size_t         remaining = buffer.size();

    while (remaining > 0) {
        int ret = ftdi_write_data(CTX,
                                  const_cast<uint8_t*>(ptr),
                                  static_cast<int>(remaining));
        if (ret < 0) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("ftdi_write_data() failed, ret="); LOG_INT(ret);
                      LOG_STRING(": "); LOG_STRING(ftdi_get_error_string(CTX)));
            result.status = Status::WRITE_ERROR;
            return result;
        }

        ptr                  += static_cast<size_t>(ret);
        result.bytes_written += static_cast<size_t>(ret);
        remaining            -= static_cast<size_t>(ret);

        if (remaining > 0) {
            if (std::chrono::steady_clock::now() >= deadline) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("write timeout: wanted="); LOG_UINT32(buffer.size());
                          LOG_STRING(" sent="); LOG_UINT32(result.bytes_written));
                result.status = Status::WRITE_ERROR;
                return result;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    result.status = Status::SUCCESS;
    return result;
}


// ============================================================================
// tout_read — blocking read (Exact / UntilDelimiter / UntilToken)
//
// ftdi_read_data() is non-blocking — returns however many bytes are
// available in the USB RX FIFO right now (0 if none).  All three modes
// poll in a 1 ms sleep loop until the deadline expires.
// ============================================================================

FT2232UART::ReadResult FT2232UART::tout_read(uint32_t           u32ReadTimeout,
                                              std::span<uint8_t> buffer,
                                              const ReadOptions& options) const
{
    ReadResult result;

    if (!m_hDevice) { result.status = Status::PORT_ACCESS; return result; }
    if (buffer.empty()) { result.status = Status::SUCCESS; result.bytes_read = 0; return result; }

    const uint32_t timeoutMs = (u32ReadTimeout == 0u)
                                   ? FT2232_UART_READ_DEFAULT_TIMEOUT
                                   : u32ReadTimeout;

    const auto deadline = std::chrono::steady_clock::now()
                          + std::chrono::milliseconds(timeoutMs);

    // ── Helper: read one byte with deadline check ─────────────────────────
    auto read_one = [&](uint8_t& byte) -> bool {
        while (true) {
            int ret = ftdi_read_data(CTX, &byte, 1);
            if (ret < 0) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("ftdi_read_data() error, ret="); LOG_INT(ret);
                          LOG_STRING(": "); LOG_STRING(ftdi_get_error_string(CTX)));
                result.status = Status::READ_ERROR;
                return false;
            }
            if (ret == 1) return true;
            // ret == 0: nothing available yet
            if (std::chrono::steady_clock::now() >= deadline) {
                result.status = Status::READ_TIMEOUT;
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };

    switch (options.mode) {

    // ── Exact: fill the entire buffer ─────────────────────────────────────
    case ReadMode::Exact:
    default: {
        while (result.bytes_read < buffer.size()) {
            int ret = ftdi_read_data(CTX,
                                     buffer.data() + result.bytes_read,
                                     static_cast<int>(buffer.size() - result.bytes_read));
            if (ret < 0) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("ftdi_read_data() error, ret="); LOG_INT(ret);
                          LOG_STRING(": "); LOG_STRING(ftdi_get_error_string(CTX)));
                result.status = Status::READ_ERROR;
                return result;
            }
            result.bytes_read += static_cast<size_t>(ret);

            if (result.bytes_read < buffer.size()) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR;
                              LOG_STRING("read timeout: wanted="); LOG_UINT32(buffer.size());
                              LOG_STRING(" got="); LOG_UINT32(result.bytes_read));
                    result.status = Status::READ_TIMEOUT;
                    return result;
                }
                if (ret == 0)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        result.status = Status::SUCCESS;
        break;
    }

    // ── UntilDelimiter: accumulate until delimiter byte seen ──────────────
    case ReadMode::UntilDelimiter: {
        while (result.bytes_read < buffer.size()) {
            uint8_t byte = 0;
            if (!read_one(byte)) return result;
            buffer[result.bytes_read++] = byte;
            if (byte == options.delimiter) {
                result.status = Status::SUCCESS;
                return result;
            }
        }
        result.status = Status::READ_ERROR; // buffer full before delimiter
        break;
    }

    // ── UntilToken: KMP search for byte sequence ───────────────────────────
    case ReadMode::UntilToken: {
        const auto& token = options.token;
        if (token.empty()) { result.status = Status::INVALID_PARAM; return result; }

        // Build KMP failure table
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

            while (matched > 0 && byte != token[matched])
                matched = fail[matched - 1];
            if (byte == token[matched]) ++matched;
            if (matched == token.size()) {
                result.status = Status::SUCCESS;
                return result;
            }
        }
        result.status = Status::READ_ERROR; // buffer full before token
        break;
    }

    } // switch

    return result;
}
