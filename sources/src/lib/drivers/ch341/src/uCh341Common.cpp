#include "uCh341.hpp"
#include "uLogger.hpp"

#include <array>
#include <string_view>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "CH341_DRV   |"
#define LOG_HDR    LOG_STRING(LT_HDR)


bool CH341::is_open() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_iHandle >= 0;
}


// ============================================================================
// PUBLIC UNIFIED INTERFACE IMPLEMENTATION
// ============================================================================

CH341::ReadResult CH341::tout_read(uint32_t u32ReadTimeout, std::span<uint8_t> buffer,
                            const ReadOptions& options,
                            std::string_view /*xtra_params*/) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    ReadResult result;

    switch (options.mode) {
        case ReadMode::Exact: {
            size_t bytes_read = 0;
            result.status = timeout_read(u32ReadTimeout, buffer, bytes_read);
            result.bytes_read = bytes_read;
            result.found_terminator = false;
            break;
        }

        case ReadMode::UntilDelimiter: {
            size_t bytes_read = 0;
            result.status = timeout_read_until(u32ReadTimeout, buffer, options.delimiter, bytes_read);
            result.bytes_read = bytes_read;
            result.found_terminator = (result.status == Status::SUCCESS);
            (void)purge(true, false);
            break;
        }

        case ReadMode::UntilToken: {
            result.status = timeout_wait_for_token(u32ReadTimeout, options.token, options.use_buffer);
            result.bytes_read = 0;  // Token search doesn't fill user buffer
            result.found_terminator = (result.status == Status::SUCCESS);
            (void)purge(true, false);
            break;
        }

        default:
            result.status = Status::INVALID_PARAM;
            result.bytes_read = 0;
            result.found_terminator = false;
            break;
    }

    return result;
}


CH341::WriteResult CH341::tout_write(uint32_t u32WriteTimeout, std::span<const uint8_t> buffer,
                               std::string_view /*xtra_params*/) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    WriteResult result;
    size_t bytes_written = 0;

    result.status = timeout_write(u32WriteTimeout, buffer, bytes_written);
    result.bytes_written = bytes_written;

    return result;
}


// ============================================================================
// PRIVATE LEGACY IMPLEMENTATION (INTERNAL USE ONLY)
// ============================================================================

CH341::Status CH341::timeout_wait_for_token (uint32_t u32ReadTimeout, std::span<const uint8_t> token, bool useBuffer) const
{
    size_t szTokenLength = token.size();
    if (token.empty() || szTokenLength == 0 || szTokenLength >= CH341_MAX_BUFLENGTH) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid token or length"));
        return Status::INVALID_PARAM;
    }

    uint32_t u32Timeout = (u32ReadTimeout == 0) ? CH341_READ_DEFAULT_TIMEOUT : u32ReadTimeout;
    bool bReturnOnTimeout = (u32ReadTimeout != 0);

    std::vector<int> viLps;
    build_kmp_table(token, szTokenLength, viLps);

    return kmp_stream_match(token, viLps, u32Timeout, bReturnOnTimeout, useBuffer);
}


void CH341::build_kmp_table (std::span<const uint8_t> pattern, size_t szLength, std::vector<int>& viLps) const
{
    viLps.resize(szLength);
    int len = 0;
    viLps[0] = 0;

    for (size_t i = 1; i < szLength; ) {
        if (pattern[i] == pattern[len]) {
            viLps[i++] = ++len;
        } else {
            if (len != 0) {
                len = viLps[len - 1];
            } else {
                viLps[i++] = 0;
            }
        }
    }
}


CH341::Status CH341::kmp_stream_match (std::span<const uint8_t> token, const std::vector<int>& viLps, uint32_t u32Timeout, bool bReturnOnTimeout, bool useBuffer) const
{
    uint8_t Buffer[CH341_MAX_BUFLENGTH] = {0};
    uint32_t u32Matched = 0;
    uint32_t u32BufferPos = 0;

    while (true) {
        uint8_t cByte;
        size_t actualBytesRead = 0;
        CH341::Status i32ReadResult = timeout_read(u32Timeout, std::span<uint8_t>(&cByte, 1), actualBytesRead);

        if (i32ReadResult != Status::SUCCESS || actualBytesRead == 0) {
            return (i32ReadResult == Status::READ_TIMEOUT && bReturnOnTimeout)
                   ? Status::READ_TIMEOUT
                   : Status::READ_ERROR;
        }

        if (useBuffer) {
            Buffer[u32BufferPos++ % CH341_MAX_BUFLENGTH] = cByte;
        }

        while (u32Matched > 0 && cByte != token[u32Matched]) {
            u32Matched = viLps[u32Matched - 1];
        }

        if (cByte == token[u32Matched]) {
            u32Matched++;
            if (u32Matched == token.size()) {
                return Status::SUCCESS;
            }
        }
    }
}


CH341::Status CH341::timeout_read_until (uint32_t u32ReadTimeout, std::span<uint8_t> buffer, uint8_t cDelimiter, size_t& szBytesRead) const
{
    if (buffer.size() < 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Buffer too small for delimiter + null terminator"));
        return Status::INVALID_PARAM;
    }

    constexpr size_t TEMP_BUFFER_SIZE = 64;
    std::array<uint8_t, TEMP_BUFFER_SIZE> tempBuffer = {0};
    szBytesRead = 0;
    CH341::Status eResult = Status::RETVAL_NOT_SET;

    while (eResult == Status::RETVAL_NOT_SET) {
        size_t bytesRemaining = buffer.size() - szBytesRead - 1;  // reserve space for '\0'
        if (bytesRemaining == 0) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Buffer full before delimiter found"));
            return Status::BUFFER_OVERFLOW;
        }

        size_t bytesToRead = std::min(TEMP_BUFFER_SIZE, bytesRemaining);
        size_t actualBytesRead = 0;

        std::span<uint8_t> readSpan(tempBuffer.data(), bytesToRead);
        CH341::Status readResult = timeout_read(u32ReadTimeout, readSpan, actualBytesRead);

        if (readResult == Status::SUCCESS && actualBytesRead > 0) {
            for (size_t i = 0; i < actualBytesRead && szBytesRead < buffer.size() - 1; ++i) {
                uint8_t ch = readSpan[i];

                if (ch == cDelimiter) {
                    buffer[szBytesRead] = '\0';  // safe null-termination
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
