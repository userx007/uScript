#include "uEth.hpp"
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

#define LT_HDR   "ETH_DRV     |"
#define LOG_HDR  LOG_STRING(LT_HDR)


bool ETH::is_open() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_iHandle >= 0;
}


// ============================================================================
// PUBLIC UNIFIED INTERFACE IMPLEMENTATION
// ============================================================================

ETH::ReadResult ETH::tout_read(uint32_t u32ReadTimeout,
                           std::span<uint8_t> buffer,
                           const ReadOptions& options,
                           std::string_view xtra_params) const
{
    ReadResult result;

    if (!xtra_params.empty())
    {
        // Single-peer TCP client: there is no per-call destination the way a
        // CAN ID selects a frame, so xtra_params is accepted only to satisfy
        // ICommDriver's shared surface and otherwise ignored here.
        LOG_PRINT(LOG_WARNING, LOG_HDR;
                  LOG_STRING("tout_read: xtra_params is not used by this driver, ignored"));
    }

    // Resolve the 0 == "use default" convention once, up front, so it applies
    // uniformly to all three read modes below.
    const uint32_t u32Timeout = (u32ReadTimeout == 0) ? ETH_READ_DEFAULT_TIMEOUT : u32ReadTimeout;

    switch (options.mode)
    {
        case ReadMode::Exact:
        {
            size_t bytes_read = 0;
            result.status           = timeout_read(u32Timeout, buffer, bytes_read);
            result.bytes_read       = bytes_read;
            result.found_terminator = false;
            break;
        }

        case ReadMode::UntilDelimiter:
        {
            size_t bytes_read = 0;
            result.status           = timeout_read_until(u32Timeout, buffer,
                                                         options.delimiter, bytes_read);
            result.bytes_read       = bytes_read;
            result.found_terminator = (result.status == Status::SUCCESS);
            break;
        }

        case ReadMode::UntilToken:
        {
            result.status           = timeout_wait_for_token(u32Timeout,
                                                             options.token,
                                                             options.use_buffer);
            result.bytes_read       = 0; // Token search does not fill the caller's buffer
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


ETH::WriteResult ETH::tout_write(uint32_t u32WriteTimeout,
                             std::span<const uint8_t> buffer,
                             std::string_view xtra_params) const
{
    WriteResult result;

    if (!xtra_params.empty())
    {
        LOG_PRINT(LOG_WARNING, LOG_HDR;
                  LOG_STRING("tout_write: xtra_params is not used by this driver, ignored"));
    }

    // Unlike the CAN driver — where a write is a single non-blocking frame
    // enqueue and the timeout parameter is unused — a TCP send(2) can block
    // or return a short count, so this driver genuinely needs a resolved
    // deadline to bound the retry loop in timeout_write().
    const uint32_t u32Timeout = (u32WriteTimeout == 0) ? ETH_WRITE_DEFAULT_TIMEOUT : u32WriteTimeout;

    size_t bytes_written = 0;
    result.status        = timeout_write(u32Timeout, buffer, bytes_written);
    result.bytes_written = bytes_written;

    return result;
}


// ============================================================================
// PRIVATE LEGACY IMPLEMENTATION (INTERNAL USE ONLY)
// ============================================================================

ETH::Status ETH::timeout_wait_for_token(uint32_t u32ReadTimeout,
                                    std::span<const uint8_t> token,
                                    bool useBuffer) const
{
    const size_t szTokenLength = token.size();
    if (token.empty() || szTokenLength == 0 || szTokenLength >= ETH_MAX_BUFLENGTH)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid token or length"));
        return Status::INVALID_PARAM;
    }

    std::vector<int> viLps;
    build_kmp_table(token, szTokenLength, viLps);

    // u32ReadTimeout has already been resolved from 0 by tout_read(), so a
    // timeout here always reflects a real, caller-meaningful deadline.
    return kmp_stream_match(token, viLps, u32ReadTimeout, /*bReturnOnTimeout=*/true, useBuffer);
}


void ETH::build_kmp_table(std::span<const uint8_t> pattern,
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


ETH::Status ETH::kmp_stream_match(std::span<const uint8_t> token,
                              const std::vector<int>& viLps,
                              uint32_t u32Timeout,
                              bool bReturnOnTimeout,
                              bool useBuffer) const
{
    // Internal ring buffer that accumulates streamed bytes across chunks.
    uint8_t  Buffer[ETH_MAX_BUFLENGTH] = {0};
    uint32_t u32Matched   = 0;
    uint32_t u32BufferPos = 0;

    // Receive bytes in chunks and feed them one-by-one into KMP. A chunk may
    // span (or split) multiple messages; the KMP state machine handles that
    // transparently since it only cares about the byte sequence, not chunk
    // boundaries.
    std::array<uint8_t, ETH_MAX_BUFLENGTH> chunk = {};

    while (true)
    {
        size_t chunkBytes = 0;
        const ETH::Status readResult =
            timeout_read(u32Timeout,
                         std::span<uint8_t>(chunk.data(), chunk.size()),
                         chunkBytes);

        if (readResult != Status::SUCCESS || chunkBytes == 0)
        {
            return (readResult == Status::READ_TIMEOUT && bReturnOnTimeout)
                   ? Status::READ_TIMEOUT
                   : Status::READ_ERROR;
        }

        for (size_t byteIdx = 0; byteIdx < chunkBytes; ++byteIdx)
        {
            const uint8_t cByte = chunk[byteIdx];

            if (useBuffer)
            {
                Buffer[u32BufferPos++ % ETH_MAX_BUFLENGTH] = cByte;
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
}


ETH::Status ETH::timeout_read_until(uint32_t u32ReadTimeout,
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
    ETH::Status eResult = Status::RETVAL_NOT_SET;

    std::array<uint8_t, ETH_MAX_BUFLENGTH> chunk = {};

    while (eResult == Status::RETVAL_NOT_SET)
    {
        const size_t bytesRemaining = buffer.size() - szBytesRead - 1; // reserve for '\0'
        if (bytesRemaining == 0)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Buffer full before delimiter found"));
            return Status::BUFFER_OVERFLOW;
        }

        // Receive one chunk of stream bytes.
        size_t chunkBytes = 0;
        const ETH::Status readResult =
            timeout_read(u32ReadTimeout,
                         std::span<uint8_t>(chunk.data(), chunk.size()),
                         chunkBytes);

        if (readResult == Status::SUCCESS && chunkBytes > 0)
        {
            // NOTE: as with the CAN driver's per-frame version, any bytes
            // received after the delimiter within this same chunk are
            // discarded when we return early below. On a byte stream this is
            // more likely to matter than on CAN, since a single recv() can
            // easily contain the start of the next message. Callers that
            // expect back-to-back delimited messages should prefer
            // ReadMode::UntilToken or size their reads to one message at a
            // time.
            for (size_t i = 0; i < chunkBytes && szBytesRead < buffer.size() - 1; ++i)
            {
                const uint8_t ch = chunk[i];

                if (ch == cDelimiter)
                {
                    buffer[szBytesRead] = '\0';
                    return Status::SUCCESS;
                }
                buffer[szBytesRead++] = ch;
            }
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
