#include "uKVCan.hpp"
#include "uLogger.hpp"

#include <array>
#include <charconv>
#include <cstring>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR   "KVCAN_DRV   |"
#define LOG_HDR  LOG_STRING(LT_HDR)

// Linux kernel headers are only available in uKVCanLinux.cpp, but we need
// struct can_filter for apply_temp_rx_filter / restore_rx_filters.
// Pull them in here via the same guard the Linux source uses.
#if defined(__linux__)
#  include <linux/can.h>
#  include <linux/can/raw.h>
#  include <sys/socket.h>
#endif


bool KVCAN::is_open() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_iHandle >= 0;
}


void KVCAN::set_tx_id(uint32_t u32Id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_u32TxId = u32Id;
}


// ============================================================================
// CHANNEL_ID HELPERS
// ============================================================================

std::optional<uint32_t> KVCAN::parse_can_id(std::string_view xtra_params)
{
    // Strip leading and trailing whitespace.
    while (!xtra_params.empty() && (xtra_params.front() == ' ' || xtra_params.front() == '\t'))
        xtra_params.remove_prefix(1);
    while (!xtra_params.empty() && (xtra_params.back() == ' ' || xtra_params.back() == '\t'))
        xtra_params.remove_suffix(1);

    if (xtra_params.empty())
        return std::nullopt;

    uint32_t value = 0;
    int      base  = 10;

    // Detect "0x" / "0X" hex prefix.
    if (xtra_params.size() > 2 &&
        xtra_params[0] == '0' &&
        (xtra_params[1] == 'x' || xtra_params[1] == 'X'))
    {
        xtra_params.remove_prefix(2);
        base = 16;
    }

    const auto [ptr, ec] = std::from_chars(xtra_params.data(),
                                           xtra_params.data() + xtra_params.size(),
                                           value, base);

    if (ec != std::errc{} || ptr != xtra_params.data() + xtra_params.size())
        return std::nullopt;   // parse error or trailing garbage

    return value;
}


void KVCAN::apply_temp_rx_filter(uint32_t can_id,
                                 std::vector<CanFilter>& saved_filters) const
{
#if defined(__linux__)
    // Save the current filter set so it can be restored later.
    // getsockopt with CAN_RAW_FILTER requires knowing the current filter count.
    // We query the option size first, then retrieve the filters.
    socklen_t optlen = 0;
    if (::getsockopt(m_iHandle, SOL_CAN_RAW, CAN_RAW_FILTER, nullptr, &optlen) == 0 &&
        optlen > 0)
    {
        const size_t count = optlen / sizeof(struct can_filter);
        std::vector<struct can_filter> kFilters(count);
        if (::getsockopt(m_iHandle, SOL_CAN_RAW, CAN_RAW_FILTER,
                         kFilters.data(), &optlen) == 0)
        {
            saved_filters.reserve(count);
            for (const auto& kf : kFilters)
                saved_filters.push_back({ kf.can_id, kf.can_mask });
        }
    }

    // Install a single exact-match filter for the requested ID.
    // Use CAN_EFF_MASK (0x1FFFFFFF) so the comparison covers both SFF and EFF
    // without the EFF/RTR/ERR flag bits interfering.
    struct can_filter kf = {};
    kf.can_id   = can_id;
    kf.can_mask = CAN_EFF_MASK;   // exact ID, ignore flag bits

    if (::setsockopt(m_iHandle, SOL_CAN_RAW, CAN_RAW_FILTER,
                     &kf, sizeof(kf)) < 0)
    {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("apply_temp_rx_filter: setsockopt failed, errno:");
                  LOG_INT(err));
        // Non-fatal: the read will proceed without the extra filter.
        saved_filters.clear();
    }
#else
    (void)can_id;
    (void)saved_filters;
#endif
}


void KVCAN::restore_rx_filters(const std::vector<CanFilter>& saved_filters) const
{
#if defined(__linux__)
    if (saved_filters.empty())
    {
        // Restore "pass everything" (NULL filter, zero length).
        ::setsockopt(m_iHandle, SOL_CAN_RAW, CAN_RAW_FILTER, nullptr, 0);
        return;
    }

    std::vector<struct can_filter> kFilters;
    kFilters.reserve(saved_filters.size());
    for (const auto& f : saved_filters)
    {
        struct can_filter kf = {};
        kf.can_id   = f.can_id;
        kf.can_mask = f.can_mask;
        kFilters.push_back(kf);
    }

    if (::setsockopt(m_iHandle, SOL_CAN_RAW, CAN_RAW_FILTER,
                     kFilters.data(),
                     static_cast<socklen_t>(kFilters.size() * sizeof(struct can_filter))) < 0)
    {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("restore_rx_filters: setsockopt failed, errno:");
                  LOG_INT(err));
    }
#else
    (void)saved_filters;
#endif
}


// ============================================================================
// PUBLIC UNIFIED INTERFACE IMPLEMENTATION
// ============================================================================

KVCAN::ReadResult KVCAN::tout_read(uint32_t u32ReadTimeout,
                               std::span<uint8_t> buffer,
                               const ReadOptions& options,
                               std::string_view xtra_params) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    ReadResult result;

    // If an xtra_params was provided, parse it and install a temporary RX filter.
    // The filter is always restored before this method returns, regardless of
    // the outcome.
    std::vector<CanFilter> savedFilters;
    const auto parsedRxId = parse_can_id(xtra_params);
    if (parsedRxId.has_value())
    {
        LOG_PRINT(LOG_DEBUG, LOG_HDR;
                  LOG_STRING("tout_read: xtra_params override rx filter to 0x");
                  LOG_HEX32(*parsedRxId));
        apply_temp_rx_filter(*parsedRxId, savedFilters);
    }

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

    // Always restore the previous filter state.
    if (parsedRxId.has_value())
        restore_rx_filters(savedFilters);

    return result;
}


KVCAN::WriteResult KVCAN::tout_write(uint32_t u32WriteTimeout,
                                 std::span<const uint8_t> buffer,
                                 std::string_view xtra_params) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    WriteResult result;
    size_t bytes_written = 0;

    // Resolve the TX ID: use the parsed xtra_params if provided, otherwise fall
    // back to the default configured via set_tx_id().
    const auto parsedTxId = parse_can_id(xtra_params);
    const uint32_t u32TxId = parsedTxId.value_or(m_u32TxId);

    if (parsedTxId.has_value())
    {
        LOG_PRINT(LOG_DEBUG, LOG_HDR;
                  LOG_STRING("tout_write: xtra_params override tx id to 0x");
                  LOG_HEX32(u32TxId));
    }

    result.status        = timeout_write(u32WriteTimeout, buffer, bytes_written, u32TxId);
    result.bytes_written = bytes_written;

    return result;
}


// ============================================================================
// PRIVATE LEGACY IMPLEMENTATION (INTERNAL USE ONLY)
// ============================================================================

KVCAN::Status KVCAN::timeout_wait_for_token(uint32_t u32ReadTimeout,
                                        std::span<const uint8_t> token,
                                        bool useBuffer) const
{
    const size_t szTokenLength = token.size();
    if (token.empty() || szTokenLength == 0 || szTokenLength >= CAN_DRV_MAX_BUFLENGTH)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid token or length"));
        return Status::INVALID_PARAM;
    }

    const uint32_t u32Timeout      = (u32ReadTimeout == 0) ? CAN_READ_DEFAULT_TIMEOUT
                                                           : u32ReadTimeout;
    const bool     bReturnOnTimeout = (u32ReadTimeout != 0);

    std::vector<int> viLps;
    build_kmp_table(token, szTokenLength, viLps);

    return kmp_stream_match(token, viLps, u32Timeout, bReturnOnTimeout, useBuffer);
}


void KVCAN::build_kmp_table(std::span<const uint8_t> pattern,
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


KVCAN::Status KVCAN::kmp_stream_match(std::span<const uint8_t> token,
                                  const std::vector<int>& viLps,
                                  uint32_t u32Timeout,
                                  bool bReturnOnTimeout,
                                  bool useBuffer) const
{
    // Internal ring buffer that accumulates payload bytes across frames.
    uint8_t  Buffer[CAN_DRV_MAX_BUFLENGTH] = {0};
    uint32_t u32Matched   = 0;
    uint32_t u32BufferPos = 0;

    // Receive frames and feed their payload bytes one-by-one into KMP.
    // A scratch buffer sized to one max KVCAN FD payload is sufficient because
    // timeout_read() fills it with exactly one frame's DLC bytes at a time.
    std::array<uint8_t, CAN_DRV_MAX_DLEN> framePayload = {};

    while (true)
    {
        size_t frameBytes = 0;
        const KVCAN::Status readResult =
            timeout_read(u32Timeout,
                         std::span<uint8_t>(framePayload.data(), framePayload.size()),
                         frameBytes);

        if (readResult != Status::SUCCESS || frameBytes == 0)
        {
            return (readResult == Status::READ_TIMEOUT && bReturnOnTimeout)
                   ? Status::READ_TIMEOUT
                   : Status::READ_ERROR;
        }

        // Walk the payload bytes through KMP state machine.
        for (size_t byteIdx = 0; byteIdx < frameBytes; ++byteIdx)
        {
            const uint8_t cByte = framePayload[byteIdx];

            if (useBuffer)
            {
                Buffer[u32BufferPos++ % CAN_DRV_MAX_BUFLENGTH] = cByte;
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


KVCAN::Status KVCAN::timeout_read_until(uint32_t u32ReadTimeout,
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
    KVCAN::Status eResult = Status::RETVAL_NOT_SET;

    std::array<uint8_t, CAN_DRV_MAX_DLEN> framePayload = {};

    while (eResult == Status::RETVAL_NOT_SET)
    {
        const size_t bytesRemaining = buffer.size() - szBytesRead - 1; // reserve for '\0'
        if (bytesRemaining == 0)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Buffer full before delimiter found"));
            return Status::BUFFER_OVERFLOW;
        }

        // Receive one KVCAN frame worth of payload.
        size_t frameBytes = 0;
        const KVCAN::Status readResult =
            timeout_read(u32ReadTimeout,
                         std::span<uint8_t>(framePayload.data(), framePayload.size()),
                         frameBytes);

        if (readResult == Status::SUCCESS && frameBytes > 0)
        {
            for (size_t i = 0; i < frameBytes && szBytesRead < buffer.size() - 1; ++i)
            {
                const uint8_t ch = framePayload[i];

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
