#include "uKVCan.hpp"
#include "uLogger.hpp"
#include "uNumeric.hpp"
#include "uKmpMatch.hpp"

#include <array>
#include <algorithm>
#include <vector>

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
    ReadResult result;

    /* ---------- locked section: resolve shared state, apply transient filter --
     *
     * The mutex is held only while reading/writing shared members (m_iHandle,
     * m_u32TxId, m_vFilters) and while calling setsockopt().  It is released
     * before any blocking I/O so that is_open(), set_tx_id(), set_filters(),
     * and tout_write() from other threads are not stalled for the full timeout
     * duration.
     *
     * If the caller supplied an xtra_params RX-ID hint, install a transient
     * single-frame acceptance filter here, then release the lock.  The filter
     * remains on the socket for the duration of the blocking read below and is
     * restored (under the lock again) afterwards.
     *
     * Format: decimal or 0x-prefixed hex CAN ID, e.g. "0x7E8" or "2024".
     * An extended-frame ID is signalled by setting bit 31 (CAN_EFF_FLAG) in
     * the parsed value, matching the same convention used by set_tx_id().
     */
    bool bTransientFilter = false;
    std::vector<CanFilter> savedFilters; // snapshot of m_vFilters before override

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!xtra_params.empty())
        {
            uint32_t u32RxId = 0;

            if (numeric::str2uint32(xtra_params, u32RxId))
            {
                /* Build a kernel filter that accepts exactly this CAN ID.
                 * For extended frames (bit 31 set) also set CAN_EFF_FLAG in
                 * the mask so the comparison is made against the full 29-bit
                 * field. */
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
    } // ---- mutex released here; blocking I/O proceeds without holding the lock

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

    /* ---------- locked section: restore filter state -----------------------
     *
     * - savedFilters empty  → previous state was accept-all; install an
     *                         explicit can_id=0/can_mask=0 filter to reinstate
     *                         that (see note below — nullptr/0 does NOT do this).
     * - savedFilters non-empty → rebuild the kernel can_filter array from the
     *                            saved CanFilter list and reapply it.
     *
     * Errors here are non-fatal and only logged; the read result is already
     * determined at this point.
     */
    if (bTransientFilter)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (savedFilters.empty())
        {
            // Previous state was accept-all — restore it.
            //
            // IMPORTANT: setsockopt(CAN_RAW_FILTER, nullptr, 0) does NOT mean
            // "accept everything" — SocketCAN registers one kernel receiver
            // per filter entry, so a 0-length list deregisters all of them
            // and the socket stops receiving ANY frame. Once this socket has
            // had a transient filter installed (as it has, right above), the
            // only way back to "accept all" is an explicit filter that
            // matches every id: can_id = 0, can_mask = 0, since
            // (frame_id & 0) == (0 & 0) is always true.
            struct can_filter acceptAllFilter = {};
            acceptAllFilter.can_id  = 0;
            acceptAllFilter.can_mask = 0;

            if (::setsockopt(m_iHandle, SOL_CAN_RAW, CAN_RAW_FILTER,
                             &acceptAllFilter, sizeof(acceptAllFilter)) < 0)
            {
                LOG_PRINT(LOG_WARNING, LOG_HDR;
                          LOG_STRING("tout_read: failed to restore accept-all filter, errno:");
                          LOG_INT(errno));
            }
            m_vFilters.clear(); // keep mirror in sync; "empty" is our own
                                // convention meaning accept-all, not "no filter
                                // programmed on the socket"
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


uint32_t KVCAN::resolveTxId(std::string_view xtra_params) const
{
    if (xtra_params.empty())
    {
        return m_u32TxId;
    }
    uint32_t u32Override = 0;
    if (numeric::str2uint32(xtra_params, u32Override))
    {
        return u32Override;
    }
    LOG_PRINT(LOG_WARNING, LOG_HDR;
              LOG_STRING("resolveTxId: xtra_params not a valid CAN ID, using default TX ID"));
    return m_u32TxId;
}
KVCAN::WriteResult KVCAN::tout_write(uint32_t u32WriteTimeout,
                                 std::span<const uint8_t> buffer,
                                 std::string_view xtra_params) const
{
    WriteResult result;

    /* ---------- locked section: resolve effective TX ID --------------------
     *
     * The mutex is held only while reading shared members (m_u32TxId).
     * It is released before the blocking write so that other callers
     * (is_open(), set_tx_id(), set_filters(), tout_read()) are not stalled
     * for the full write-timeout duration.
     *
     * If the caller supplied an xtra_params TX-ID override, use it for this
     * frame only — do NOT persist it to m_u32TxId.
     *
     * Format: decimal or 0x-prefixed hex CAN ID, e.g. "0x123" or "291".
     * Setting bit 31 (CAN_EFF_FLAG) selects a 29-bit extended-frame ID,
     * matching the same convention used by set_tx_id().
     */
    uint32_t u32EffectiveTxId;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        u32EffectiveTxId = resolveTxId(xtra_params);

        if (!xtra_params.empty())
        {
                LOG_PRINT(LOG_DEBUG, LOG_HDR;
                      LOG_STRING("tout_write: effective TX ID for this exchange:");
                          LOG_HEX32(u32EffectiveTxId));
        }

        /* ---- Arm the RX acceptance filter for this exact TX id -----------
         *
         * Guards against a race with fast (e.g. loopback/simulator) replies:
         * SocketCAN checks a frame against the socket's filter only at the
         * moment the kernel delivers it. If a reply on u32EffectiveTxId
         * arrives before a later tout_read() call installs its own transient
         * filter for that id, it is silently dropped there and then — a
         * filter installed afterwards cannot retroactively rescue it. Once a
         * frame IS accepted into the socket's receive queue, though, later
         * filter changes cannot un-queue it. So we widen (never narrow) the
         * filter to accept u32EffectiveTxId here, before the frame is put on
         * the bus, closing that window regardless of how tout_read() manages
         * its own filter afterwards.
         *
         * An empty m_vFilters is our own convention for "accept everything"
         * (see set_filters()), which already covers any id, so nothing to do
         * in that case. Otherwise we only append and re-apply the filter set
         * if this exact id isn't already covered by an existing entry.
         */
        const uint32_t u32FilterMask = (u32EffectiveTxId & CAN_EFF_FLAG)
                                      ? (CAN_EFF_FLAG | CAN_EFF_MASK)
                                      : CAN_SFF_MASK;

        const bool bAlreadyCovered = m_vFilters.empty() ||
            std::any_of(m_vFilters.begin(), m_vFilters.end(),
                        [&](const CanFilter& f)
                        {
                            return f.can_id == u32EffectiveTxId && f.can_mask == u32FilterMask;
                        });

        if (!bAlreadyCovered)
        {
            std::vector<CanFilter> vWidened = m_vFilters;
            vWidened.push_back(CanFilter{u32EffectiveTxId, u32FilterMask});

            std::vector<struct can_filter> kFilters;
            kFilters.reserve(vWidened.size());
            for (const auto& f : vWidened)
            {
                struct can_filter kf = {};
                kf.can_id   = f.can_id;
                kf.can_mask = f.can_mask;
                kFilters.push_back(kf);
            }

            if (::setsockopt(m_iHandle, SOL_CAN_RAW, CAN_RAW_FILTER,
                             kFilters.data(),
                             static_cast<socklen_t>(kFilters.size() * sizeof(struct can_filter))) == 0)
            {
                m_vFilters = std::move(vWidened);
                LOG_PRINT(LOG_DEBUG, LOG_HDR;
                          LOG_STRING("tout_write: widened RX filter to also accept TX id:");
                          LOG_HEX32(u32EffectiveTxId));
            }
            else
            {
                LOG_PRINT(LOG_WARNING, LOG_HDR;
                          LOG_STRING("tout_write: failed to widen RX filter for TX id, errno:");
                          LOG_INT(errno));
            }
        }
    } // ---- mutex released here; blocking write proceeds without holding the lock

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
    ukmp::build_kmp_table(pattern, szLength, viLps);
}


KVCAN::Status KVCAN::kmp_stream_match(std::span<const uint8_t> token,
                                  const std::vector<int>& viLps,
                                  uint32_t u32Timeout,
                                  bool bReturnOnTimeout,
                                  bool useBuffer) const
{
    // Receive frames and feed their payload bytes one-by-one into KMP.
    // A scratch buffer sized to one max KVCAN FD payload is sufficient because
    // timeout_read() fills it with exactly one frame's DLC bytes at a time.
    // The ring buffer (used only when useBuffer) is sized independently, to
    // the driver's overall max buffer length rather than a single frame.
    return ukmp::kmp_stream_match(
        [this](uint32_t timeout, std::span<uint8_t> buf, size_t& bytesRead) { return timeout_read(timeout, buf, bytesRead); },
        token, viLps, u32Timeout, bReturnOnTimeout, useBuffer,
        /*szChunkBufferSize=*/CAN_DRV_MAX_DLEN, /*szRingBufferSize=*/CAN_DRV_MAX_BUFLENGTH);
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
