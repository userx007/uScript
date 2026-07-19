#include "uKSpi.hpp"
#include "uLogger.hpp"
#include "uKmpMatch.hpp"

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

#define LT_HDR   "KSPI_DRV    |"
#define LOG_HDR  LOG_STRING(LT_HDR)


bool KSPI::is_open() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_iHandle >= 0;
}


// ============================================================================
// PUBLIC UNIFIED INTERFACE IMPLEMENTATION
// ============================================================================

KSPI::ReadResult KSPI::tout_read(uint32_t u32ReadTimeout,
                               std::span<uint8_t> buffer,
                               const ReadOptions& options,
                               std::string_view /*xtra_params*/) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    ReadResult result;

    switch (options.mode)
    {
        case ReadMode::Exact:
        {
            size_t bytes_read = 0;
            result.status           = timeout_read(u32ReadTimeout, buffer, bytes_read);
            result.bytes_read       = bytes_read;
            result.found_terminator = false;
            break;
        }

        case ReadMode::UntilDelimiter:
        {
            size_t bytes_read = 0;
            result.status           = timeout_read_until(u32ReadTimeout, buffer,
                                                         options.delimiter, bytes_read);
            result.bytes_read       = bytes_read;
            result.found_terminator = (result.status == Status::SUCCESS);
            break;
        }

        case ReadMode::UntilToken:
        {
            result.status           = timeout_wait_for_token(u32ReadTimeout,
                                                             options.token,
                                                             options.use_buffer);
            result.bytes_read       = 0; // Token search does not fill the user buffer
            result.found_terminator = (result.status == Status::SUCCESS);
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


KSPI::WriteResult KSPI::tout_write(uint32_t u32WriteTimeout,
                                 std::span<const uint8_t> buffer,
                                 std::string_view /*xtra_params*/) const
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

KSPI::Status KSPI::timeout_wait_for_token(uint32_t u32ReadTimeout,
                                        std::span<const uint8_t> token,
                                        bool useBuffer) const
{
    const size_t szTokenLength = token.size();
    if (token.empty() || szTokenLength == 0 || szTokenLength >= KSPI_MAX_BUFLENGTH)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid token or length"));
        return Status::INVALID_PARAM;
    }

    const uint32_t u32Timeout      = (u32ReadTimeout == 0) ? KSPI_READ_DEFAULT_TIMEOUT : u32ReadTimeout;
    const bool     bReturnOnTimeout = (u32ReadTimeout != 0);

    std::vector<int> viLps;
    build_kmp_table(token, szTokenLength, viLps);

    return kmp_stream_match(token, viLps, u32Timeout, bReturnOnTimeout, useBuffer);
}


void KSPI::build_kmp_table(std::span<const uint8_t> pattern,
                          size_t szLength,
                          std::vector<int>& viLps) const
{
    ukmp::build_kmp_table(pattern, szLength, viLps);
}


KSPI::Status KSPI::kmp_stream_match(std::span<const uint8_t> token,
                                  const std::vector<int>& viLps,
                                  uint32_t u32Timeout,
                                  bool bReturnOnTimeout,
                                  bool useBuffer) const
{
    return ukmp::kmp_stream_match(
        [this](uint32_t timeout, std::span<uint8_t> buf, size_t& bytesRead) { return timeout_read(timeout, buf, bytesRead); },
        token, viLps, u32Timeout, bReturnOnTimeout, useBuffer,
        /*szChunkBufferSize=*/1, /*szRingBufferSize=*/KSPI_MAX_BUFLENGTH);
}


KSPI::Status KSPI::timeout_read_until(uint32_t u32ReadTimeout,
                                    std::span<uint8_t> buffer,
                                    uint8_t cDelimiter,
                                    size_t& szBytesRead) const
{
    if (buffer.size() < 2)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Buffer too small for delimiter + null terminator"));
        return Status::INVALID_PARAM;
    }

    szBytesRead = 0;
    KSPI::Status eResult = Status::RETVAL_NOT_SET;

    while (eResult == Status::RETVAL_NOT_SET)
    {
        const size_t bytesRemaining = buffer.size() - szBytesRead - 1; // reserve for '\0'
        if (bytesRemaining == 0)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Buffer full before delimiter found"));
            return Status::BUFFER_OVERFLOW;
        }

        uint8_t cByte           = 0;
        size_t  actualBytesRead = 0;

        KSPI::Status readResult =
            timeout_read(u32ReadTimeout, std::span<uint8_t>(&cByte, 1), actualBytesRead);

        if (readResult == Status::SUCCESS && actualBytesRead > 0)
        {
            if (cByte == cDelimiter)
            {
                buffer[szBytesRead] = '\0';
                return Status::SUCCESS;
            }
            buffer[szBytesRead++] = cByte;
        }
        else if (readResult == Status::READ_TIMEOUT)
        {
            eResult = (u32ReadTimeout > 0) ? Status::READ_TIMEOUT : Status::PORT_ACCESS;
        }
        else
        {
            eResult = Status::PORT_ACCESS;
        }
    }

    return eResult;
}
