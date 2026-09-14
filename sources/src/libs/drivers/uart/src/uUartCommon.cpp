#include "uKmpMatch.hpp"
#include "uLogger.hpp"
#include "uUart.hpp"

#include <algorithm>
#include <array>
#include <mutex>
#include <span>
#include <stddef.h>
#include <stdint.h>
#include <stop_token>
#include <string_view>
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

#define LT_HDR  "UART_DRV    |"
#define LOG_HDR LOG_STRING(LT_HDR)

bool UART::is_open() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_iHandle >= 0;
}

// ============================================================================
// PUBLIC UNIFIED INTERFACE IMPLEMENTATION
// ============================================================================

UART::ReadResult UART::tout_read(uint32_t u32ReadTimeout, std::span<uint8_t> buffer,
                                 const ReadOptions &options,
                                 std::string_view /*xtra_params*/,
                                 std::stop_token stop_tok) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    ReadResult result;

    switch (options.mode) {
    case ReadMode::Exact: {
        size_t bytes_read       = 0;
        result.status           = timeout_read(u32ReadTimeout, buffer, bytes_read, stop_tok);
        result.bytes_read       = bytes_read;
        result.found_terminator = false;
        break;
    }

    case ReadMode::UntilDelimiter: {
        size_t bytes_read       = 0;
        result.status           = timeout_read_until(u32ReadTimeout, buffer, options.delimiter, bytes_read, stop_tok);
        result.bytes_read       = bytes_read;
        result.found_terminator = (result.status == Status::SUCCESS);
        (void)purge(true, false);
        break;
    }

    case ReadMode::UntilToken: {
        result.status           = timeout_wait_for_token(u32ReadTimeout, options.token, options.use_buffer, stop_tok);
        result.bytes_read       = 0; // Token search doesn't fill user buffer
        result.found_terminator = (result.status == Status::SUCCESS);
        (void)purge(true, false);
        break;
    }

    default:
        result.status           = Status::INVALID_PARAM;
        result.bytes_read       = 0;
        result.found_terminator = false;
        break;
    }

    return result;
}

UART::WriteResult UART::tout_write(uint32_t u32WriteTimeout, std::span<const uint8_t> buffer,
                                   std::string_view /*xtra_params*/,
                                   std::stop_token /*stop_tok*/) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    WriteResult result;
    size_t bytes_written = 0;

    result.status        = timeout_write(u32WriteTimeout, buffer, bytes_written);
    result.bytes_written = bytes_written;

    return result;
}

// ============================================================================
// PRIVATE LEGACY IMPLEMENTATION (INTERNAL USE ONLY)
// ============================================================================

UART::Status UART::timeout_wait_for_token(uint32_t u32ReadTimeout, std::span<const uint8_t> token, bool useBuffer,
                                          std::stop_token stop_tok) const
{
    size_t szTokenLength = token.size();
    if (token.empty() || szTokenLength == 0 || szTokenLength >= UART_MAX_BUFLENGTH) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid token or length"));
        return Status::INVALID_PARAM;
    }

    uint32_t u32Timeout   = (u32ReadTimeout == 0) ? UART_READ_DEFAULT_TIMEOUT : u32ReadTimeout;
    bool bReturnOnTimeout = (u32ReadTimeout != 0);

    std::vector<int> viLps;
    build_kmp_table(token, szTokenLength, viLps);

    return kmp_stream_match(token, viLps, u32Timeout, bReturnOnTimeout, useBuffer, stop_tok);
}

void UART::build_kmp_table(std::span<const uint8_t> pattern, size_t szLength, std::vector<int> &viLps) const
{
    ukmp::build_kmp_table(pattern, szLength, viLps);
}

UART::Status UART::kmp_stream_match(std::span<const uint8_t> token, const std::vector<int> &viLps, uint32_t u32Timeout, bool bReturnOnTimeout, bool useBuffer,
                                    std::stop_token stop_tok) const
{
    return ukmp::kmp_stream_match(
        [this, stop_tok](uint32_t timeout, std::span<uint8_t> buf, size_t &bytesRead) { return timeout_read(timeout, buf, bytesRead, stop_tok); },
        token, viLps, u32Timeout, bReturnOnTimeout, useBuffer,
        /*szChunkBufferSize=*/1, /*szRingBufferSize=*/UART_MAX_BUFLENGTH);
}

UART::Status UART::timeout_read_until(uint32_t u32ReadTimeout, std::span<uint8_t> buffer, uint8_t cDelimiter, size_t &szBytesRead,
                                      std::stop_token stop_tok) const
{
    if (buffer.size() < 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Buffer too small for delimiter + null terminator"));
        return Status::INVALID_PARAM;
    }

    constexpr size_t TEMP_BUFFER_SIZE                = 64;
    std::array<uint8_t, TEMP_BUFFER_SIZE> tempBuffer = {0};
    szBytesRead                                      = 0;
    UART::Status eResult                             = Status::RETVAL_NOT_SET;

    while (eResult == Status::RETVAL_NOT_SET) {
        size_t bytesRemaining = buffer.size() - szBytesRead - 1; // reserve space for '\0'
        if (bytesRemaining == 0) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Buffer full before delimiter found"));
            return Status::BUFFER_OVERFLOW;
        }

        size_t bytesToRead     = std::min(TEMP_BUFFER_SIZE, bytesRemaining);
        size_t actualBytesRead = 0;

        std::span<uint8_t> readSpan(tempBuffer.data(), bytesToRead);
        UART::Status readResult = timeout_read(u32ReadTimeout, readSpan, actualBytesRead, stop_tok);

        if (readResult == Status::SUCCESS && actualBytesRead > 0) {
            for (size_t i = 0; i < actualBytesRead && szBytesRead < buffer.size() - 1; ++i) {
                uint8_t ch = readSpan[i];
                // LOG_PRINT(LOG_WERBOSE, LOG_HDR; LOG_STRING("read:"); LOG_HEX8(ch); LOG_STRING("|"); LOG_CHAR(ch));

                if (ch == cDelimiter) {
                    buffer[szBytesRead] = '\0'; // safe null-termination
                    return Status::SUCCESS;
                } else {
                    buffer[szBytesRead++] = ch;
                }
            }
        } else if (readResult == Status::READ_TIMEOUT) {
            eResult = (u32ReadTimeout > 0) ? Status::READ_TIMEOUT : Status::PORT_ACCESS;
        } else {
            eResult = Status::PORT_ACCESS;
        }
    }

    return eResult;
}
