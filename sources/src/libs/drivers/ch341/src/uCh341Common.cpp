#include "uCh341.hpp"
#include "uKmpMatch.hpp"
#include "uLogger.hpp"

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
//                            LOG DEFINITIONS                                  //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
#undef LT_HDR
#endif
#ifdef LOG_HDR
#undef LOG_HDR
#endif

#define LT_HDR  "CH341_DRV   |"
#define LOG_HDR LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                            IMPLEMENTATION                                   //
/////////////////////////////////////////////////////////////////////////////////

bool CH341::is_open() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_iHandle >= 0;
}

CH341::ReadResult CH341::tout_read(uint32_t u32ReadTimeout, std::span<uint8_t> buffer,
                                   const ReadOptions &sOptions,
                                   std::string_view /*xtra_params*/,
                                   std::stop_token stop_tok) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    ReadResult result;

    switch (sOptions.mode) {
    case ReadMode::Exact: {
        size_t bytes_read       = 0;
        result.status           = timeout_read(u32ReadTimeout, buffer, bytes_read, stop_tok);
        result.bytes_read       = bytes_read;
        result.found_terminator = false;
        break;
    }

    case ReadMode::UntilDelimiter: {
        size_t bytes_read       = 0;
        result.status           = timeout_read_until(u32ReadTimeout, buffer, sOptions.delimiter, bytes_read, stop_tok);
        result.bytes_read       = bytes_read;
        result.found_terminator = (result.status == Status::SUCCESS);
        (void)purge(true, false);
        break;
    }

    case ReadMode::UntilToken: {
        result.status           = timeout_wait_for_token(u32ReadTimeout, sOptions.token, sOptions.use_buffer, stop_tok);
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

CH341::WriteResult CH341::tout_write(uint32_t u32WriteTimeout, std::span<const uint8_t> buffer,
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

CH341::Status CH341::timeout_wait_for_token(uint32_t u32ReadTimeout, std::span<const uint8_t> token, bool bUseBuffer,
                                            std::stop_token stop_tok) const
{
    size_t szTokenLength = token.size();
    if (token.empty() || szTokenLength == 0 || szTokenLength >= CH341_MAX_BUFLENGTH) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid token or length"));
        return Status::INVALID_PARAM;
    }

    uint32_t u32Timeout   = (u32ReadTimeout == 0) ? CH341_READ_DEFAULT_TIMEOUT : u32ReadTimeout;
    bool bReturnOnTimeout = (u32ReadTimeout != 0);

    std::vector<int> viLps;
    build_kmp_table(token, szTokenLength, viLps);

    return kmp_stream_match(token, viLps, u32Timeout, bReturnOnTimeout, bUseBuffer, stop_tok);
}

void CH341::build_kmp_table(std::span<const uint8_t> pattern, size_t szLength, std::vector<int> &vViLps) const
{
    ukmp::build_kmp_table(pattern, szLength, vViLps);
}

CH341::Status CH341::kmp_stream_match(std::span<const uint8_t> token, const std::vector<int> &vViLps, uint32_t u32Timeout, bool bReturnOnTimeout, bool bUseBuffer,
                                      std::stop_token stop_tok) const
{
    return ukmp::kmp_stream_match(
        [this, stop_tok](uint32_t timeout, std::span<uint8_t> buf, size_t &bytesRead) { return timeout_read(timeout, buf, bytesRead, stop_tok); },
        token, vViLps, u32Timeout, bReturnOnTimeout, bUseBuffer,
        /*szChunkBufferSize=*/1, /*szRingBufferSize=*/CH341_MAX_BUFLENGTH);
}

CH341::Status CH341::timeout_read_until(uint32_t u32ReadTimeout, std::span<uint8_t> buffer, uint8_t u8CDelimiter, size_t &szBytesRead,
                                        std::stop_token stop_tok) const
{
    if (buffer.size() < 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Buffer too small for delimiter + null terminator"));
        return Status::INVALID_PARAM;
    }

    constexpr size_t TEMP_BUFFER_SIZE                = 64;
    std::array<uint8_t, TEMP_BUFFER_SIZE> tempBuffer = {0};
    szBytesRead                                      = 0;
    CH341::Status eResult                            = Status::RETVAL_NOT_SET;

    while (eResult == Status::RETVAL_NOT_SET) {
        size_t bytesRemaining = buffer.size() - szBytesRead - 1; // reserve space for '\0'
        if (bytesRemaining == 0) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Buffer full before delimiter found"));
            return Status::BUFFER_OVERFLOW;
        }

        size_t bytesToRead     = std::min(TEMP_BUFFER_SIZE, bytesRemaining);
        size_t actualBytesRead = 0;

        std::span<uint8_t> readSpan(tempBuffer.data(), bytesToRead);
        CH341::Status readResult = timeout_read(u32ReadTimeout, readSpan, actualBytesRead, stop_tok);

        if (readResult == Status::SUCCESS && actualBytesRead > 0) {
            for (size_t i = 0; i < actualBytesRead && szBytesRead < buffer.size() - 1; ++i) {
                uint8_t ch = readSpan[i];

                if (ch == u8CDelimiter) {
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
