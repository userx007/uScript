#include "uI2C.hpp"
#include "uLogger.hpp"

#include <array>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR   "I2C_DRV     |"
#define LOG_HDR  LOG_STRING(LT_HDR)


bool I2C::is_open() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_iHandle >= 0;
}


// ============================================================================
// PUBLIC UNIFIED INTERFACE IMPLEMENTATION
// ============================================================================

I2C::ReadResult I2C::tout_read(uint32_t u32ReadTimeout,
                               std::span<uint8_t> buffer,
                               const ReadOptions& options) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    ReadResult result;

    switch (options.mode)
    {
        case ReadMode::Exact:
        {
            size_t bytes_read = 0;
            result.status         = timeout_read(u32ReadTimeout, buffer, bytes_read);
            result.bytes_read     = bytes_read;
            result.found_terminator = false;
            break;
        }

        case ReadMode::UntilDelimiter:
        {
            size_t bytes_read = 0;
            result.status         = timeout_read_until(u32ReadTimeout, buffer,
                                                       options.delimiter, bytes_read);
            result.bytes_read     = bytes_read;
            result.found_terminator = (result.status == Status::SUCCESS);
            break;
        }

        case ReadMode::UntilToken:
        {
            result.status         = timeout_wait_for_token(u32ReadTimeout,
                                                           options.token,
                                                           options.use_buffer);
            result.bytes_read     = 0; // Token search does not fill the user buffer
            result.found_terminator = (result.status == Status::SUCCESS);
            break;
        }

        default:
            result.status         = Status::INVALID_PARAM;
            result.bytes_read     = 0;
            result.found_terminator = false;
            break;
    }

    return result;
}


I2C::WriteResult I2C::tout_write(uint32_t u32WriteTimeout,
                                 std::span<const uint8_t> buffer) const
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

I2C::Status I2C::timeout_wait_for_token(uint32_t u32ReadTimeout,
                                        std::span<const uint8_t> token,
                                        bool useBuffer) const
{
    const size_t szTokenLength = token.size();
    if (token.empty() || szTokenLength == 0 || szTokenLength >= I2C_MAX_BUFLENGTH)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid token or length"));
        return Status::INVALID_PARAM;
    }

    uint32_t u32Timeout     = (u32ReadTimeout == 0) ? I2C_READ_DEFAULT_TIMEOUT : u32ReadTimeout;
    bool     bReturnOnTimeout = (u32ReadTimeout != 0);

    std::vector<int> viLps;
    build_kmp_table(token, szTokenLength, viLps);

    return kmp_stream_match(token, viLps, u32Timeout, bReturnOnTimeout, useBuffer);
}


void I2C::build_kmp_table(std::span<const uint8_t> pattern,
                          size_t szLength,
                          std::vector<int>& viLps) const
{
    viLps.resize(szLength);
    int len  = 0;
    viLps[0] = 0;

    for (size_t i = 1; i < szLength; )
    {
        if (pattern[i] == pattern[len])
        {
            viLps[i++] = ++len;
        }
        else
        {
            if (len != 0)
            {
                len = viLps[len - 1];
            }
            else
            {
                viLps[i++] = 0;
            }
        }
    }
}


I2C::Status I2C::kmp_stream_match(std::span<const uint8_t> token,
                                  const std::vector<int>& viLps,
                                  uint32_t u32Timeout,
                                  bool bReturnOnTimeout,
                                  bool useBuffer) const
{
    uint8_t  Buffer[I2C_MAX_BUFLENGTH] = {0};
    uint32_t u32Matched   = 0;
    uint32_t u32BufferPos = 0;

    while (true)
    {
        uint8_t cByte          = 0;
        size_t  actualBytesRead = 0;

        I2C::Status i32ReadResult =
            timeout_read(u32Timeout, std::span<uint8_t>(&cByte, 1), actualBytesRead);

        if (i32ReadResult != Status::SUCCESS || actualBytesRead == 0)
        {
            return (i32ReadResult == Status::READ_TIMEOUT && bReturnOnTimeout)
                   ? Status::READ_TIMEOUT
                   : Status::READ_ERROR;
        }

        if (useBuffer)
        {
            Buffer[u32BufferPos++ % I2C_MAX_BUFLENGTH] = cByte;
        }

        while (u32Matched > 0 && cByte != token[u32Matched])
        {
            u32Matched = static_cast<uint32_t>(viLps[u32Matched - 1]);
        }

        if (cByte == token[u32Matched])
        {
            ++u32Matched;
            if (u32Matched == token.size())
            {
                return Status::SUCCESS;
            }
        }
    }
}


I2C::Status I2C::timeout_read_until(uint32_t u32ReadTimeout,
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
    I2C::Status eResult = Status::RETVAL_NOT_SET;

    while (eResult == Status::RETVAL_NOT_SET)
    {
        const size_t bytesRemaining = buffer.size() - szBytesRead - 1; // reserve for '\0'
        if (bytesRemaining == 0)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Buffer full before delimiter found"));
            return Status::BUFFER_OVERFLOW;
        }

        uint8_t cByte          = 0;
        size_t  actualBytesRead = 0;

        I2C::Status readResult =
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
