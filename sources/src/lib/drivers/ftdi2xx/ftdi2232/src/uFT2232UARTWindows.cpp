/*
 * FT2232 async UART driver — Windows platform implementation
 *
 * Implements all five platform hooks for FT2232UART:
 *
 *   open_device   — opens FT2232D channel B as a D2XX serial port (no MPSSE)
 *   apply_config  — pushes baud / data / flow-control settings
 *   close         — releases the D2XX handle
 *   tout_write    — blocking write with timeout
 *   tout_read     — blocking read (Exact / UntilDelimiter / UntilToken)
 *
 * On FT2232D the USB device presents two interfaces in the FTD2XX list:
 *   ftIndex = chipIndex*2 + 0  →  Channel A  (MPSSE  — FT2232Base)
 *   ftIndex = chipIndex*2 + 1  →  Channel B  (async UART  — this file)
 */

#include "uFT2232UART.hpp"
#include "FT2232Base.hpp"   // FT2232_VID / FT2232D_PID constants
#include "uLogger.hpp"

#include <ftd2xx.h>

#include <algorithm>
#include <chrono>
#include <thread>

#define LT_HDR  "FT2232_UART|"
#define LOG_HDR LOG_STRING(LT_HDR)

#define FT_HDL (static_cast<FT_HANDLE>(m_hDevice))


// ============================================================================
// open_device
// ============================================================================

FT2232UART::Status FT2232UART::open_device(FT2232Base::Variant variant, uint8_t u8DeviceIndex)
{
    (void)variant;
    
    const DWORD ftIndex = static_cast<DWORD>(u8DeviceIndex) * 2u + 1u; // channel B

    // ── VID/PID verification ──────────────────────────────────────────────
    {
        DWORD     flags = 0, type = 0, devId = 0, locId = 0;
        char      serialNum[16]   = {0};
        char      description[64] = {0};
        FT_HANDLE tempHandle      = nullptr;

        if (FT_GetDeviceInfoDetail(ftIndex, &flags, &type, &devId, &locId,
                                   serialNum, description, &tempHandle) != FT_OK) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("FT_GetDeviceInfoDetail() failed, ftIndex=");
                      LOG_UINT32(ftIndex));
            return Status::PORT_ACCESS;
        }

        const uint16_t vid = static_cast<uint16_t>((devId >> 16) & 0xFFFFu);
        const uint16_t pid = static_cast<uint16_t>( devId        & 0xFFFFu);

        if (vid != FT2232Base::FT2232_VID || pid != FT2232Base::FT2232D_PID) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("unexpected VID=0x"); LOG_HEX16(vid);
                      LOG_STRING(" PID=0x"); LOG_HEX16(pid);
                      LOG_STRING(" at ftIndex="); LOG_UINT32(ftIndex));
            return Status::PORT_ACCESS;
        }
    }

    // ── Open ──────────────────────────────────────────────────────────────
    FT_HANDLE handle = nullptr;
    if (FT_Open(static_cast<int>(ftIndex), &handle) != FT_OK || !handle) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("FT_Open() failed, ftIndex="); LOG_UINT32(ftIndex));
        return Status::PORT_ACCESS;
    }

    // ── Reset / USB tuning (no FT_SetBitMode — channel B has no MPSSE) ───
    FT_ResetDevice(handle);
    FT_SetUSBParameters(handle, 65536u, 65536u);
    FT_SetLatencyTimer(handle, 1u);
    FT_Purge(handle, FT_PURGE_RX | FT_PURGE_TX);

    m_hDevice = static_cast<void*>(handle);

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("FT2232D UART ch-B opened, deviceIndex=");
              LOG_UINT32(u8DeviceIndex);
              LOG_STRING(" ftIndex="); LOG_UINT32(ftIndex));

    return Status::SUCCESS;
}


// ============================================================================
// apply_config
// ============================================================================

FT2232UART::Status FT2232UART::apply_config(const UartConfig& config) const
{
    // Baud rate
    if (FT_SetBaudRate(FT_HDL, static_cast<DWORD>(config.baudRate)) != FT_OK) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("FT_SetBaudRate() failed, baud=");
                  LOG_UINT32(config.baudRate));
        return Status::PORT_ACCESS;
    }

    // Data characteristics
    // UartConfig stopBits/parity encodings match D2XX FT_STOP_BITS_* / FT_PARITY_* values
    if (FT_SetDataCharacteristics(FT_HDL,
                                   static_cast<UCHAR>(config.dataBits),
                                   static_cast<UCHAR>(config.stopBits),
                                   static_cast<UCHAR>(config.parity)) != FT_OK) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("FT_SetDataCharacteristics() failed"));
        return Status::PORT_ACCESS;
    }

    // Flow control
    const USHORT flow = config.hwFlowCtrl ? FT_FLOW_RTS_CTS : FT_FLOW_NONE;
    if (FT_SetFlowControl(FT_HDL, flow, 0x11u, 0x13u) != FT_OK) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("FT_SetFlowControl() failed"));
        return Status::PORT_ACCESS;
    }

    // Timeouts
    FT_SetTimeouts(FT_HDL,
                   FT2232UART::FT2232_UART_READ_DEFAULT_TIMEOUT,
                   FT2232UART::FT2232_UART_WRITE_DEFAULT_TIMEOUT);

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

    FT_Close(FT_HDL);
    m_hDevice = nullptr;

    LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("FT2232D UART ch-B closed"));
    return Status::SUCCESS;
}


// ============================================================================
// tout_write — blocking write with timeout
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

    FT_SetTimeouts(FT_HDL, FT2232_UART_READ_DEFAULT_TIMEOUT, timeoutMs);

    DWORD     written = 0;
    FT_STATUS ftStat  = FT_Write(FT_HDL,
                                  const_cast<LPVOID>(static_cast<const void*>(buffer.data())),
                                  static_cast<DWORD>(buffer.size()),
                                  &written);

    result.bytes_written = static_cast<size_t>(written);

    if (ftStat != FT_OK) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("FT_Write() failed, status="); LOG_UINT32(ftStat));
        result.status = Status::WRITE_ERROR;
        return result;
    }
    if (written != static_cast<DWORD>(buffer.size())) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("FT_Write() short write, wanted="); LOG_UINT32(buffer.size());
                  LOG_STRING(" got="); LOG_UINT32(written));
        result.status = Status::WRITE_ERROR;
        return result;
    }

    result.status = Status::SUCCESS;
    return result;
}


// ============================================================================
// tout_read — blocking read (Exact / UntilDelimiter / UntilToken)
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

    FT_SetTimeouts(FT_HDL, timeoutMs, FT2232_UART_WRITE_DEFAULT_TIMEOUT);

    const auto deadline = std::chrono::steady_clock::now()
                          + std::chrono::milliseconds(timeoutMs);

    // ── Helper: read exactly one byte with deadline check ─────────────────
    auto read_one = [&](uint8_t& byte) -> bool {
        while (true) {
            DWORD queued = 0;
            if (FT_GetQueueStatus(FT_HDL, &queued) != FT_OK) {
                result.status = Status::READ_ERROR;
                return false;
            }
            if (queued > 0) {
                DWORD got = 0;
                if (FT_Read(FT_HDL, &byte, 1u, &got) != FT_OK || got == 0) {
                    result.status = Status::READ_ERROR;
                    return false;
                }
                return true;
            }
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
        size_t remaining = buffer.size();
        while (remaining > 0) {
            DWORD queued = 0;
            if (FT_GetQueueStatus(FT_HDL, &queued) != FT_OK) {
                result.status = Status::READ_ERROR;
                return result;
            }
            if (queued > 0) {
                DWORD toRead = static_cast<DWORD>(
                    std::min(static_cast<size_t>(queued), remaining));
                DWORD got = 0;
                if (FT_Read(FT_HDL,
                             buffer.data() + result.bytes_read,
                             toRead, &got) != FT_OK) {
                    result.status = Status::READ_ERROR;
                    return result;
                }
                result.bytes_read += static_cast<size_t>(got);
                remaining         -= static_cast<size_t>(got);
            } else {
                if (std::chrono::steady_clock::now() >= deadline) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR;
                              LOG_STRING("read timeout: wanted="); LOG_UINT32(buffer.size());
                              LOG_STRING(" got="); LOG_UINT32(result.bytes_read));
                    result.status = Status::READ_TIMEOUT;
                    return result;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        result.status = Status::SUCCESS;
        break;
    }

    // ── UntilDelimiter: accumulate bytes until delimiter byte seen ─────────
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
        // Buffer full before delimiter
        result.status = Status::READ_ERROR;
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
        // Buffer full before token
        result.status = Status::READ_ERROR;
        break;
    }

    } // switch

    return result;
}
