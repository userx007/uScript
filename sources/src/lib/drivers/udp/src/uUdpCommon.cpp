#include "uUdp.hpp"
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

#define LT_HDR   "UDP_DRV     |"
#define LOG_HDR  LOG_STRING(LT_HDR)


bool UDP::is_open() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_iHandle >= 0;
}


// ============================================================================
// PUBLIC UNIFIED INTERFACE IMPLEMENTATION
// ============================================================================

UDP::ReadResult UDP::tout_read(uint32_t u32ReadTimeout,
                           std::span<uint8_t> buffer,
                           const ReadOptions& options,
                           std::string_view xtra_params) const
{
    ReadResult result;

    if (!xtra_params.empty())
    {
        // The socket is connect()ed to a single default peer, so the kernel
        // already scopes incoming datagrams to it — there is no per-call
        // source filter for xtra_params to install here (unlike the CAN
        // driver's temporary RX filter). Accepted only to satisfy
        // ICommDriver; otherwise ignored.
        LOG_PRINT(LOG_WARNING, LOG_HDR;
                  LOG_STRING("tout_read: xtra_params is not used on the read path, ignored"));
    }

    const uint32_t u32Timeout = (u32ReadTimeout == 0) ? UDP_READ_DEFAULT_TIMEOUT : u32ReadTimeout;

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


UDP::WriteResult UDP::tout_write(uint32_t u32WriteTimeout,
                             std::span<const uint8_t> buffer,
                             std::string_view xtra_params) const
{
    WriteResult result;

    const uint32_t u32Timeout = (u32WriteTimeout == 0) ? UDP_WRITE_DEFAULT_TIMEOUT : u32WriteTimeout;

    if (xtra_params.empty())
    {
        // Send to the default peer recorded by open()'s connect() call.
        size_t bytes_written = 0;
        result.status        = timeout_write(u32Timeout, buffer, bytes_written,
                                             /*pDestAddr=*/nullptr, /*szDestAddrLen=*/0);
        result.bytes_written = bytes_written;
        return result;
    }

    // Per-call destination override — parse "host:port" (numeric only) and
    // sendto() it for this single datagram.
    std::vector<uint8_t> vAddrStorage;
    if (!resolve_numeric_host_port(xtra_params, vAddrStorage))
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("tout_write: xtra_params is not a valid numeric host:port"));
        result.status        = Status::INVALID_PARAM;
        result.bytes_written = 0;
        return result;
    }

    size_t bytes_written = 0;
    result.status        = timeout_write(u32Timeout, buffer, bytes_written,
                                         vAddrStorage.data(), vAddrStorage.size());
    result.bytes_written = bytes_written;

    return result;
}


// ============================================================================
// PRIVATE LEGACY IMPLEMENTATION (INTERNAL USE ONLY)
// ============================================================================

UDP::Status UDP::timeout_wait_for_token(uint32_t u32ReadTimeout,
                                    std::span<const uint8_t> token,
                                    bool useBuffer) const
{
    const size_t szTokenLength = token.size();
    if (token.empty() || szTokenLength == 0 || szTokenLength >= UDP_MAX_DGRAM_LEN)
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


void UDP::build_kmp_table(std::span<const uint8_t> pattern,
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


UDP::Status UDP::kmp_stream_match(std::span<const uint8_t> token,
                              const std::vector<int>& viLps,
                              uint32_t u32Timeout,
                              bool bReturnOnTimeout,
                              bool useBuffer) const
{
    // Internal ring buffer that accumulates streamed bytes across datagrams.
    std::vector<uint8_t> Buffer(useBuffer ? UDP_MAX_DGRAM_LEN : 0);
    uint32_t u32Matched   = 0;
    uint32_t u32BufferPos = 0;

    // Scratch buffer for one datagram at a time. Sized to the theoretical
    // max so no legal datagram is ever truncated mid-search.
    std::vector<uint8_t> datagram(UDP_MAX_DGRAM_LEN);

    while (true)
    {
        size_t datagramBytes = 0;
        const UDP::Status readResult =
            timeout_read(u32Timeout,
                        std::span<uint8_t>(datagram.data(), datagram.size()),
                        datagramBytes);

        if (readResult != Status::SUCCESS || datagramBytes == 0)
        {
            return (readResult == Status::READ_TIMEOUT && bReturnOnTimeout)
                   ? Status::READ_TIMEOUT
                   : Status::READ_ERROR;
        }

        for (size_t byteIdx = 0; byteIdx < datagramBytes; ++byteIdx)
        {
            const uint8_t cByte = datagram[byteIdx];

            if (useBuffer)
            {
                Buffer[u32BufferPos++ % UDP_MAX_DGRAM_LEN] = cByte;
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


UDP::Status UDP::timeout_read_until(uint32_t u32ReadTimeout,
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
    UDP::Status eResult = Status::RETVAL_NOT_SET;

    // Scratch buffer for one datagram at a time.
    std::vector<uint8_t> datagram(UDP_MAX_DGRAM_LEN);

    while (eResult == Status::RETVAL_NOT_SET)
    {
        const size_t bytesRemaining = buffer.size() - szBytesRead - 1; // reserve for '\0'
        if (bytesRemaining == 0)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Buffer full before delimiter found"));
            return Status::BUFFER_OVERFLOW;
        }

        size_t datagramBytes = 0;
        const UDP::Status readResult =
            timeout_read(u32ReadTimeout,
                        std::span<uint8_t>(datagram.data(), datagram.size()),
                        datagramBytes);

        if (readResult == Status::SUCCESS && datagramBytes > 0)
        {
            // NOTE: as with the CAN driver, any bytes received after the
            // delimiter within this same datagram are discarded when we
            // return early below.
            for (size_t i = 0; i < datagramBytes && szBytesRead < buffer.size() - 1; ++i)
            {
                const uint8_t ch = datagram[i];

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
