#include "uKVCan.hpp"
#include "uLogger.hpp"
#include "uNumeric.hpp"

#include <array>

#include <sys/socket.h>      // setsockopt
#include <linux/can.h>       // can_filter, CAN_EFF_FLAG, CAN_EFF_MASK, CAN_SFF_MASK
#include <linux/can/raw.h>   // SOL_CAN_RAW, CAN_RAW_FILTER

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
// PUBLIC UNIFIED INTERFACE IMPLEMENTATION
// ============================================================================

KVCAN::ReadResult KVCAN::tout_read(uint32_t u32ReadTimeout,
                               std::span<uint8_t> buffer,
                               const ReadOptions& options,
                               std::string_view xtra_params) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    ReadResult result;

    /* If the caller supplied an xtra_params RX-ID hint, install a transient
     * single-frame acceptance filter for the duration of this call.
     *
     * The previous filter state (m_vFilters) is saved and fully restored
     * before returning, so that subsequent calls without xtra_params continue
     * to use whatever filter set was active before this call.
     *
     *
     * Format: decimal or 0x-prefixed hex CAN ID, e.g. "0x7E8" or "2024".
     * An extended-frame ID is signalled by setting bit 31 (CAN_EFF_FLAG) in
     * the parsed value, matching the same convention used by set_tx_id().
     */
    bool bTransientFilter = false;
    std::vector<CanFilter> savedFilters; // snapshot of m_vFilters before override

    if (!xtra_params.empty())
    {
        uint32_t u32RxId = 0;

        if (numeric::string_to_unsigned<uint32_t>(xtra_params, u32RxId))
        {
            /* Build a kernel filter that accepts exactly this CAN ID.
             * For extended frames (bit 31 set) also set CAN_EFF_FLAG in the
             * mask so the comparison is made against the full 29-bit field. */
            struct can_filter kf = {};
            if (u32RxId & CAN_EFF_FLAG)
            {
                kf.can_id   = u32RxId;
                kf.can_mask = CAN_EFF_MASK | CAN_EFF_FLAG;
            }
            else
            {
                kf.can_id   = u32RxId & CAN_SFF_MASK;
                kf.can_mask = CAN_SFF_MASK;
            }

            if (::setsockopt(m_iHandle, SOL_CAN_RAW, CAN_RAW_FILTER,
                             &kf, sizeof(kf)) == 0)
            {
                bTransientFilter = true;
                savedFilters = m_vFilters; // save current filter state for restore
                LOG_PRINT(LOG_DEBUG, LOG_HDR;
                          LOG_STRING("tout_read: transient RX filter id:");
                          LOG_HEX32(u32RxId));
            }
            else
            {
                LOG_PRINT(LOG_WARNING, LOG_HDR;
                          LOG_STRING("tout_read: failed to set transient RX filter, errno:");
                          LOG_INT(errno));
            }
        }
        else
        {
            LOG_PRINT(LOG_WARNING, LOG_HDR;
                      LOG_STRING("tout_read: xtra_params not a valid CAN ID, ignored:"));
        }
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

    /* Restore the filter state that was active before the transient override.
     *
     * - savedFilters empty  → previous state was accept-all; pass nullptr to
     *                         the kernel to reinstate that.
     * - savedFilters non-empty → rebuild the kernel can_filter array from the
     *                            saved CanFilter list and reapply it.
     *
     * Errors here are non-fatal and only logged; the read result is already
     * determined at this point.
     */
    if (bTransientFilter)
    {
        if (savedFilters.empty())
        {
            // Previous state was accept-all — restore it.
            if (::setsockopt(m_iHandle, SOL_CAN_RAW, CAN_RAW_FILTER, nullptr, 0) < 0)
            {
                LOG_PRINT(LOG_WARNING, LOG_HDR;
                          LOG_STRING("tout_read: failed to restore accept-all filter, errno:");
                          LOG_INT(errno));
            }
        }
        else
        {
			m_vFilters = savedFilters;

            if (::setsockopt(m_iHandle, SOL_CAN_RAW, CAN_RAW_FILTER,
                             m_vFilters.data(),
                             static_cast<socklen_t>(m_vFilters.size() * sizeof(struct can_filter))) < 0)
            {
                LOG_PRINT(LOG_WARNING, LOG_HDR;
                          LOG_STRING("tout_read: failed to restore previous filters, errno:");
                          LOG_INT(errno));
            }
        }

        LOG_PRINT(LOG_DEBUG, LOG_HDR;
                  LOG_STRING("tout_read: transient RX filter removed, restored filter count:");
                  LOG_UINT32(static_cast<uint32_t>(m_vFilters.size())));
    }

    return result;
}


KVCAN::WriteResult KVCAN::tout_write(uint32_t u32WriteTimeout,
                                 std::span<const uint8_t> buffer,
                                 std::string_view xtra_params) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    WriteResult result;

    /* If the caller supplied an xtra_params TX-ID override, use it for this
     * frame only — do NOT persist it to m_u32TxId.
     *
     * Format: decimal or 0x-prefixed hex CAN ID, e.g. "0x123" or "291".
     * Setting bit 31 (CAN_EFF_FLAG) selects a 29-bit extended-frame ID,
     * matching the same convention used by set_tx_id().
     */
    uint32_t u32EffectiveTxId = m_u32TxId;
    if (!xtra_params.empty())
    {
        uint32_t u32Override = 0;

        if (numeric::string_to_unsigned<uint32_t>(xtra_params, u32Override))
        {
            u32EffectiveTxId = u32Override;
            LOG_PRINT(LOG_DEBUG, LOG_HDR;
                      LOG_STRING("tout_write: TX ID overridden by xtra_params:");
                      LOG_HEX32(u32EffectiveTxId));
        }
        else
        {
            LOG_PRINT(LOG_WARNING, LOG_HDR;
                      LOG_STRING("tout_write: xtra_params not a valid CAN ID, using default TX ID"));
        }
    }

    size_t bytes_written = 0;
    result.status        = timeout_write(u32WriteTimeout, buffer, bytes_written, u32EffectiveTxId);
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
